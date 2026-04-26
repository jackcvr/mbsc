#pragma once

#include <charconv>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

template <std::invocable F>
class Defer {
 public:
  explicit Defer(F&& f) noexcept : f_(std::move(f)) {}
  ~Defer() noexcept { f_(); }
  Defer(const Defer&) = delete;
  Defer& operator=(const Defer&) = delete;
  Defer(Defer&&) = delete;
  Defer& operator=(Defer&&) = delete;

 protected:
  F f_;
};

template <typename T, typename... Args>
constexpr bool is_any(const T& val, const Args&... args) noexcept {
  return ((val == args) || ...);
}

inline int get_response_len(std::span<const std::uint8_t> pdu) {
  if (pdu.empty()) return 0;
  std::uint8_t fc = pdu[0];
  switch (fc) {
    case 0x01:
    case 0x02: {
      if (pdu.size() < 5) return 0;
      std::uint16_t qty = pdu[3] << 8 | pdu[4];
      return 2 + (qty + 7) / 8;
    }
    case 0x03:
    case 0x04: {
      if (pdu.size() < 5) return 0;
      std::uint16_t qty = pdu[3] << 8 | pdu[4];
      return 2 + qty * 2;
    }
    case 0x05:
    case 0x06:
    case 0x0F:
    case 0x10:
      return 5;
    default:
      // Max PDU size minus Slave address and CRC
      return 253;
  }
}

template <typename T>
constexpr T parse_number(std::string_view number) {
  static_assert(std::is_unsigned_v<T>, "parse_number only supports unsigned integral types");
  if (number.empty()) throw std::invalid_argument("empty word");
  if (number.starts_with('-')) throw std::invalid_argument("negative numbers are not allowed");

  int base = 10;
  std::string_view target = number;

  if (target.starts_with("0x") || target.starts_with("0X")) {
    target.remove_prefix(2);
    base = 16;
  }

  T res{};
  auto [ptr, ec] = std::from_chars(target.data(), target.data() + target.size(), res, base);

  if (ec != std::errc{}) {
    throw std::invalid_argument("invalid numerical value");
  }
  if (ptr != target.data() + target.size()) {
    throw std::invalid_argument("trailing characters found");
  }
  return res;
}

template <typename T>
T read_number(std::istringstream& iss, const T _default = 1) {
  std::string _count;
  T count = _default;
  if (iss >> _count) {
    count = parse_number<T>(_count);
  }
  return count;
}

template <typename T>
std::vector<T> parse_payload(std::string_view payload) {
  static_assert(std::is_integral_v<T>, "T must be an integral type");
  constexpr std::size_t value_len = sizeof(T) * 2;
  if (payload.length() % value_len != 0) {
    throw std::invalid_argument("hex string length does not align with the requested type size");
  }

  std::vector<T> values;
  values.reserve(payload.length() / value_len);

  for (std::size_t i = 0; i < payload.length(); i += value_len) {
    T val;
    const char* chunk_end = payload.data() + i + value_len;
    auto [ptr, ec] = std::from_chars(payload.data() + i, chunk_end, val, 16);
    if (ec != std::errc() || ptr != chunk_end) {
      throw std::invalid_argument("invalid hex character in payload");
    }
    values.push_back(val);
  }
  return values;
}

template <typename T>
std::vector<T> read_payload(std::istringstream& iss) {
  std::string s;
  if (!(iss >> s)) throw std::invalid_argument("missing payload");
  auto payload = parse_payload<T>(s);
  if (payload.empty()) throw std::invalid_argument("bad payload");
  return payload;
}

template <typename T>
std::string format_payload(const T& payload, int len = -1) {
  using ValueType = std::remove_cvref_t<decltype(payload[0])>;
  constexpr std::size_t chars_count = sizeof(ValueType) * 2;
  std::size_t actual_len = (len == -1) ? std::size(payload) : static_cast<std::size_t>(len);
  std::string s;

  s.reserve(actual_len * chars_count);
  auto out = std::back_inserter(s);
  for (std::size_t i = 0; i < actual_len; ++i) {
    std::format_to(out, "{:0{}x}", payload[i], chars_count);
  }
  return s;
}
