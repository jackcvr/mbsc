#pragma once

#include <concepts>
#include <cstddef>
#include <iterator>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
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
constexpr bool IsAny(const T& val, const Args&... args) noexcept {
  return ((val == args) || ...);
}

template <typename T>
constexpr T ParseNumber(std::string_view number) {
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
T ReadNumber(std::istringstream& iss, const T _default = 1) {
  std::string _count;
  T count = _default;
  if (iss >> _count) {
    count = ParseNumber<T>(_count);
  }
  return count;
}

template <typename T>
std::vector<T> ParsePayload(std::string_view payload) {
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
std::vector<T> ReadPayload(std::istringstream& iss) {
  std::string s;
  if (!(iss >> s)) throw std::invalid_argument("missing payload");
  auto payload = ParsePayload<T>(s);
  if (payload.empty()) throw std::invalid_argument("bad payload");
  return payload;
}

template <typename T>
std::string FormatPayload(const T& payload, int len = -1) {
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
