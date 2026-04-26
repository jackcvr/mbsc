#pragma once

#include <string_view>

namespace actions {
constexpr std::string_view WB = "wb";  // write bits (coils) (rw)
constexpr std::string_view RB = "rb";  // read bits (coils) (rw)

constexpr std::string_view WR = "wr";  // write holding registers (rw)
constexpr std::string_view RR = "rr";  // read holding registers (rw)

constexpr std::string_view RIB = "rib";  // read input bits (discreate inputs) (ro)
constexpr std::string_view RIR = "rir";  // read input registers (ro)

constexpr std::string_view REQ = "req";  // raw ADU frame
}  // namespace actions

namespace error_types {
constexpr std::string_view VALUE = "value";
constexpr std::string_view MODBUS = "modbus";
constexpr std::string_view SYSTEM = "system";
}  // namespace error_types
