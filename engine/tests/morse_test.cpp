#include "kz4ap/morse.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace kz4ap;

TEST(Morse, EncodesLettersCaseInsensitively) {
    EXPECT_EQ(morse::encode('A'), ".-");
    EXPECT_EQ(morse::encode('a'), ".-");
    EXPECT_EQ(morse::encode('K'), "-.-");
}

TEST(Morse, EncodesDigitsAndPunctuation) {
    EXPECT_EQ(morse::encode('5'), ".....");
    EXPECT_EQ(morse::encode('0'), "-----");
    EXPECT_EQ(morse::encode('/'), "-..-.");
    EXPECT_EQ(morse::encode('?'), "..--..");
}

TEST(Morse, CharacterWithoutCodeIsEmpty) {
    EXPECT_TRUE(morse::encode('#').empty());
    EXPECT_TRUE(morse::encode(' ').empty());
}

TEST(Morse, DecodesPatterns) {
    EXPECT_EQ(morse::decode("-.-"), 'K');
    EXPECT_EQ(morse::decode("...--"), '3');
    EXPECT_EQ(morse::decode("........"), '\0');
    EXPECT_EQ(morse::decode(""), '\0');
}

TEST(Morse, EveryCodeRoundTrips) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?.,=+-(";
    for (char c : chars) {
        const auto pattern = morse::encode(c);
        ASSERT_FALSE(pattern.empty()) << c;
        EXPECT_EQ(morse::decode(pattern), c) << c;
    }
}
