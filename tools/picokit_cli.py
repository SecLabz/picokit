#!/usr/bin/env python3
"""PICokit — PIC18F ICSP Programmer host CLI.

Communicates with a Raspberry Pi Pico running PICokit firmware
using a binary frame protocol over USB CDC serial.
"""
import argparse
import struct
import sys
import time

import serial
from intelhex import IntelHex

# Protocol constants
CMD_DIAG         = 0x01
CMD_ERASE        = 0x02
CMD_WRITE_PAGE   = 0x03
CMD_WRITE_CONFIG = 0x04
CMD_WRITE_EEPROM = 0x05
CMD_READ         = 0x06
CMD_RESET_TARGET = 0x07
CMD_TEST_EEPROM  = 0x08
CMD_VERSION      = 0x09

STATUS_OK          = 0x00
STATUS_ERR_CMD     = 0x01
STATUS_ERR_CRC     = 0x02
STATUS_ERR_TARGET  = 0x03
STATUS_ERR_VERIFY  = 0x04
STATUS_ERR_PAYLOAD = 0x05

STATUS_NAMES = {
    STATUS_OK: "OK",
    STATUS_ERR_CMD: "invalid command",
    STATUS_ERR_CRC: "CRC mismatch",
    STATUS_ERR_TARGET: "no target detected",
    STATUS_ERR_VERIFY: "verification failed",
    STATUS_ERR_PAYLOAD: "invalid payload",
}

FLASH_PAGE_SIZE = 128
FLASH_START     = 0x000000
EEPROM_START    = 0x310000
CONFIG_START    = 0x300000
CONFIG_END      = 0x30000F
READ_CHUNK      = 128  # bytes per READ command


def crc8(data: bytes) -> int:
    """CRC-8 with polynomial 0x07, init 0x00."""
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


class Protocol:
    """Binary frame protocol over serial."""

    def __init__(self, port: str, baudrate: int = 115200, timeout: float = 30.0):
        self.ser = serial.Serial(port, baudrate, timeout=timeout)
        time.sleep(2)  # wait for Pico USB CDC init

    def close(self):
        if self.ser.is_open:
            self.ser.close()

    def send(self, cmd: int, payload: bytes = b"", retries: int = 3) -> tuple[int, bytes]:
        """Send a command and return (status, response_payload)."""
        for attempt in range(retries):
            try:
                self.ser.reset_input_buffer()
                frame = struct.pack("<BH", cmd, len(payload)) + payload
                frame += bytes([crc8(frame)])
                self.ser.write(frame)
                return self._recv()
            except IOError:
                if attempt == retries - 1:
                    raise
                time.sleep(0.05)

    def _recv(self) -> tuple[int, bytes]:
        """Read a response frame. Returns (status, payload)."""
        hdr = self._read_exact(3)
        if hdr is None:
            raise IOError("timeout reading response header")
        status = hdr[0]
        length = struct.unpack("<H", hdr[1:3])[0]
        payload = b""
        if length > 0:
            payload = self._read_exact(length)
            if payload is None:
                raise IOError("timeout reading response payload")
        crc_byte = self._read_exact(1)
        if crc_byte is None:
            raise IOError("timeout reading response CRC")
        expected = crc8(hdr + payload)
        if crc_byte[0] != expected:
            raise IOError(f"response CRC mismatch: got 0x{crc_byte[0]:02X}, expected 0x{expected:02X}")
        return status, payload

    def _read_exact(self, n: int) -> bytes | None:
        data = self.ser.read(n)
        if len(data) != n:
            return None
        return data

    def send_ok(self, cmd: int, payload: bytes = b"") -> bytes:
        """Send command, raise on error, return response payload."""
        status, resp = self.send(cmd, payload)
        if status != STATUS_OK:
            name = STATUS_NAMES.get(status, f"0x{status:02X}")
            raise RuntimeError(f"device error: {name}")
        return resp


def progress_bar(current: int, total: int, width: int = 40, prefix: str = ""):
    frac = current / total if total else 1
    filled = int(width * frac)
    bar = "█" * filled + "░" * (width - filled)
    pct = frac * 100
    print(f"\r{prefix}[{bar}] {pct:5.1f}% ({current}/{total})", end="", flush=True)
    if current >= total:
        print()


def cmd_version(proto: Protocol):
    resp = proto.send_ok(CMD_VERSION)
    print(f"Firmware: {resp.decode('utf-8', errors='replace')}")


def cmd_diag(proto: Protocol):
    resp = proto.send_ok(CMD_DIAG)
    dev_id = struct.unpack("<H", resp[0:2])[0]
    rev_id = struct.unpack("<H", resp[2:4])[0]
    name = resp[4:].decode("utf-8", errors="replace")
    rev_major = chr(((rev_id >> 6) & 0x1F) + 65)
    rev_minor = rev_id & 0x3F
    print(f"Device:   {name}")
    print(f"ID:       0x{dev_id:04X}")
    print(f"Revision: {rev_major}{rev_minor} (0x{rev_id:04X})")


def cmd_wipe(proto: Protocol):
    print("Erasing all memory...", end="", flush=True)
    proto.send_ok(CMD_ERASE)
    print(" done.")


def cmd_write(proto: Protocol, hex_file: str):
    ih = IntelHex(hex_file)

    # 1) Diag
    resp = proto.send_ok(CMD_DIAG)
    dev_id = struct.unpack("<H", resp[0:2])[0]
    name = resp[4:].decode("utf-8", errors="replace")
    print(f"Target: {name} (0x{dev_id:04X})")

    # 2) Erase
    print("Erasing...", end="", flush=True)
    proto.send_ok(CMD_ERASE)
    print(" done.")

    # 3) Collect pages from HEX
    flash_pages = {}  # page_start_addr -> bytearray(128)
    config_data = {}  # addr -> byte
    eeprom_data = {}  # addr -> byte

    for addr in ih.addresses():
        val = ih[addr]
        if CONFIG_START <= addr <= CONFIG_END:
            config_data[addr] = val
        elif addr >= EEPROM_START:
            eeprom_data[addr] = val
        else:
            page_start = (addr // FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE
            if page_start not in flash_pages:
                flash_pages[page_start] = bytearray([0xFF] * FLASH_PAGE_SIZE)
            flash_pages[page_start][addr - page_start] = val

    # 4) Write flash pages
    pages = sorted(flash_pages.keys())
    total = len(pages)
    if total:
        print(f"Writing {total} flash pages...")
        for i, page_addr in enumerate(pages):
            payload = struct.pack("<I", page_addr) + bytes(flash_pages[page_addr])
            proto.send_ok(CMD_WRITE_PAGE, payload)
            progress_bar(i + 1, total, prefix="  Flash: ")

    # 5) Write config
    if config_data:
        base = min(config_data.keys())
        end = max(config_data.keys()) + 1
        length = end - base
        buf = bytearray([0xFF] * length)
        for addr, val in config_data.items():
            buf[addr - base] = val
        payload = struct.pack("<IH", base, length) + bytes(buf)
        print("Writing config words...", end="", flush=True)
        proto.send_ok(CMD_WRITE_CONFIG, payload)
        print(" done.")

    # 6) Write EEPROM
    if eeprom_data:
        base = min(eeprom_data.keys())
        end = max(eeprom_data.keys()) + 1
        length = end - base
        buf = bytearray([0xFF] * length)
        for addr, val in eeprom_data.items():
            buf[addr - base] = val
        # Send in chunks to stay within payload limit
        chunk = 128
        total_chunks = (length + chunk - 1) // chunk
        print(f"Writing EEPROM ({length} bytes)...")
        for ci in range(total_chunks):
            offset = ci * chunk
            n = min(chunk, length - offset)
            payload = struct.pack("<IH", base + offset, n) + bytes(buf[offset:offset + n])
            proto.send_ok(CMD_WRITE_EEPROM, payload)
            progress_bar(ci + 1, total_chunks, prefix="  EEPROM: ")

    # 7) Reset target
    proto.send_ok(CMD_RESET_TARGET)
    print("Write complete.")


def cmd_verify(proto: Protocol, hex_file: str):
    ih = IntelHex(hex_file)

    segments = ih.segments()
    mismatches = 0

    # Compute total bytes across all segments for progress
    total_bytes = sum(end - start for start, end in segments)
    bytes_checked = 0

    print(f"Verifying against {hex_file}...")
    for seg_start, seg_end in segments:
        seg_len = seg_end - seg_start

        for offset in range(0, seg_len, READ_CHUNK):
            addr = seg_start + offset
            n = min(READ_CHUNK, seg_len - offset)
            payload = struct.pack("<IH", addr, n)
            resp = proto.send_ok(CMD_READ, payload)

            for i in range(n):
                expected = ih[addr + i]
                actual = resp[i]
                if expected != actual:
                    if mismatches < 10:
                        print(f"  Mismatch at 0x{addr + i:06X}: expected 0x{expected:02X}, read 0x{actual:02X}")
                    mismatches += 1

            bytes_checked += n
            progress_bar(bytes_checked, total_bytes, prefix="  Verify: ")

    proto.send_ok(CMD_RESET_TARGET)
    if mismatches == 0:
        print(f"Verification OK: {total_bytes} bytes match.")
    else:
        print(f"Verification FAILED: {mismatches} mismatches in {total_bytes} bytes.")
        sys.exit(1)


def cmd_dump(proto: Protocol, filename: str, start: int, size: int, fmt: str):
    ih = IntelHex()
    total_read = 0

    print(f"Reading {size} bytes from 0x{start:06X}...")
    for offset in range(0, size, READ_CHUNK):
        addr = start + offset
        n = min(READ_CHUNK, size - offset)
        payload = struct.pack("<IH", addr, n)
        resp = proto.send_ok(CMD_READ, payload)
        for i in range(n):
            ih[addr + i] = resp[i]
        total_read += n
        progress_bar(total_read, size, prefix="  Read: ")

    proto.send_ok(CMD_RESET_TARGET)

    if fmt == "bin":
        ih.tobinfile(filename)
    else:
        ih.write_hex_file(filename)
    print(f"Saved to {filename}")


def cmd_config(proto: Protocol):
    # Read 16 bytes at 0x300000
    payload = struct.pack("<IH", CONFIG_START, 16)
    resp = proto.send_ok(CMD_READ, payload)
    proto.send_ok(CMD_RESET_TARGET)

    print("-" * 72)
    print(f"{'Register':<12} {'Addr':<8} {'Value':<6} {'Description'}")
    print("-" * 72)

    def fextosc(v):
        return {7: "ECH", 6: "ECM", 5: "ECL", 4: "OFF", 2: "HS", 1: "XT", 0: "LP"}.get(v & 7, "Reserved")

    def rstosc(v):
        return {7: "EXTOSC", 6: "HFINTOSC 1MHz", 5: "LFINTOSC", 4: "SOSC",
                2: "EXTOSC+4xPLL", 0: "HFINTOSC 64MHz"}.get((v >> 4) & 7, "Reserved")

    def wdt(v):
        return {3: "Enabled", 2: "Sleep-disabled", 1: "SWDTEN", 0: "Disabled"}.get((v >> 4) & 3, "?")

    for i in range(0, 16, 2):
        lo = resp[i]
        hi = resp[i + 1]
        addr = CONFIG_START + i
        desc_lo = ""
        desc_hi = ""
        if i == 0:
            desc_lo = f"FEXTOSC={fextosc(lo)} RSTOSC={rstosc(lo)}"
            desc_hi = f"CLKDIV={hi & 0x0F}"
        elif i == 2:
            mclr = "on" if (lo & 0x80) else "off"
            desc_lo = f"MCLR={mclr}"
        elif i == 4:
            desc_lo = f"WDT={wdt(lo)}"
        elif i == 6:
            lvp = "on" if (lo & 0x20) else "off"
            dbg = "off" if (lo & 0x80) else "on"
            desc_lo = f"LVP={lvp} DEBUG={dbg}"
        elif i == 8:
            cp = "on" if not (lo & 1) else "off"
            desc_lo = f"CP={cp}"

        print(f"{'CONFIG' + str(i // 2 + 1) + 'L':<12} {addr:06X}   0x{lo:02X}   {desc_lo}")
        print(f"{'CONFIG' + str(i // 2 + 1) + 'H':<12} {addr + 1:06X}   0x{hi:02X}   {desc_hi}")

    print("-" * 72)


def cmd_test_eeprom(proto: Protocol):
    print("Running EEPROM test...", end="", flush=True)
    resp = proto.send_ok(CMD_TEST_EEPROM)
    result = resp[0] if resp else 0
    if result:
        print(" PASS")
    else:
        print(" FAIL")
        sys.exit(1)


def cmd_reset(proto: Protocol):
    proto.send_ok(CMD_RESET_TARGET)
    print("Target reset.")


def main():
    parser = argparse.ArgumentParser(
        description="PICokit — PIC18F ICSP Programmer",
        epilog="Examples:\n"
               "  %(prog)s /dev/tty.usbmodem* diag\n"
               "  %(prog)s /dev/ttyACM0 write firmware.hex\n"
               "  %(prog)s /dev/ttyACM0 verify firmware.hex\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("port", help="Serial port")

    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("version", help="Show firmware version")
    sub.add_parser("diag", help="Read device ID and revision")

    sub.add_parser("wipe", help="Bulk erase all memory")

    p = sub.add_parser("write", help="Program HEX file")
    p.add_argument("file", help="Intel HEX file")

    p = sub.add_parser("verify", help="Verify against HEX file")
    p.add_argument("file", help="Intel HEX file")

    p = sub.add_parser("dump", help="Read memory to file")
    p.add_argument("file", help="Output file")
    p.add_argument("--start", default=0, type=lambda x: int(x, 0), help="Start address (default: 0)")
    p.add_argument("--size", default=131072, type=lambda x: int(x, 0), help="Size in bytes (default: 128K)")
    p.add_argument("--format", choices=["hex", "bin"], default="hex", help="Output format")

    sub.add_parser("config", help="Read and decode config bits")
    sub.add_parser("test_eeprom", help="EEPROM write/read self-test")
    sub.add_parser("reset", help="Release target from ICSP")

    args = parser.parse_args()

    proto = Protocol(args.port)
    try:
        if args.command == "version":
            cmd_version(proto)
        elif args.command == "diag":
            cmd_diag(proto)
        elif args.command == "wipe":
            cmd_wipe(proto)
        elif args.command == "write":
            cmd_write(proto, args.file)
        elif args.command == "verify":
            cmd_verify(proto, args.file)
        elif args.command == "dump":
            cmd_dump(proto, args.file, args.start, args.size, args.format)
        elif args.command == "config":
            cmd_config(proto)
        elif args.command == "test_eeprom":
            cmd_test_eeprom(proto)
        elif args.command == "reset":
            cmd_reset(proto)
    except (RuntimeError, IOError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nAborted.")
        sys.exit(130)
    finally:
        proto.close()


if __name__ == "__main__":
    main()
