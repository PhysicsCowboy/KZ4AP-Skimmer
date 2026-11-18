#include "known_call_download.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace kz4ap::app;

TEST(KnownCallDownload, FindsTheFileInTheListing) {
    // The shape of supercheckpartial.com/api/v1/files' answer on 2026-11-18 (two of its fourteen entries).
    const char* listing =
        R"([{"name":"MASTER.DTA","size":1333775,"etag":"0c5b","modified":"2026-11-18T00:01:32.993871993Z"},)"
        R"({"name":"MASTER.SCP","size":359821,"etag":"e4ae","modified":"2026-11-18T00:01:32.961871910Z"}])";
    const auto file = find_listed_file(listing, "master.scp");
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->name, "MASTER.SCP");
    EXPECT_EQ(file->size_bytes, 359821);
    EXPECT_EQ(file->etag, "e4ae");
    EXPECT_EQ(file->modified, "2026-11-18T00:01:32.961871910Z");
    EXPECT_EQ(download_url(*file), "https://supercheckpartial.com/downloads/MASTER.SCP");
    EXPECT_FALSE(find_listed_file(listing, "MASTERDX.SCP").has_value());
}

TEST(KnownCallDownload, RefusesABadListingAndAStrangeName) {
    EXPECT_THROW(find_listed_file("<html>not json</html>", "MASTER.SCP"), std::runtime_error);
    EXPECT_THROW(find_listed_file(R"({"name":"MASTER.SCP"})", "MASTER.SCP"), std::runtime_error);
    EXPECT_FALSE(find_listed_file(R"([1, "x", {"size": 3}])", "MASTER.SCP").has_value());
    ListedFile bad;
    bad.name = "../../evil?x=1";
    EXPECT_THROW(download_url(bad), std::runtime_error);
    bad.name = "";
    EXPECT_THROW(download_url(bad), std::runtime_error);
}

TEST(KnownCallDownload, TheUserAgentNamesOnlyTheProgram) {
    const std::string agent = user_agent();
    EXPECT_EQ(agent.rfind("KZ4AP-Skimmer/", 0), 0u);
    EXPECT_EQ(agent.find('@'), std::string::npos);
    EXPECT_EQ(agent.find(' '), std::string::npos);
}
