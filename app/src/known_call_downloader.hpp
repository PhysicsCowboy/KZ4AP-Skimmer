// The Download MASTER.SCP button's work, with Qt's network: supercheckpartial.com's list of files, then the file, kept in a folder with
// its ETag (known_call_download.hpp has the addresses and the site's rules). One download at a time; the app makes one of these on the
// first press, so nothing reaches the network before. docs/signal-processing.md, A.12, "The app's modes and list", has the exact form.
#pragma once

#include "known_call_download.hpp"

#include "kz4ap/callsign.hpp"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace kz4ap::app {

// A known-call list read from a file, any path the system takes (through memory: KnownCalls::read_file takes a narrow string, which
// fails on a path outside the system's code page). Throws std::runtime_error naming the file when it cannot be read or is refused.
std::shared_ptr<const callsign::KnownCalls> read_known_calls(const QString& path);

// The headers every request carries, in full: Qt adds Host, Connection and, for a conditional request, If-None-Match, and nothing else
// is sent (no cookie, and Accept-Language set so that Qt does not send the system's languages).
QList<QPair<QByteArray, QByteArray>> request_headers();

class KnownCallDownloader : public QObject {
    Q_OBJECT

public:
    struct Settings {
        QUrl files_url = QUrl(QString::fromUtf8(kScpFilesUrl.data(), static_cast<qsizetype>(kScpFilesUrl.size())));
        QString download_base = QString::fromUtf8(kScpDownloadBase.data(), static_cast<qsizetype>(kScpDownloadBase.size()));
        QString file_name = QString::fromUtf8(kScpFileName.data(), static_cast<qsizetype>(kScpFileName.size()));
        std::int64_t max_list_bytes = kMaxListBytes;        // bytes
        std::int64_t max_listing_bytes = kMaxListingBytes;  // bytes
        int transfer_timeout_ms = kTransferTimeoutMs;       // ms
    };

    // folder: where the file and its ETag are kept (the app: its local data folder).
    explicit KnownCallDownloader(QString folder, QObject* parent = nullptr);
    KnownCallDownloader(QString folder, Settings settings, QObject* parent = nullptr);

    // Asks for the list, unless a download is under way. Ends with exactly one of done or failed.
    void start();
    bool busy() const { return busy_; }
    QString list_path() const;
    QString etag_path() const;

signals:
    // What it is doing now, for the controls' status.
    void progress(const QString& text);
    // The list, read and kept at list_path(); fresh: downloaded now (else the copy kept, unchanged on the server). bytes: the file's.
    void done(std::shared_ptr<const kz4ap::callsign::KnownCalls> list, bool fresh, qint64 bytes);
    // Why it stopped; the copy kept, if any, is as it was.
    void failed(const QString& message);

private:
    enum class Stop { None, TooLarge, OtherHost };
    QNetworkReply* get(const QNetworkRequest& request, std::int64_t limit_bytes);
    void ask_for_file(bool conditional);
    void listing_received(QNetworkReply* reply);
    void file_received(QNetworkReply* reply);
    QString reply_error(QNetworkReply* reply, std::int64_t limit_bytes) const;
    void fail(const QString& message);

    QString folder_;
    Settings settings_;
    QNetworkAccessManager* network_ = nullptr;
    bool busy_ = false;
    ListedFile listed_;
    bool retried_ = false;  // this download has already asked again without the ETag (a damaged copy kept)
};

}  // namespace kz4ap::app
