#include "mbsc/utils.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <vector>

TEST(UtilsTest, DeferExecutesOnDestruction) {
  bool executed = false;
  {
    Defer d([&] { executed = true; });
    EXPECT_FALSE(executed);
  }
  EXPECT_TRUE(executed);
}

TEST(UtilsTest, IsAnyMatchesCorrectly) {
  EXPECT_TRUE(is_any(5, 1, 2, 3, 4, 5));
  EXPECT_TRUE(is_any(1, 1, 2, 3));
  EXPECT_FALSE(is_any(9, 1, 2, 3));
}

TEST(UtilsTest, GetRequestLen) {
  // Too short
  EXPECT_EQ(get_request_len(std::vector<uint8_t>{}), -1);
  EXPECT_EQ(get_request_len(std::vector<uint8_t>{0x01}), -1);

  // FC 1-6 (Length 8)
  EXPECT_EQ(get_request_len(std::vector<uint8_t>{0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}), 8);

  // FC 15-16 (Dynamic length based on index 6)
  EXPECT_EQ(get_request_len(std::vector<uint8_t>{0x01, 0x0F, 0x00, 0x00, 0x00, 0x0A, 0x02, 0xFF, 0xFF}), 11);
  EXPECT_EQ(get_request_len(std::vector<uint8_t>{0x01, 0x10, 0x00, 0x00, 0x00, 0x0A}), -1);  // Missing index 6

  // FC 22 (Length 10)
  EXPECT_EQ(get_request_len(std::vector<uint8_t>{0x01, 0x16, 0, 0, 0, 0, 0, 0, 0, 0}), 10);

  // FC 23 (Dynamic length based on index 10)
  EXPECT_EQ(get_request_len(std::vector<uint8_t>{0x01, 0x17, 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 1, 2, 3, 4}), 15);
  EXPECT_EQ(get_request_len(std::vector<uint8_t>{0x01, 0x17, 0, 0, 0, 0, 0, 0, 0, 0}), -1);  // Missing index 10

  // Unknown FC
  EXPECT_EQ(get_request_len(std::vector<uint8_t>{0x01, 0x99, 0x00, 0x00}), -1);
}

TEST(UtilsTest, GetResponseLen) {
  EXPECT_EQ(get_response_len(std::vector<uint8_t>{}), 0);

  // FC 1, 2 (Qty = 10 bits -> 2 bytes data -> total 4 bytes)
  EXPECT_EQ(get_response_len(std::vector<uint8_t>{0x01, 0x00, 0x00, 0x00, 0x0A}), 4);
  EXPECT_EQ(get_response_len(std::vector<uint8_t>{0x01, 0x00, 0x00}), 0);  // Too short

  // FC 3, 4 (Qty = 5 regs -> 10 bytes data -> total 12 bytes)
  EXPECT_EQ(get_response_len(std::vector<uint8_t>{0x03, 0x00, 0x00, 0x00, 0x05}), 12);

  // FC 5, 6, 15, 16 (Always 5)
  EXPECT_EQ(get_response_len(std::vector<uint8_t>{0x05}), 5);
  EXPECT_EQ(get_response_len(std::vector<uint8_t>{0x10}), 5);

  // Default (253)
  EXPECT_EQ(get_response_len(std::vector<uint8_t>{0x99}), 253);
}

TEST(UtilsTest, ParseNumber) {
  EXPECT_EQ(parse_number<uint32_t>("123"), 123);
  EXPECT_EQ(parse_number<uint16_t>("0x1A"), 26);
  EXPECT_EQ(parse_number<uint16_t>("0XFF"), 255);

  // Explicitly test the new throw on negative numbers and unsigned enforcement
  EXPECT_THROW(parse_number<uint32_t>("-456"), std::invalid_argument);

  EXPECT_THROW(parse_number<uint32_t>(""), std::invalid_argument);
  EXPECT_THROW(parse_number<uint32_t>("abc"), std::invalid_argument);
  EXPECT_THROW(parse_number<uint32_t>("123junk"), std::invalid_argument);
}

TEST(UtilsTest, ReadNumber) {
  std::istringstream iss_valid("42");
  EXPECT_EQ(read_number<uint32_t>(iss_valid, 10), 42);

  std::istringstream iss_hex("0xFF");
  EXPECT_EQ(read_number<uint32_t>(iss_hex, 10), 255);

  std::istringstream iss_empty("");
  EXPECT_EQ(read_number<uint32_t>(iss_empty, 99), 99);

  std::istringstream iss_invalid("invalid");
  EXPECT_THROW(read_number<uint32_t>(iss_invalid), std::invalid_argument);
}

TEST(UtilsTest, ParsePayload) {
  auto v8 = parse_payload<uint8_t>("0A1B2C");
  EXPECT_EQ(v8, (std::vector<uint8_t>{0x0A, 0x1B, 0x2C}));

  auto v16 = parse_payload<uint16_t>("0A1B2C3D");
  EXPECT_EQ(v16, (std::vector<uint16_t>{0x0A1B, 0x2C3D}));

  // Bad alignment
  EXPECT_THROW(parse_payload<uint8_t>("0A1"), std::invalid_argument);
  EXPECT_THROW(parse_payload<uint16_t>("0A1B2"), std::invalid_argument);

  // Invalid hex character (testing the exact chunk end check)
  EXPECT_THROW(parse_payload<uint8_t>("0X1Y"), std::invalid_argument);
}

TEST(UtilsTest, ReadPayload) {
  std::istringstream iss("AABBCC");
  auto v = read_payload<uint8_t>(iss);
  EXPECT_EQ(v, (std::vector<uint8_t>{0xAA, 0xBB, 0xCC}));

  std::istringstream empty_iss("");
  EXPECT_THROW(read_payload<uint8_t>(empty_iss), std::invalid_argument);
}

TEST(UtilsTest, FormatPayload) {
  std::vector<uint8_t> v8{0x01, 0x0A, 0xFF};
  EXPECT_EQ(format_payload(v8), "010aff");
  EXPECT_EQ(format_payload(v8, 2), "010a");

  std::vector<uint16_t> v16{0x010A, 0xFFFF};
  EXPECT_EQ(format_payload(v16), "010affff");
}
