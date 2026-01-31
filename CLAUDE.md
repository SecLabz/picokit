# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ICSP (In-Circuit Serial Programming) programmer for PIC18FxxK42 microcontrollers (tested on PIC18F27K42) using a Raspberry Pi Pico (RP2040). The Pico runs C firmware that implements the ICSP protocol over SPI, communicating with a host PC via a binary serial protocol over USB CDC. A Python CLI tool (`tools/picokit_cli.py`) drives programming operations from the host.

## Build Commands

```bash
# Build firmware (output: build/picokit.uf2)
mkdir -p build && cd build && cmake .. && make -j

# Deploy via picotool
picotool load -v -x build/picokit.uf2 -f

# Host tool usage (requires pyserial, intelhex)
python3 tools/picokit_cli.py /dev/ttyACM0 <command>
# Commands: version, diag, wipe, write <hex>, verify <hex>, dump <file>, config, test_eeprom, reset
```

## Architecture

```
tools/picokit_cli.py (host)  ──binary serial 115200──▶  src/main.c (Pico)  ──SPI 5MHz──▶  PIC18 target
                                                          │
                                                    src/protocol.c/h  (frame codec, CRC)
                                                    src/icsp.c/h      (ICSP protocol)
                                                    src/target.c/h    (chip ID table)
```

- **src/main.c**: Protocol dispatch loop — reads binary frames, dispatches to handlers, sends responses.
- **src/protocol.c/h**: Binary frame codec with CRC-8. Frame format: `[CMD/STATUS:1][LEN:2 LE][PAYLOAD:N][CRC8:1]`.
- **src/icsp.c/h**: Low-level ICSP protocol — LVP entry/exit, SPI command encoding (8-bit opcode + 24-bit payload), page program/erase with timing delays.
- **src/target.c/h**: Chip identification table and memory map constants.
- **tools/picokit_cli.py**: Host CLI with Protocol class. All HEX parsing done in Python.

## Prerequisites

- **LVP must be enabled** on the target PIC. This programmer only supports Low-Voltage Programming. If LVP is disabled, 9V High-Voltage Programming is required, which is not supported.

## Key Protocol Details

- SPI Mode 1 (CPOL=0, CPHA=1), 5 MHz, MSB-first
- Must re-enter LVP after bulk erase before writing
- Timing: bulk erase 26ms, page erase 11ms, flash row write 3ms, EEPROM write 11ms/byte
- GPIO29 = MCLR, SPI0 pins: GP6 (CLK), GP7 (MOSI/DAT), GP4 (MISO)

## PIC18F27K42 Memory Map

- Flash: 0x000000–0x01FFFF (128KB, 128-byte pages)
- EEPROM: 0x310000–0x3103FF (1KB)
- Config: 0x300000–0x30000F
- Device/Revision ID: 0x3FFFFE / 0x3FFFFC
