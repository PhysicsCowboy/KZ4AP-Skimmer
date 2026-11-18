#include "known_call_download.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

#ifndef KZ4AP_VERSION
#define KZ4AP_VERSION "0"
#endif

namespace kz4ap::app {

std::string user_agent() { return std::string("KZ4AP-Skimmer/") + KZ4AP_VERSION; }

namespace {

bool same_ignoring_case(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}

}  // namespace

std::optional<ListedFile> find_listed_file(std::string_view listing_json, std::string_view name) {
    const nlohmann::json listing = nlohmann::json::parse(listing_json, nullptr, false);
    if (listing.is_discarded() || !listing.is_array()) throw std::runtime_error("the server's list of files is not a JSON array");
    for (const nlohmann::json& entry : listing) {
        if (!entry.is_object()) continue;
        const auto n = entry.find("name");
        if (n == entry.end() || !n->is_string() || !same_ignoring_case(n->get<std::string>(), name)) continue;
        ListedFile out;
        out.name = n->get<std::string>();
        if (const auto s = entry.find("size"); s != entry.end() && s->is_number_integer()) out.size_bytes = s->get<std::int64_t>();
        if (const auto e = entry.find("etag"); e != entry.end() && e->is_string()) out.etag = e->get<std::string>();
        if (const auto m = entry.find("modified"); m != entry.end() && m->is_string()) out.modified = m->get<std::string>();
        return out;
    }
    return std::nullopt;
}

std::string download_url(const ListedFile& file) {
    const bool plain = !file.name.empty() && file.name.size() <= 64 && file.name.front() != '.' &&
                       std::all_of(file.name.begin(), file.name.end(), [](char c) {
                           return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
                       });
    if (!plain) throw std::runtime_error("the server's list of files gives a name that is not a plain file name: " + file.name);
    return std::string(kScpDownloadBase) + file.name;
}

}  // namespace kz4ap::app
