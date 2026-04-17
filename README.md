# mbsc

`mbsc` is a lightweight, interactive Modbus command-line tool.
It supports both single-shot commands for scripting and an interactive REPL for real-time device interaction.

## Features

* Interactive REPL.
* Background traffic sniffing.
* Support for standard Modbus functions (Read/Write Coils, Registers).
* Raw PDU frame transmission.

## Usage

Note: all the payload(printed and expected) is in hexidecimal format, while `slave_id`, `address` and `count` can be in both hexidecimal or decimal formats.

```text
Usage: ./mbsc [-s] [-o device,baud_rate,parity,data_bits,stop_bits] [<action(rb|rr|rib|rir|wb|wr|req)> <slave_id> <address> [<count|payload>]]

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
  ./mbsc -o /dev/ttyS0,9600,E           (Interactive: Changes device, baud_rate, parity)
  ./mbsc -s -o /dev/ttyUSB1             (Interactive: Enable sniffer on custom device)
  ./mbsc rr 1 0x64 2                    (Single-shot: Read 2 registers from slave 1 at addr 0x64)

Actions:
  req <frame>                                 - Send raw frame
  wb  <slave_id> <address> <payload>          - Write Bit(s)
  rb  <slave_id> <address> <count>            - Read Bit(s)
  wr  <slave_id> <address> <payload>          - Write Register(s)
  rr  <slave_id> <address> [<count>]          - Read Register(s)
  rib <slave_id> <address> [<count>]          - Read Input Bit(s)
  rir <slave_id> <address> [<count>]          - Read Input Register(s)
```

## Examples

```bash
# read holding registers
> rr 1 0x62 2
00110011

# write bits
> wb 2 50 01000101
01000101

# write single register
> wr 5 10 ff00
ff00

# read discrete input bits
> rib 1 0 4
01010000

# raw ADU request
> req 01030062000265D5
010304001100116a3a

# error handling (printed to stderr)
> rr 1 0x64 2
modbus: modbus exception 2: illegal data address

# sniffer output (started with -s)
01030062000265D5
010304001100118AA9
```

## Dependencies

To build `mbsc`, you need the following:

* **C++ Compiler:** Must support C++23 (e.g., GCC 13+ or Clang 16+)
* **Build Tools:** `make`, `pkg-config`, `wget`
* **Libraries:** `libreadline-dev`, `libncurses-dev` (required for interactive REPL)
* **Cross-compilation (Optional):** Docker with `buildx` emulation support

## Building

To build `mbsc` for your current host machine (defaults to amd64), run:

```bash
make
```

The statically linked executable will be generated at build/mbsc.amd64 (or your specified ARCH).

#### Cross-Compiling (Docker Buildx)

Cross-compilation is fully containerized using Docker Buildx and QEMU emulation. To build for a different architecture (e.g., arm64, armhf), run:

```bash
make buildx ARCH=arm64
```

This automatically registers the QEMU emulators, builds the toolchain container, and outputs the cross-compiled binary to the build/ directory.

## License

[MIT](https://spdx.org/licenses/MIT.html)