#include "known_call_downloader.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace kz4ap::app {

namespace {

// A reply's property: the reply was stopped here because its answer grew past its limit (Qt reports such an abort as it reports a
// transfer timeout, OperationCanceledError, so the reason is kept apart).
constexpr const char* kTooLarge = "kz4ap_too_large";

std::string native(const QString& path) { return QDir::toNativeSeparators(path).toStdString(); }

}  // namespace

std::shared_ptr<const callsign::KnownCalls> read_known_calls(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("cannot read the known-call list " + native(path) + ": " + file.errorString().toStdString());
    std::istringstream in(file.readAll().toStdString());
    try {
        return std::make_shared<const callsign::KnownCalls>(callsign::KnownCalls::read(in));
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string(e.what()) + ": " + native(path));
    }
}

QList<QPair<QByteArray, QByteArray>> request_headers() {
    return {
        {"User-Agent", QByteArray::fromStdString(user_agent())},
        // Qt would otherwise send the system's languages.
        {"Accept-Language", "*"},
        // The file as it is, so that Content-Length is the file's size (Qt would otherwise ask for gzip and inflate it).
        {"Accept-Encoding", "identity"},
    };
}

KnownCallDownloader::KnownCallDownloader(QString folder, QObject* parent) : KnownCallDownloader(std::move(folder), Settings{}, parent) {}

KnownCallDownloader::KnownCallDownloader(QString folder, Settings settings, QObject* parent)
    : QObject(parent), folder_(std::move(folder)), settings_(std::move(settings)) {}

QString KnownCallDownloader::list_path() const { return folder_ + QLatin1Char('/') + settings_.file_name; }
QString KnownCallDownloader::etag_path() const { return list_path() + QStringLiteral(".etag"); }

void KnownCallDownloader::start() {
    if (busy_) return;
    if (network_ == nullptr) network_ = new QNetworkAccessManager(this);
    busy_ = true;
    retried_ = false;
    emit progress(tr("asking %1 for its files...").arg(settings_.files_url.host()));
    QNetworkRequest request(settings_.files_url);
    QNetworkReply* reply = get(request, settings_.max_listing_bytes);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { listing_received(reply); });
}

QNetworkReply* KnownCallDownloader::get(const QNetworkRequest& asked, std::int64_t limit_bytes) {
    QNetworkRequest request(asked);
    for (const auto& [name, value] : request_headers()) request.setRawHeader(name, value);
    request.setTransferTimeout(settings_.transfer_timeout_ms);
    // A redirect only to the same scheme, host and port: a listing or a redirect cannot send the request elsewhere.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    // No cookie is sent or kept.
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    QNetworkReply* reply = network_->get(request);
    // The answer is held in memory as it comes: past the limit it is stopped, before it is all there.
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit_bytes](qint64 received, qint64 total) {
        if (received > limit_bytes || total > limit_bytes) {
            reply->setProperty(kTooLarge, true);
            reply->abort();
        }
    });
    return reply;
}

QString KnownCallDownloader::reply_error(QNetworkReply* reply, std::int64_t limit_bytes) const {
    if (reply->property(kTooLarge).toBool()) return tr("the answer is more than %1 bytes: stopped").arg(limit_bytes);
    switch (reply->error()) {
        case QNetworkReply::OperationCanceledError:  // setTransferTimeout's abort
        case QNetworkReply::TimeoutError: return tr("no data for %1 s: stopped").arg(settings_.transfer_timeout_ms / 1000.0);
        case QNetworkReply::InsecureRedirectError: return tr("redirected to another host or scheme: refused");
        default: break;
    }
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    return status.isValid() ? tr("%1 (HTTP %2)").arg(reply->errorString()).arg(status.toInt()) : reply->errorString();
}

void KnownCallDownloader::fail(const QString& message) {
    busy_ = false;
    emit failed(message);
}

void KnownCallDownloader::listing_received(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return fail(reply_error(reply, settings_.max_listing_bytes));
    // An answer that came whole in one piece passes the check in progress: it is checked again here.
    if (reply->bytesAvailable() > settings_.max_listing_bytes) return fail(tr("the answer is more than %1 bytes: stopped").arg(settings_.max_listing_bytes));
    const QByteArray body = reply->readAll();
    try {
        const std::optional<ListedFile> file =
            find_listed_file(std::string_view(body.constData(), static_cast<std::size_t>(body.size())), settings_.file_name.toStdString());
        if (!file) return fail(tr("%1 lists no %2").arg(settings_.files_url.host(), settings_.file_name));
        if (file->size_bytes > settings_.max_list_bytes)
            return fail(tr("the file is %1 bytes, more than %2 bytes").arg(file->size_bytes).arg(settings_.max_list_bytes));
        listed_ = *file;
        ask_for_file(true);
    } catch (const std::exception& e) {
        fail(QString::fromStdString(e.what()));
    }
}

void KnownCallDownloader::ask_for_file(bool conditional) {
    QNetworkRequest request;
    try {
        request.setUrl(QUrl(QString::fromStdString(download_url(listed_, settings_.download_base.toStdString()))));
    } catch (const std::exception& e) {
        return fail(QString::fromStdString(e.what()));
    }
    // The copy kept from the last download is sent back as its ETag: unchanged, the server says so (304) and sends nothing.
    QFile etag(etag_path());
    if (conditional && QFileInfo::exists(list_path()) && etag.open(QIODevice::ReadOnly)) request.setRawHeader("If-None-Match", etag.readAll().trimmed());
    emit progress(tr("downloading %1...").arg(QString::fromStdString(listed_.name)));
    QNetworkReply* reply = get(request, settings_.max_list_bytes);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { file_received(reply); });
}

void KnownCallDownloader::file_received(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return fail(reply_error(reply, settings_.max_list_bytes));
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 304) {
        try {
            std::shared_ptr<const callsign::KnownCalls> list = read_known_calls(list_path());
            busy_ = false;
            emit done(list, false, QFileInfo(list_path()).size());
        } catch (const std::exception& e) {
            if (retried_) return fail(QString::fromStdString(e.what()));
            // The copy kept no longer reads, and its ETag would bring a 304 every time: ask once more for the whole file.
            retried_ = true;
            QFile::remove(etag_path());
            emit progress(tr("the copy kept does not read: downloading it again..."));
            ask_for_file(false);
        }
        return;
    }
    if (status != 200) return fail(tr("HTTP %1").arg(status));
    if (reply->bytesAvailable() > settings_.max_list_bytes) return fail(tr("the answer is more than %1 bytes: stopped").arg(settings_.max_list_bytes));
    const QByteArray body = reply->readAll();
    // An answer cut short by a closed connection ends with no error: its size is checked against what the server said, or failing that
    // against the listing.
    const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
    const qint64 expected = length.isValid() ? length.toLongLong() : listed_.size_bytes;
    if (expected >= 0 && body.size() < expected) return fail(tr("the answer was cut short: %1 of %2 bytes").arg(body.size()).arg(expected));
    if (expected >= 0 && body.size() > expected) return fail(tr("the answer is longer than announced: %1, not %2 bytes").arg(body.size()).arg(expected));
    // Read before it is kept: a page of HTML or a file of no calls leaves the copy kept as it was.
    std::shared_ptr<const callsign::KnownCalls> list;
    try {
        std::istringstream in(body.toStdString());
        list = std::make_shared<const callsign::KnownCalls>(callsign::KnownCalls::read(in));
    } catch (const std::exception& e) {
        return fail(QString::fromStdString(e.what()));
    }
    if (!QDir().mkpath(folder_)) return fail(tr("cannot make the folder %1").arg(QDir::toNativeSeparators(folder_)));
    QSaveFile save(list_path());
    if (!save.open(QIODevice::WriteOnly) || save.write(body) != body.size() || !save.commit())
        return fail(tr("cannot save %1: %2").arg(QDir::toNativeSeparators(list_path()), save.errorString()));
    const QByteArray tag = reply->rawHeader("ETag");
    QSaveFile etag(etag_path());
    if (tag.isEmpty() || !etag.open(QIODevice::WriteOnly) || etag.write(tag) != tag.size() || !etag.commit()) QFile::remove(etag_path());
    busy_ = false;
    emit done(list, true, body.size());
}

}  // namespace kz4ap::app
