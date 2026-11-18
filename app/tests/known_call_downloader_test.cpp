// KnownCallDownloader against a server on 127.0.0.1 in this process: no internet is reached.
#include "known_call_downloader.hpp"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <memory>

using namespace kz4ap::app;

namespace {

// A request as the server saw it: its path and its headers, names in lower case.
struct Request {
    QByteArray path;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray header(const QByteArray& name) const {
        for (const auto& [n, v] : headers)
            if (n == name) return v;
        return {};
    }
    bool has(const QByteArray& name) const {
        for (const auto& [n, v] : headers)
            if (n == name) return true;
        return false;
    }
};

// A tiny HTTP/1.1 server: each request gets the handler's bytes, then the connection closes; an empty answer leaves it open and silent.
class Server {
public:
    using Handler = std::function<QByteArray(const Request&)>;
    explicit Server(Handler handler) : handler_(std::move(handler)) {
        server_.listen(QHostAddress::LocalHost);
        QObject::connect(&server_, &QTcpServer::newConnection, [this] {
            while (QTcpSocket* socket = server_.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const qsizetype end = buffer->indexOf("\r\n\r\n");
                    if (end < 0) return;
                    const QList<QByteArray> lines = buffer->left(end).split('\n');
                    buffer->clear();
                    Request request;
                    const QList<QByteArray> first = lines.front().trimmed().split(' ');
                    request.path = first.size() > 1 ? first[1] : QByteArray();
                    for (qsizetype i = 1; i < lines.size(); ++i) {
                        const qsizetype colon = lines[i].indexOf(':');
                        if (colon > 0) request.headers.append({lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed()});
                    }
                    requests.append(request);
                    const QByteArray answer = handler_(request);
                    if (answer.isEmpty()) return;  // a stall
                    socket->write(answer);
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort()); }
    quint16 port() const { return server_.serverPort(); }
    QList<Request> requests;

private:
    Handler handler_;
    QTcpServer server_;
};

const QByteArray kList = "!!Order,1,1\n#\n# Super Check Partial\nK1ABC\nW2XY\nN5JJ\n";

QByteArray listing(qint64 size) {
    return QStringLiteral(R"([{"name":"MASTER.DTA","size":10,"etag":"a"},{"name":"MASTER.SCP","size":%1,"etag":"b","modified":"2026-11-18T00:01:32Z"}])")
        .arg(size)
        .toUtf8();
}

QByteArray answer(const QByteArray& status, const QByteArray& body, QList<QByteArray> headers = {}, bool length = true) {
    QByteArray out = "HTTP/1.1 " + status + "\r\nConnection: close\r\n";
    if (length) out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (const QByteArray& h : headers) out += h + "\r\n";
    return out + "\r\n" + body;
}

struct Outcome {
    bool done = false;
    bool failed = false;
    bool fresh = false;
    std::size_t calls = 0;
    QString message;
};

KnownCallDownloader::Settings local(const Server& server) {
    KnownCallDownloader::Settings s;
    s.files_url = QUrl(server.base() + QStringLiteral("/api/v1/files"));
    s.download_base = server.base() + QStringLiteral("/downloads/");
    return s;
}

Outcome run(KnownCallDownloader& d) {
    Outcome out;
    QEventLoop loop;
    QObject::connect(&d, &KnownCallDownloader::done, &loop, [&](std::shared_ptr<const kz4ap::callsign::KnownCalls> list, bool fresh, qint64) {
        out.done = true;
        out.fresh = fresh;
        out.calls = list->size();
        loop.quit();
    });
    QObject::connect(&d, &KnownCallDownloader::failed, &loop, [&](const QString& message) {
        out.failed = true;
        out.message = message;
        loop.quit();
    });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    d.start();
    loop.exec();
    QObject::disconnect(&d, nullptr, &loop, nullptr);
    return out;
}

QByteArray contents(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void write(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

}  // namespace

TEST(KnownCallDownloader, DownloadsKeepsAndSendsOnlyItsOwnHeaders) {
    Server server([](const Request& r) {
        if (r.path == "/api/v1/files") return answer("200 OK", listing(kList.size()));
        return answer("200 OK", kList, {"ETag: \"abc\""});
    });
    QTemporaryDir dir;
    KnownCallDownloader d(dir.path(), local(server));
    const Outcome o = run(d);
    ASSERT_TRUE(o.done) << o.message.toStdString();
    EXPECT_TRUE(o.fresh);
    EXPECT_EQ(o.calls, 3u);  // the !! directive and the comments are not calls
    EXPECT_EQ(contents(d.list_path()), kList);
    EXPECT_EQ(contents(d.etag_path()), "\"abc\"");
    ASSERT_EQ(server.requests.size(), 2);
    EXPECT_EQ(server.requests[1].path, "/downloads/MASTER.SCP");
    // Exactly these headers go out, and nothing of the user's: no language, no cookie.
    for (const Request& r : server.requests) {
        QSet<QByteArray> names;
        for (const auto& [n, v] : r.headers) names.insert(n);
        EXPECT_EQ(names, (QSet<QByteArray>{"host", "user-agent", "accept-language", "accept-encoding", "connection"}));
        EXPECT_TRUE(r.header("user-agent").startsWith("KZ4AP-Skimmer/"));
        EXPECT_EQ(r.header("accept-language"), "*");
        EXPECT_EQ(r.header("accept-encoding"), "identity");
    }
}

TEST(KnownCallDownloader, ASecondPressSendsTheEtagAndReadsTheCopyOn304) {
    Server server([](const Request& r) {
        if (r.path == "/api/v1/files") return answer("200 OK", listing(kList.size()));
        if (r.header("if-none-match") == "\"abc\"") return answer("304 Not Modified", {}, {"ETag: \"abc\""}, false);
        return answer("200 OK", kList, {"ETag: \"abc\""});
    });
    QTemporaryDir dir;
    KnownCallDownloader d(dir.path(), local(server));
    ASSERT_TRUE(run(d).done);
    const Outcome o = run(d);
    ASSERT_TRUE(o.done) << o.message.toStdString();
    EXPECT_FALSE(o.fresh);
    EXPECT_EQ(o.calls, 3u);
    ASSERT_EQ(server.requests.size(), 4);
    EXPECT_FALSE(server.requests[1].has("if-none-match"));
    EXPECT_EQ(server.requests[3].header("if-none-match"), "\"abc\"");
}

TEST(KnownCallDownloader, ADamagedCopyIsFetchedAgainWithoutItsEtagOnce) {
    Server server([](const Request& r) {
        if (r.path == "/api/v1/files") return answer("200 OK", listing(kList.size()));
        if (r.has("if-none-match")) return answer("304 Not Modified", {}, {}, false);
        return answer("200 OK", kList, {"ETag: \"new\""});
    });
    QTemporaryDir dir;
    KnownCallDownloader d(dir.path(), local(server));
    write(d.list_path(), QByteArray("\0\0garbage", 9));
    write(d.etag_path(), "\"old\"");
    const Outcome o = run(d);
    ASSERT_TRUE(o.done) << o.message.toStdString();
    EXPECT_TRUE(o.fresh);
    ASSERT_EQ(server.requests.size(), 3);
    EXPECT_EQ(server.requests[1].header("if-none-match"), "\"old\"");
    EXPECT_FALSE(server.requests[2].has("if-none-match"));
    EXPECT_EQ(contents(d.list_path()), kList);
    EXPECT_EQ(contents(d.etag_path()), "\"new\"");
}

TEST(KnownCallDownloader, AnAnswerCutShortIsNotKept) {
    // No Content-Length and the connection closed early: Qt reports no error; the listing's size tells.
    Server server([](const Request& r) {
        if (r.path == "/api/v1/files") return answer("200 OK", listing(kList.size()));
        return answer("200 OK", kList.left(30), {"ETag: \"abc\""}, false);
    });
    QTemporaryDir dir;
    KnownCallDownloader d(dir.path(), local(server));
    const Outcome o = run(d);
    ASSERT_TRUE(o.failed);
    EXPECT_TRUE(o.message.contains(QStringLiteral("cut short"))) << o.message.toStdString();
    EXPECT_FALSE(QFile::exists(d.list_path()));
    EXPECT_FALSE(QFile::exists(d.etag_path()));
}

TEST(KnownCallDownloader, AStallIsReportedAsAStall) {
    Server server([](const Request& r) {
        if (r.path == "/api/v1/files") return answer("200 OK", listing(kList.size()));
        return QByteArray();  // nothing, and the connection stays open
    });
    QTemporaryDir dir;
    KnownCallDownloader::Settings s = local(server);
    s.transfer_timeout_ms = 300;
    KnownCallDownloader d(dir.path(), s);
    const Outcome o = run(d);
    ASSERT_TRUE(o.failed);
    EXPECT_TRUE(o.message.contains(QStringLiteral("no data"))) << o.message.toStdString();
    EXPECT_FALSE(o.message.contains(QStringLiteral("bytes"))) << o.message.toStdString();
}

TEST(KnownCallDownloader, AnAnswerPastItsLimitIsStopped) {
    const QByteArray big = kList + QByteArray(5000, 'X');
    Server server([&](const Request& r) {
        if (r.path == "/api/v1/files") return answer("200 OK", listing(100));
        return answer("200 OK", big, {}, false);
    });
    QTemporaryDir dir;
    KnownCallDownloader::Settings s = local(server);
    s.max_list_bytes = 1000;
    KnownCallDownloader d(dir.path(), s);
    const Outcome o = run(d);
    ASSERT_TRUE(o.failed);
    EXPECT_TRUE(o.message.contains(QStringLiteral("more than 1000 bytes"))) << o.message.toStdString();
    EXPECT_FALSE(QFile::exists(d.list_path()));
    // The list of files has its own limit.
    s.max_list_bytes = kMaxListBytes;
    s.max_listing_bytes = 100;
    KnownCallDownloader small(dir.path(), s);
    const Outcome l = run(small);
    ASSERT_TRUE(l.failed);
    EXPECT_TRUE(l.message.contains(QStringLiteral("more than 100 bytes"))) << l.message.toStdString();
}

TEST(KnownCallDownloader, ARedirectOnlyToTheSameHost) {
    quint16 port = 0;
    Server server([&](const Request& r) {
        if (r.path == "/api/v1/files") return answer("200 OK", listing(kList.size()));
        if (r.path == "/downloads/MASTER.SCP") return answer("302 Found", {}, {"Location: /files/MASTER.SCP"});
        if (r.path == "/files/MASTER.SCP") return answer("200 OK", kList);
        return answer("404 Not Found", {});
    });
    QTemporaryDir dir;
    KnownCallDownloader same(dir.path(), local(server));
    const Outcome o = run(same);
    ASSERT_TRUE(o.done) << o.message.toStdString();

    port = server.port();
    Server elsewhere([&](const Request& r) {
        if (r.path == "/api/v1/files") return answer("200 OK", listing(kList.size()));
        // The same machine under another name is another host.
        return answer("302 Found", {}, {QStringLiteral("Location: http://localhost:%1/files/MASTER.SCP").arg(port).toUtf8()});
    });
    QTemporaryDir dir2;
    KnownCallDownloader d(dir2.path(), local(elsewhere));
    const Outcome e = run(d);
    ASSERT_TRUE(e.failed);
    EXPECT_TRUE(e.message.contains(QStringLiteral("another host"))) << e.message.toStdString();
    EXPECT_EQ(elsewhere.requests.size(), 2);
}

TEST(KnownCallDownloader, ReadsAListFromAPathOutsideTheCodePage) {
    QTemporaryDir dir;
    const QString folder = dir.path() + QStringLiteral("/Prüfung Жук");
    ASSERT_TRUE(QDir().mkpath(folder));
    const QString path = folder + QStringLiteral("/ÄÖ.scp");
    write(path, kList);
    const auto list = read_known_calls(path);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->size(), 3u);
    EXPECT_THROW(read_known_calls(folder + QStringLiteral("/none.scp")), std::runtime_error);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
