// clang-format off
#include <cstdio>
#include <readline/history.h>
#include <readline/readline.h>
// clang-format on

#include <termios.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <print>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../include/mbsc/constants.hpp"
#include "mbsc/constants.hpp"
#include "mbsc/modbus.hpp"
#include "mbsc/utils.hpp"

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
  Modbus& modbus;
  std::mutex mtx;
  std::atomic<bool> is_requesting{false};
  std::atomic<bool> is_running{true};
};

inline void Print(std::string_view msg) { std::println("{}", msg); }

inline void PrintError(std::string_view type, std::string_view msg) { std::println(stderr, "{}: {}", type, msg); }

/*
 * output format:
 * - <payload>
 */
void ModbusListen(AppContext* ctx) {
  // 10ms is a safe user-space approximation for the Modbus t3.5 silence gap.
  constexpr int32_t kInterByteTimeoutMs = 10;

  auto& modbus = ctx->modbus;
  std::vector<std::uint8_t> buf;
  std::vector<std::uint8_t> adu;

  while (ctx->is_running.load(std::memory_order_relaxed)) {
    try {
      ctx->is_requesting.wait(true);
      std::lock_guard lock(ctx->mtx);
      if (ctx->is_requesting.load(std::memory_order_relaxed)) {
        continue;
      }
      while (true) {
        int n = modbus.RecvAdu(buf, adu, 10, kInterByteTimeoutMs);
        if (n != 0) {
          Print(format_payload(adu));
        } else {
          break;
        }
      }
    } catch (const std::exception& e) {
      PrintError(error_types::SYSTEM, e.what());
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
void ProcessLine(AppContext& ctx, const std::string& line) {
  if (line.empty()) return;

  std::istringstream iss(line);
  std::string action;

  if (!(iss >> action)) {
    throw std::invalid_argument("missing action");
  }

  auto& config = ctx.config;
  auto& modbus = ctx.modbus;

  ctx.is_requesting.store(true);
  ctx.is_requesting.notify_all();
  std::lock_guard lock(ctx.mtx);
  Defer reset_requesting([&ctx]() {
    ctx.is_requesting.store(false);
    ctx.is_requesting.notify_all();
  });

  try {
    modbus.TcFlush();

    if (action == actions::REQ) {
      auto values = read_payload<std::uint8_t>(iss);
      modbus.SendAdu(values);

      std::vector<std::uint8_t> buf;
      std::vector<std::uint8_t> adu;
      int n = modbus.RecvAdu(buf, adu, config.read_timeout_ms, 50);
      if (n > 0) {
        Print(format_payload(adu));
        return;
      }
      throw ModbusError("timeout or corrupted data");
    }

    std::string str_slave_id, str_address;
    if (!(iss >> str_slave_id >> str_address)) {
      throw std::invalid_argument("invalid format");
    }
    auto slave_id = parse_number<std::uint8_t>(str_slave_id);
    auto address = parse_number<std::uint16_t>(str_address);
    std::uint16_t count = 1;

    if (is_any(action, actions::RB, actions::RR, actions::RIB, actions::RIR)) {
      count = read_number<std::uint32_t>(iss);
      if (count <= 0) {
        throw std::invalid_argument("invalid count");
      }
    }

    modbus.Invoke(nmbs_set_destination_rtu_address, slave_id);

    if (action == actions::WB) {
      auto payload = read_payload<std::uint8_t>(iss);
      count = payload.size();
      if (count == 1) {
        modbus.Call(nmbs_write_single_coil, address, payload[0]);
      } else {
        modbus.Call(nmbs_write_multiple_coils, address, static_cast<int>(count), payload.data());
      }
      Print(format_payload(payload, count));

    } else if (action == actions::RB) {
      std::vector<std::uint8_t> payload(count);
      modbus.Call(nmbs_read_coils, address, count, payload.data());
      Print(format_payload(payload, count));

    } else if (action == actions::WR) {
      auto payload = read_payload<std::uint16_t>(iss);
      count = payload.size();
      if (count == 1) {
        modbus.Call(nmbs_write_single_register, address, payload[0]);
      } else {
        modbus.Call(nmbs_write_multiple_registers, address, static_cast<int>(count), payload.data());
      }
      Print(format_payload(payload, count));

    } else if (action == actions::RR) {
      std::vector<std::uint16_t> payload(count);
      modbus.Call(nmbs_read_holding_registers, address, count, payload.data());
      Print(format_payload(payload, count));

    } else if (action == actions::RIB) {
      std::vector<std::uint8_t> payload(count);
      modbus.Call(nmbs_read_discrete_inputs, address, count, payload.data());
      Print(format_payload(payload, count));

    } else if (action == actions::RIR) {
      std::vector<std::uint16_t> payload(count);
      modbus.Call(nmbs_read_input_registers, address, count, payload.data());
      Print(format_payload(payload, count));
    } else {
      throw std::invalid_argument("invalid action");
    }
  } catch (const std::invalid_argument& e) {
    PrintError(error_types::VALUE, e.what());
  } catch (const ModbusError& e) {
    PrintError(error_types::MODBUS, e.what());
  } catch (const std::exception& e) {
    PrintError(error_types::SYSTEM, e.what());
  }
}

void StdinListen(AppContext& ctx) {
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
    ProcessLine(ctx, line);
  }
}

void PrintHelp(const char* prog_name) {
  std::println(
      R"(Usage: {0} [-s] [-o device,baud_rate,parity,data_bits,stop_bits] [<action(rb|rr|rib|rir|wb|wr|req)> <slave_id> <address> [<count|payload>]]

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
    if (arg == "-h" || arg == "--help") {
      PrintHelp(argv[0]);
      return 0;
    }
    if (arg == "-s") {
      sniff = true;
      continue;
    }
    if (arg == "-o" && i + 1 < argc) {
      opts = argv[++i];
      continue;
    }
    for (; i < argc; ++i) {
      cmd += argv[i];
      cmd += " ";
    }
  }

  Config config;
  if (!opts.empty()) {
    std::istringstream iss{std::string(opts)};
    std::string token;
    if (std::getline(iss, token, ',') && !token.empty()) config.device = token;
    if (std::getline(iss, token, ',') && !token.empty()) config.baud_rate = std::stoi(token);
    if (std::getline(iss, token, ',') && !token.empty()) config.parity = token[0];
    if (std::getline(iss, token, ',') && !token.empty()) config.data_bits = std::stoi(token);
    if (std::getline(iss, token, ',') && !token.empty()) config.stop_bits = std::stoi(token);
  }

  Modbus modbus(config.device, config.baud_rate, config.data_bits, config.parity, config.stop_bits,
                false,   // xonxoff
                false);  // rtscts

  modbus.Invoke(nmbs_set_read_timeout, config.read_timeout_ms);
  modbus.Invoke(nmbs_set_byte_timeout, 50);

  struct sigaction sa{};
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  AppContext ctx{.config = config, .modbus = modbus, .mtx = {}};
  g_is_running = &ctx.is_running;

  modbus.TcFlush();

  if (!cmd.empty()) {
    ProcessLine(ctx, cmd);
  } else {
    std::thread sniffer_thread;
    if (sniff) {
      sniffer_thread = std::thread(ModbusListen, &ctx);
    }
    StdinListen(ctx);

    ctx.is_running.store(false);
    ctx.is_requesting.notify_all();
    if (sniff && sniffer_thread.joinable()) {
      sniffer_thread.join();
    }
  }

  return 0;
}
