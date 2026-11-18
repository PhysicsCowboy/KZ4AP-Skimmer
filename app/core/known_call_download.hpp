// The known-call list's download, without Qt: where MASTER.SCP is asked for and how the answer is read. The app asks only when the
// operator presses its button (the owner's ruling, 2026-11-18: the program never ships a list and reaches the network only then).
// Super Check Partial's guide for developers (supercheckpartial.com, "Developers", read 2026-11-18): find the files through
// /api/v1/files rather than a fixed address, download one from /downloads/<name>, send the ETag back (If-None-Match) so that an
// unchanged file is not sent again (304), and name the software and its version in the User-Agent.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kz4ap::app {

inline constexpr std::string_view kScpFilesUrl = "https://supercheckpartial.com/api/v1/files";
inline constexpr std::string_view kScpDownloadBase = "https://supercheckpartial.com/downloads/";
inline constexpr std::string_view kScpFileName = "MASTER.SCP";
// The program and its version, nothing of the operator's (no name, call, address or system).
std::string user_agent();
// bytes: a larger answer is refused, as no list of calls is that large (MASTER.SCP was 359 821 bytes on 2026-11-18). Heuristic, a
// guard against an answer without end.
inline constexpr std::int64_t kMaxListBytes = 16 * 1024 * 1024;
// bytes: the list of files likewise (2 166 bytes on 2026-11-18). Heuristic.
inline constexpr std::int64_t kMaxListingBytes = 1024 * 1024;
// ms: a request that moves no data for this long fails. Heuristic.
inline constexpr int kTransferTimeoutMs = 30000;

// A file in the listing.
struct ListedFile {
    std::string name;
    std::int64_t size_bytes = -1;  // bytes; -1 when the listing does not say
    std::string etag;
    std::string modified;  // as the listing gives it (ISO 8601, UTC)
};

// The entry called `name` (case ignored) in /api/v1/files' answer, a JSON array of objects with name, size, etag and modified; none if
// it is not there. Throws std::runtime_error when the answer is not such an array.
std::optional<ListedFile> find_listed_file(std::string_view listing_json, std::string_view name);
// The address to download a listed file from: base, then the name. Throws std::runtime_error for a name that is not a plain file name
// (letters, digits, dot, hyphen and underscore), so that a listing cannot name a path or a query; the request then goes to base's host,
// and a redirect only to that host (KnownCallDownloader).
std::string download_url(const ListedFile& file, std::string_view base = kScpDownloadBase);

}  // namespace kz4ap::app
