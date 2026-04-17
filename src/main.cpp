// clang-format off
#include <cstdio>
#include <readline/history.h>
#include <readline/readline.h>
// clang-format on

#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "mbsc/constants.hpp"
#include "mbsc/utils.hpp"
#include "mbsc/wrapper.hpp"
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
    static void check(int rc, serial_t* ctx) {
        if (rc < 0) throw SerialError(serial_errmsg(ctx));
    }
};

template <>
struct ErrorPolicy<nmbs_t> {
    static void check(nmbs_error rc, nmbs_t* /*ctx*/) {
        if (rc != NMBS_ERROR_NONE) throw ModbusError(nmbs_strerror(rc));
    }
};

using Serial = Wrapper<serial_t>;
using Modbus = Wrapper<nmbs_t>;

struct Config {
    std::string device = "/dev/ttyUSB0";
    int baud_rate = 115200;
    char parity = 'N';
    int data_bits = 8;
    int stop_bits = 1;
    int read_timeout_ms = 1000;
};

struct AppContext {
    Config config;
    Serial* serial = nullptr;
    Modbus* modbus = nullptr;
    std::mutex mtx;
    std::atomic<bool> is_requesting{false};
    std::atomic<bool> is_running{true};
};

inline void print(std::string_view msg) { std::println("{}", msg); }

inline void print_error(std::string_view type, std::string_view msg) { std::println(stderr, "{}: {}", type, msg); }

std::int32_t read_serial(std::uint8_t* buf, std::uint16_t count, std::int32_t byte_timeout_ms, void* arg) {
    serial_t* serial = static_cast<serial_t*>(arg);
    int result = serial_read(serial, buf, count, byte_timeout_ms);
    if (result < 0) {
        return -1;
    }
    return result;
}

std::int32_t write_serial(const std::uint8_t* buf, std::uint16_t count, std::int32_t /*byte_timeout_ms*/, void* arg) {
    serial_t* serial = static_cast<serial_t*>(arg);
    int result = serial_write(serial, buf, count);
    if (result < 0) {
        return -1;
    }
    return result;
}

void serial_tcflush(Serial& serial) {
    if (tcflush(serial.invoke(serial_fd), TCIFLUSH) != 0) {
        throw std::system_error(errno, std::generic_category(), "tcflush failed");
    }
}

int send_adu(Serial& serial, std::span<const std::uint8_t> frame) {
    return serial.call(serial_write, frame.data(), frame.size());
}

int recv_adu(Serial& serial, std::vector<std::uint8_t>& buf, std::int32_t read_timeout_ms, std::int32_t byte_timeout_ms) {
    constexpr size_t MAX_ADU_SIZE = 260;

    buf.clear();
    buf.reserve(MAX_ADU_SIZE);
    std::array<uint8_t, MAX_ADU_SIZE> chunk{};

    int n = serial.call(serial_read, chunk.data(), 1, read_timeout_ms);
    if (n > 0) {
        buf.push_back(chunk[0]);
        while (true) {
            int more = serial.call(serial_read, chunk.data(), chunk.size(), byte_timeout_ms);
            if (more > 0) {
                buf.insert(buf.end(), chunk.begin(), chunk.begin() + more);
                if (buf.size() >= MAX_ADU_SIZE) break;
            } else {
                break;
            }
        }
    }
    return static_cast<int>(buf.size());
}

/*
 * output format:
 * - <payload>
 */
void modbus_listen(AppContext* ctx) {
    // 10ms is a safe user-space approximation for the Modbus t3.5 silence gap.
    constexpr int32_t INTER_BYTE_TIMEOUT_MS = 10;

    auto serial = ctx->serial;
    std::vector<std::uint8_t> buf;

    while (ctx->is_running.load(std::memory_order_relaxed)) {
        try {
            ctx->is_requesting.wait(true);
            std::lock_guard lock(ctx->mtx);
            if (ctx->is_requesting.load(std::memory_order_relaxed)) {
                continue;
            }
            int n = recv_adu(*serial, buf, 10, INTER_BYTE_TIMEOUT_MS);
            if (n > 0) {
                print(format_payload(buf));
            }
        } catch (const std::exception& e) {
            print_error(error_types::SYSTEM, e.what());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

/*
 * input formats:
 * - req <frame>
 * - <action(wb|wr)> <slave_id> <address> [<payload>]
 * - <action(rb|rr|rib|rir)> <slave_id> <address> [<count>]
 *
 * output formats (stdout):
 * - <payload>
 *
 * error format (stderr):
 * - <error_type>: <msg>
 */
void process_line(AppContext& ctx, const std::string& line) {
    if (line.empty()) return;

    std::istringstream iss(line);
    std::string action;

    if (!(iss >> action)) {
        throw std::invalid_argument("missing action");
    }

    auto config = ctx.config;
    auto* serial = ctx.serial;
    auto* modbus = ctx.modbus;

    ctx.is_requesting.store(true);
    ctx.is_requesting.notify_all();
    std::lock_guard lock(ctx.mtx);
    Defer reset_requesting([&ctx]() {
        ctx.is_requesting.store(false);
        ctx.is_requesting.notify_all();
    });

    try {
        serial_tcflush(*serial);

        if (action == actions::REQ) {
            auto values = read_payload<std::uint8_t>(iss);
            send_adu(*serial, values);
            int n = recv_adu(*serial, values, config.read_timeout_ms, 50);
            if (n > 0) {
                print(format_payload(values));
                return;
            }
            throw ModbusError("timeout");
        }

        std::string _slave_id, _address;
        if (!(iss >> _slave_id >> _address)) {
            throw std::invalid_argument("invalid format");
        }
        auto slave_id = parse_number<std::uint8_t>(_slave_id);
        auto address = parse_number<std::uint16_t>(_address);
        std::uint16_t count = 1;

        if (is_any(action, actions::RB, actions::RR, actions::RIB, actions::RIR)) {
            count = read_number<std::uint32_t>(iss);
            if (count <= 0) {
                throw std::invalid_argument("invalid count");
            }
        }

        modbus->invoke(nmbs_set_destination_rtu_address, slave_id);

        if (action == actions::WB) {
            auto payload = read_payload<std::uint8_t>(iss);
            count = payload.size();
            if (count == 1) {
                modbus->call(nmbs_write_single_coil, address, payload[0]);
            } else {
                modbus->call(nmbs_write_multiple_coils, address, static_cast<int>(count), payload.data());
            }
            print(format_payload(payload, count));

        } else if (action == actions::RB) {
            std::vector<std::uint8_t> payload(count);
            modbus->call(nmbs_read_coils, address, count, payload.data());
            print(format_payload(payload, count));

        } else if (action == actions::WR) {
            auto payload = read_payload<std::uint16_t>(iss);
            count = payload.size();
            if (count == 1) {
                modbus->call(nmbs_write_single_register, address, payload[0]);
            } else {
                modbus->call(nmbs_write_multiple_registers, address, static_cast<int>(count), payload.data());
            }
            print(format_payload(payload, count));

        } else if (action == actions::RR) {
            std::vector<std::uint16_t> payload(count);
            modbus->call(nmbs_read_holding_registers, address, count, payload.data());
            print(format_payload(payload, count));

        } else if (action == actions::RIB) {
            std::vector<std::uint8_t> payload(count);
            modbus->call(nmbs_read_discrete_inputs, address, count, payload.data());
            print(format_payload(payload, count));

        } else if (action == actions::RIR) {
            std::vector<std::uint16_t> payload(count);
            modbus->call(nmbs_read_input_registers, address, count, payload.data());
            print(format_payload(payload, count));
        } else {
            throw std::invalid_argument("invalid action");
        }
    } catch (const std::invalid_argument& e) {
        print_error(error_types::VALUE, e.what());
    } catch (const ModbusError& e) {
        print_error(error_types::MODBUS, e.what());
    } catch (const std::exception& e) {
        print_error(error_types::SYSTEM, e.what());
    }
}

void stdin_listen(AppContext& ctx) {
    bool is_interactive = isatty(STDIN_FILENO);
    std::string line;

    while (ctx.is_running.load(std::memory_order_relaxed)) {
        if (is_interactive) {
            char* input = readline("");
            if (!input) {
                std::println("");
                break;
            }
            Defer free_input([input] { free(input); });
            line = std::string(input);
            if (!line.empty()) add_history(line.c_str());
        } else {
            if (!std::getline(std::cin, line)) {
                break;
            }
        }
        process_line(ctx, line);
    }
}

void print_help(const char* prog_name) {
    std::println(R"(Usage: {0} [-s] [-o device,baud_rate,parity,data_bits,stop_bits] [<action(rb|rr|rib|rir|wb|wr|req)> <slave_id> <address> [<count|payload>]]

Connection Parameters (-o, comma-separated, omit to use defaults):
  device      (default: /dev/ttyUSB0)
  baud_rate   (default: 115200)
  parity      (default: N)
  data_bits   (default: 8)
  stop_bits   (default: 1)

Options:
  -h, --help    Show this help message and exit
  -s            Enable background sniffing (Interactive mode only)
  -o <params>   Connection parameters

Execution Modes:
  Interactive:  Run without an action command. Opens a REPL.
  Single-shot:  Provide an action command to execute it once and exit.

Examples:
  {0} -o /dev/ttyS0,9600,E           (Interactive: Changes device, baud_rate, parity)
  {0} -s -o /dev/ttyUSB1             (Interactive: Enable sniffer on custom device)
  {0} rr 1 0x64 2                    (Single-shot: Read 2 registers from slave 1 at addr 0x64)

Actions:
  req <frame>                                 - Send raw frame
  wb  <slave_id> <address> <payload>          - Write Bit(s)
  rb  <slave_id> <address> <count>            - Read Bit(s)
  wr  <slave_id> <address> <payload>          - Write Register(s)
  rr  <slave_id> <address> [<count>]          - Read Register(s)
  rib <slave_id> <address> [<count>]          - Read Input Bit(s)
  rir <slave_id> <address> [<count>]          - Read Input Register(s))",
                 prog_name);
}
std::atomic<bool>* g_is_running;

extern "C" void handle_signal(int /*signo*/) {
    if (g_is_running) {
        g_is_running->store(false, std::memory_order_relaxed);
    }
}

int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::string_view opts;
    std::string cmd;
    bool sniff = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") { print_help(argv[0]); return 0; }
        if (arg == "-s") { sniff = true; continue; }
        if (arg == "-o" && i + 1 < argc) { opts = argv[++i]; continue; }
        for (; i < argc; ++i) {
            cmd += argv[i];
            cmd += " ";
        }
    }

    AppContext ctx;
    g_is_running = &ctx.is_running;
    auto& config = ctx.config;

    if (!opts.empty()) {
        std::istringstream iss{std::string(opts)};
        std::string token;
        if (std::getline(iss, token, ',') && !token.empty()) config.device = token;
        if (std::getline(iss, token, ',') && !token.empty()) config.baud_rate = std::stoi(token);
        if (std::getline(iss, token, ',') && !token.empty()) config.parity = token[0];
        if (std::getline(iss, token, ',') && !token.empty()) config.data_bits = std::stoi(token);
        if (std::getline(iss, token, ',') && !token.empty()) config.stop_bits = std::stoi(token);
    }

    serial_t* s = serial_new();
    Defer clean_serial([s] {
        serial_close(s);
        serial_free(s);
    });
    Serial serial(*s);
    ctx.serial = &serial;
    serial_parity_t parity_enum = (config.parity == 'E')   ? PARITY_EVEN
                                  : (config.parity == 'O') ? PARITY_ODD
                                                           : PARITY_NONE;
    serial.call(serial_open_advanced, config.device.c_str(),
                static_cast<std::uint32_t>(config.baud_rate),
                static_cast<unsigned int>(config.data_bits),
                parity_enum,
                static_cast<unsigned int>(config.stop_bits),
                false,   // xonxoff
                false);  // rtscts

    nmbs_platform_conf platform_conf;
    nmbs_platform_conf_create(&platform_conf);
    platform_conf.transport = NMBS_TRANSPORT_RTU;
    platform_conf.read = read_serial;
    platform_conf.write = write_serial;
    platform_conf.arg = serial.ctx();

    nmbs_t nmbs;
    Modbus modbus(nmbs);
    ctx.modbus = &modbus;
    modbus.call(nmbs_client_create, &platform_conf);
    modbus.invoke(nmbs_set_read_timeout, config.read_timeout_ms);
    modbus.invoke(nmbs_set_byte_timeout, 50);

    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    serial_tcflush(serial);

    if (!cmd.empty()) {
        process_line(ctx, cmd);
    } else {
        std::thread sniffer_thread;
        if (sniff) {
            sniffer_thread = std::thread(modbus_listen, &ctx);
        }
        stdin_listen(ctx);

        ctx.is_running.store(false);
        ctx.is_requesting.notify_all();
        if (sniff && sniffer_thread.joinable()) {
            sniffer_thread.join();
        }
    }

    return 0;
}
