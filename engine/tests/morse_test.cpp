#include "kz4ap/morse.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string_view>
#include <vector>

using namespace kz4ap;

TEST(Morse, EncodesLettersCaseInsensitively) {
    EXPECT_EQ(morse::encode("A"), ".-");
    EXPECT_EQ(morse::encode("a"), ".-");
    EXPECT_EQ(morse::encode("K"), "-.-");
}

TEST(Morse, EncodesDigitsAndPunctuation) {
    EXPECT_EQ(morse::encode("5"), ".....");
    EXPECT_EQ(morse::encode("0"), "-----");
    EXPECT_EQ(morse::encode("/"), "-..-.");
    EXPECT_EQ(morse::encode("?"), "..--..");
    EXPECT_EQ(morse::encode("!"), "-.-.--");
    EXPECT_EQ(morse::encode("\""), ".-..-.");
    EXPECT_EQ(morse::encode(")"), "-.--.-");
    EXPECT_EQ(morse::encode("_"), "..--.-");
    EXPECT_EQ(morse::encode("$"), "...-..-");
    EXPECT_EQ(morse::encode("@"), ".--.-.");
}

TEST(Morse, EncodesProsignTokens) {
    EXPECT_EQ(morse::encode("<SK>"), "...-.-");
    EXPECT_EQ(morse::encode("<sk>"), "...-.-");
    EXPECT_EQ(morse::encode("<KN>"), "-.--.");
    EXPECT_EQ(morse::encode("<SOS>"), "...---...");
    EXPECT_EQ(morse::encode("<CL>"), "-.-..-..");
    EXPECT_EQ(morse::encode("<HH>"), "........");
}

TEST(Morse, NoCodeForUnknownOrExcludedSymbols) {
    for (std::string_view s : {"#", " ", "", "(", "=", "+", "&", "<XX>", "<SK", "AB"}) {
        EXPECT_TRUE(morse::encode(s).empty()) << s;
    }
}

TEST(Morse, DecodesPatterns) {
    EXPECT_EQ(morse::decode("-.-"), "K");
    EXPECT_EQ(morse::decode("...--"), "3");
    EXPECT_EQ(morse::decode("-.--."), "<KN>");  // not '('
    EXPECT_EQ(morse::decode("-...-"), "<BT>");  // not '='
    EXPECT_EQ(morse::decode(".-.-."), "<AR>");  // not '+'
    EXPECT_EQ(morse::decode(".-..."), "<AS>");  // not '&'
    EXPECT_TRUE(morse::decode("..--").empty());  // U-umlaut: non-English, excluded
    EXPECT_TRUE(morse::decode("").empty());
}

TEST(Morse, EightOrMoreDitsIsTheErrorSignal) {
    EXPECT_EQ(morse::decode("........"), "<HH>");
    EXPECT_EQ(morse::decode(".........."), "<HH>");
    EXPECT_TRUE(morse::decode(".......").empty());  // seven dits is not a code
}

TEST(Morse, EveryCodeRoundTripsAndIsUnique) {
    const std::vector<std::string_view> all = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S",
        "T", "U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        ".", ",", ";", ":", "?", "!", "'", "\"", ")", "/", "-", "$", "@", "_",
        "<AA>", "<AR>", "<AS>", "<BK>", "<BT>", "<CL>", "<HH>", "<KA>", "<KN>", "<SK>", "<SN>", "<SOS>"};
    ASSERT_EQ(all.size(), 62u);
    std::set<std::string_view> patterns;
    for (auto s : all) {
        const auto pattern = morse::encode(s);
        ASSERT_FALSE(pattern.empty()) << s;
        EXPECT_EQ(morse::decode(pattern), s) << s;
        EXPECT_TRUE(patterns.insert(pattern).second) << "duplicate pattern for " << s;
    }
}

TEST(Morse, SplitsWordsIntoSymbols) {
    using V = std::vector<std::string_view>;
    EXPECT_EQ(morse::symbols("K1ABC<KN>"), (V{"K", "1", "A", "B", "C", "<KN>"}));
    EXPECT_EQ(morse::symbols("<SK>"), (V{"<SK>"}));
    EXPECT_EQ(morse::symbols("A<B"), (V{"A", "<", "B"}));  // unclosed token: plain characters
    EXPECT_TRUE(morse::symbols("").empty());
}
