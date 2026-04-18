# mbsc

A Modbus command-line interface.

## Features

* Interactive REPL.
* Background traffic sniffing.
* Support for standard Modbus functions (Read/Write Coils, Registers).
* Raw PDU frame transmission.


## Usage

```text
Usage: ./build/mbsc [-s] [-o device,baud_rate,parity,data_bits,stop_bits] [<action(rb|rr|rib|rir|wb|wr|req)> <slave_id> <address> [<count|payload>]]

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
  ./build/mbsc -o /dev/ttyS0,9600,E           (Interactive: Changes device, baud_rate, parity)
  ./build/mbsc -s -o /dev/ttyUSB1             (Interactive: Enable sniffer on custom device)
  ./build/mbsc rr 1 0x64 2                    (Single-shot: Read 2 registers from slave 1 at addr 0x64)

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
# Read Holding Registers
> rr 1 0x62 2
00110011

# Write Bits
> wb 2 50 01000101
01000101

# Write Single Register
> wr 5 10 ff00
ff00

# Read Discrete Input Bits
> rib 1 0 4
01010000

# Raw PDU Request
> req 010300620002
01030400110011

# Error Handling (printed to stderr)
> rr 1 0x64 2
modbus: modbus exception 2: illegal data address

# Sniffer Output (started with -s)
# The modbus_listen thread prints full frames including Slave ID and PDU
010300620002
01030400110011
```

P.S. all the payload(receiving/expected) is in hex format.

## License

MIT License