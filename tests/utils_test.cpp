#include "mbsc/utils.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

TEST(DeferTest, ExecutesOnScopeExit) {
  bool executed = false;
  {
    Defer d([&executed] { executed = true; });
    EXPECT_FALSE(executed);
  }
  EXPECT_TRUE(executed);
}

TEST(IsAnyTest, MatchesValuesCorrectly) {
  EXPECT_TRUE(IsAny(5, 1, 3, 5, 7));
  EXPECT_FALSE(IsAny(5, 1, 3, 7, 9));

  std::string_view target = "b";
  EXPECT_TRUE(IsAny(target, "a", "b", "c"));
  EXPECT_FALSE(IsAny(target, "x", "y", "z"));
}

TEST(ParseNumberTest, ParsesDecimals) {
  EXPECT_EQ(ParseNumber<std::uint32_t>("123"), 123);
  EXPECT_EQ(ParseNumber<std::uint8_t>("255"), 255);
}

TEST(ParseNumberTest, ParsesHexadecimal) {
  EXPECT_EQ(ParseNumber<std::uint32_t>("0x1A"), 26);
  EXPECT_EQ(ParseNumber<std::uint16_t>("0XFFFF"), 65535);
}

TEST(ParseNumberTest, ThrowsOnInvalidInput) {
  EXPECT_THROW(ParseNumber<std::uint32_t>(""), std::invalid_argument);
  EXPECT_THROW(ParseNumber<std::uint32_t>("-5"), std::invalid_argument);
  EXPECT_THROW(ParseNumber<std::uint32_t>("12a"), std::invalid_argument);
  EXPECT_THROW(ParseNumber<std::uint32_t>("0xGZ"), std::invalid_argument);
}

TEST(ReadNumberTest, ReadsFromStream) {
  std::istringstream iss("456");
  EXPECT_EQ(ReadNumber<std::uint32_t>(iss), 456);
}

TEST(ReadNumberTest, ReturnsDefaultWhenStreamEmpty) {
  std::istringstream iss("");
  EXPECT_EQ(ReadNumber<std::uint32_t>(iss, 99), 99);
}

TEST(ParsePayloadTest, ParsesBytes) {
  auto res = ParsePayload<std::uint8_t>("deadbeef");
  std::vector<std::uint8_t> expected{0xde, 0xad, 0xbe, 0xef};
  EXPECT_EQ(res, expected);
}

TEST(ParsePayloadTest, ParsesWords) {
  auto res = ParsePayload<std::uint16_t>("deadbeef");
  std::vector<std::uint16_t> expected{0xdead, 0xbeef};
  EXPECT_EQ(res, expected);
}

TEST(ParsePayloadTest, ThrowsOnInvalidLengthOrFormat) {
  // Odd length for 8-bit parsing
  EXPECT_THROW(ParsePayload<std::uint8_t>("123"), std::invalid_argument);

  // Unaligned length for 16-bit parsing
  EXPECT_THROW(ParsePayload<std::uint16_t>("deadbee"), std::invalid_argument);

  // Invalid hex characters
  EXPECT_THROW(ParsePayload<std::uint8_t>("deagbeef"), std::invalid_argument);
}

TEST(ReadPayloadTest, ReadsPayloadFromStream) {
  std::istringstream iss("aabbcc");
  auto res = ReadPayload<std::uint8_t>(iss);
  std::vector<std::uint8_t> expected{0xaa, 0xbb, 0xcc};
  EXPECT_EQ(res, expected);
}

TEST(ReadPayloadTest, ThrowsOnMissingPayload) {
  std::istringstream iss("");
  EXPECT_THROW(ReadPayload<std::uint8_t>(iss), std::invalid_argument);
}

TEST(FormatPayloadTest, FormatsBytes) {
  std::vector<std::uint8_t> payload{0x01, 0xab, 0xff};
  EXPECT_EQ(FormatPayload(payload), "01abff");
}

TEST(FormatPayloadTest, FormatsWords) {
  std::vector<std::uint16_t> payload{0x0001, 0xabcd};
  EXPECT_EQ(FormatPayload(payload), "0001abcd");
}

TEST(FormatPayloadTest, FormatsWithLimit) {
  std::vector<std::uint8_t> payload{0xaa, 0xbb, 0xcc, 0xdd};
  EXPECT_EQ(FormatPayload(payload, 2), "aabb");
}
