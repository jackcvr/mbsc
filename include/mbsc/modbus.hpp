#pragma once

#include <termios.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <mbsc/wrapper.hpp>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "wrapper.hpp"
#include "nanomodbus/nanomodbus.h"
#include "serial/serial.h"

class SerialError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class ModbusError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

template <>
struct ErrorPolicy<serial_t> {
  static void Check(int rc, serial_t* ctx) {
    if (rc < 0) throw SerialError(serial_errmsg(ctx));
  }
};

template <>
struct ErrorPolicy<nmbs_t> {
  static void Check(nmbs_error rc, nmbs_t* /*ctx*/) {
    if (rc != NMBS_ERROR_NONE) throw ModbusError(nmbs_strerror(rc));
  }
};

using Serial = Wrapper<serial_t>;
using Nmbs = Wrapper<nmbs_t>;

class Modbus {
 public:
  explicit Modbus(std::string_view device, int baud_rate, int data_bits, char parity, int stop_bits, bool xonxoff,
                  bool rtscts) {
    serial_ = serial_new();
    if (serial_ == nullptr) {
      throw std::runtime_error("Failed to allocate serial port.");
    }

    serial_parity_t parity_enum = (parity == 'E') ? PARITY_EVEN : (parity == 'O') ? PARITY_ODD : PARITY_NONE;

    try {
      Serial(*serial_).Call(serial_open_advanced, device.data(), static_cast<std::uint32_t>(baud_rate),
                                       static_cast<unsigned int>(data_bits), parity_enum,
                                       static_cast<unsigned int>(stop_bits), xonxoff, rtscts);

      nmbs_platform_conf platform_conf;
      nmbs_platform_conf_create(&platform_conf);
      platform_conf.transport = NMBS_TRANSPORT_RTU;
      platform_conf.read = ReadSerialCallback;
      platform_conf.write = WriteSerialCallback;
      platform_conf.arg = serial_;

      Nmbs(nmbs_).Call(nmbs_client_create, &platform_conf);

    } catch (...) {
      serial_close(serial_);
      serial_free(serial_);
      throw;
    }
  }

  ~Modbus() {
    if (serial_ != nullptr) {
      serial_close(serial_);
      serial_free(serial_);
    }
  }

  Modbus(const Modbus&) = delete;
  Modbus& operator=(const Modbus&) = delete;

  Modbus(Modbus&& other) noexcept : serial_(std::exchange(other.serial_, nullptr)), nmbs_(other.nmbs_) {}

  Modbus& operator=(Modbus&& other) noexcept {
    if (this != &other) {
      if (serial_ != nullptr) {
        serial_close(serial_);
        serial_free(serial_);
      }
      serial_ = std::exchange(other.serial_, nullptr);
      nmbs_ = other.nmbs_;
    }
    return *this;
  }

  template <typename Func, typename... Args>
  auto Call(Func f, Args&&... args) {
    return Nmbs(nmbs_).Call(f, std::forward<Args>(args)...);
  }

  template <typename Func, typename... Args>
  auto Invoke(Func f, Args&&... args) noexcept {
    return Nmbs(nmbs_).Invoke(f, std::forward<Args>(args)...);
  }

  void TcFlush() {
    if (tcflush(serial_fd(serial_), TCIFLUSH) != 0) {
      throw std::system_error(errno, std::generic_category(), "tcflush failed");
    }
  }

  int SendAdu(std::span<const std::uint8_t> frame) {
    auto sw = Serial(*serial_);
    return sw.Call(serial_write, frame.data(), static_cast<std::uint16_t>(frame.size()));
  }

  int RecvAdu(std::vector<std::uint8_t>& buf, std::vector<std::uint8_t>& adu, std::int32_t read_timeout_ms,
              std::int32_t byte_timeout_ms) {
    constexpr size_t kMaxAduSize = 260;

    adu.clear();
    std::array<std::uint8_t, kMaxAduSize> chunk{};
    auto sw = Serial(*serial_);

    while (true) {
      if (buf.size() >= 4) {
        for (size_t len = 4; len <= buf.size(); ++len) {
          std::uint16_t calc_crc = nmbs_crc_calc(buf.data(), len - 2, nullptr);

          // Reverted to your original logic
          std::uint16_t frame_crc = (static_cast<std::uint16_t>(buf[len - 2]) << 8) | buf[len - 1];

          if (calc_crc == frame_crc) {
            adu.assign(buf.begin(), buf.begin() + len);
            buf.erase(buf.begin(), buf.begin() + len);
            return static_cast<int>(adu.size());
          }
        }
      }

      int timeout = buf.empty() ? read_timeout_ms : byte_timeout_ms;
      int n = sw.Call(serial_read, chunk.data(), 1, timeout);

      if (n > 0) {
        buf.push_back(chunk[0]);
        int more = sw.Call(serial_read, chunk.data(), static_cast<std::uint16_t>(chunk.size()), byte_timeout_ms);
        if (more > 0) {
          buf.insert(buf.end(), chunk.begin(), chunk.begin() + more);
        }
      } else {
        break;
      }
    }

    if (!buf.empty()) {
      adu = std::move(buf);
      buf.clear();
      return -static_cast<int>(adu.size());
    }

    return 0;
  }

 protected:
  static std::int32_t ReadSerialCallback(std::uint8_t* buf, std::uint16_t count, std::int32_t byte_timeout_ms,
                                         void* arg) {
    serial_t* serial = static_cast<serial_t*>(arg);
    int result = serial_read(serial, buf, count, byte_timeout_ms);
    return (result < 0) ? -1 : result;
  }

  static std::int32_t WriteSerialCallback(const std::uint8_t* buf, std::uint16_t count,
                                          std::int32_t /*byte_timeout_ms*/, void* arg) {
    serial_t* serial = static_cast<serial_t*>(arg);
    int result = serial_write(serial, buf, count);
    return (result < 0) ? -1 : result;
  }

  serial_t* serial_{nullptr};
  nmbs_t nmbs_{};
};
