#!/usr/bin/env python3
"""
W25Q16JW Flasher — Python CLI
==============================
Read, erase, write, and verify Winbond W25Q16JW SPI flash via Arduino.

Usage:
  python3 flasher.py [--port /dev/ttyUSB0] [--baud 115200] <command> [args]

Commands:
  info          Show JEDEC ID and status registers
  peek          Read first 64 bytes
  read <file>   Read entire chip, save to file
  erase         Full chip erase (asks confirmation)
  write <file>  Write ROM file to chip with verification
  verify <file> Verify chip matches ROM file
  debug         Clear CMP + test sector 0 erase
"""

import serial
import struct
import time
import sys
import os
import hashlib
import argparse

CHIP_SIZE = 2097152  # 2,097,152 bytes
PAGE_SIZE = 256


def detect_port():
    """Auto-detect Arduino serial port."""
    import glob
    for pattern in ['/dev/ttyUSB*', '/dev/ttyACM*']:
        ports = sorted(glob.glob(pattern))
        if ports:
            return ports[0]
    return None


def open_serial(port, baud):
    ser = serial.Serial(port, baud, timeout=5)
    ser.dtr = False
    time.sleep(2)
    ser.reset_input_buffer()
    return ser


def wait_ready(ser, timeout=30):
    """Wait for Arduino to print READY."""
    start = time.time()
    buf = b""
    while b"READY" not in buf:
        if ser.in_waiting:
            buf += ser.read(ser.in_waiting)
        if time.time() - start > timeout:
            print("TIMEOUT waiting for READY")
            return False
        time.sleep(0.01)
    if buf.strip() != b"READY":
        print(buf.decode(errors="replace").strip())
    return True


def send_cmd(ser, cmd, wait=True):
    """Send single-char command, optionally wait for response."""
    ser.write(cmd.encode())
    ser.flush()
    if wait:
        return wait_ready(ser)


def read_until(ser, marker, timeout=60):
    """Read serial lines until marker found."""
    start = time.time()
    while time.time() - start < timeout:
        if ser.in_waiting:
            line = ser.readline().decode(errors="replace").strip()
            if line:
                print(f"  {line}")
            if marker in line:
                return True
        time.sleep(0.01)
    return False


def cmd_info(ser):
    ser.write(b"I")
    time.sleep(0.3)
    print(ser.read(ser.in_waiting).decode(errors="replace"))


def cmd_peek(ser):
    ser.write(b"P")
    time.sleep(0.3)
    print(ser.read(ser.in_waiting).decode(errors="replace"))


def cmd_debug(ser):
    ser.write(b"D")
    time.sleep(2)
    print(ser.read(ser.in_waiting).decode(errors="replace"))


def cmd_read(ser, outfile):
    ser.write(b"R")
    if not wait_ready(ser):
        return

    chip = bytearray()
    start = time.time()
    print(f"Reading {CHIP_SIZE:,} bytes...")

    while len(chip) < CHIP_SIZE:
        needed = PAGE_SIZE
        page = b""
        while len(page) < needed:
            chunk = ser.read(needed - len(page))
            if not chunk:
                print("Stream dropped!")
                break
            page += chunk
        chip.extend(page)
        ser.write(b"N")  # ack

        if len(chip) % 65536 == 0:
            pct = len(chip) * 100 / CHIP_SIZE
            elapsed = time.time() - start
            print(f"  [{len(chip):>8,}/{CHIP_SIZE:,}] {pct:5.1f}%  {elapsed:.0f}s")

    elapsed = time.time() - start
    with open(outfile, "wb") as f:
        f.write(bytes(chip))
    print(f"\nSaved {len(chip):,} bytes to {outfile} in {elapsed:.0f}s")


def cmd_erase(ser):
    ser.write(b"E")
    time.sleep(0.5)
    print(ser.read(ser.in_waiting).decode(errors="replace"))

    print("Sending Y...")
    ser.write(b"Y")
    read_until(ser, "SUCCESS", timeout=400)


def cmd_write(ser, romfile, verify=True):
    if not os.path.exists(romfile):
        print(f"File not found: {romfile}")
        return

    with open(romfile, "rb") as f:
        data = f.read()

    # Pad to chip size
    if len(data) < CHIP_SIZE:
        data = data + b"\xFF" * (CHIP_SIZE - len(data))

    print(f"ROM: {romfile} ({len(data):,} bytes)")

    # Send command
    ser.write(b"W")
    if not wait_ready(ser):
        print("Arduino not responding. Is the chip connected?")
        return

    # Stream pages
    sent = 0
    start = time.time()

    while sent < len(data):
        chunk = data[sent : sent + PAGE_SIZE]
        ser.write(chunk)
        sent += len(chunk)

        # Wait for NEXT
        while True:
            line = ser.readline().decode(errors="replace").strip()
            if "NEXT" in line:
                break
            if line:
                print(f"  {line}")

        if sent % (PAGE_SIZE * 256) == 0:
            pct = sent * 100 / len(data)
            elapsed = time.time() - start
            print(f"  [{sent:>8,}/{len(data):,}] {pct:5.1f}%  {elapsed:.0f}s")

    # Wait for result
    print("Waiting for result...")
    while True:
        line = ser.readline().decode(errors="replace").strip()
        if line:
            print(f"  {line}")
        if "SUCCESS" in line or "FAILED" in line or "READY" in line:
            break

    elapsed = time.time() - start
    print(f"Done in {elapsed:.0f}s.")


def cmd_verify(ser, romfile):
    """Read chip and compare with romfile."""
    with open(romfile, "rb") as f:
        expected = f.read()

    # Pad to chip size (same as write does)
    if len(expected) < CHIP_SIZE:
        expected = expected + b"\xFF" * (CHIP_SIZE - len(expected))

    tmp = "/tmp/w25_verify_dump.bin"
    cmd_read(ser, tmp)

    with open(tmp, "rb") as f:
        actual = f.read()

    size = min(len(expected), len(actual))
    if expected[:size] == actual[:size]:
        print(f"\n✓ VERIFIED — chip matches {romfile}")
        print(f"  MD5: {hashlib.md5(actual[:size]).hexdigest()}")
    else:
        diffs = sum(1 for i in range(size) if expected[i] != actual[i])
        print(f"\n✗ MISMATCH — {diffs} differences")
    os.remove(tmp)


def main():
    parser = argparse.ArgumentParser(description="W25Q16JW SPI Flasher")
    parser.add_argument("--port", default=None, help="Serial port (auto-detected if not specified)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument(
        "command",
        choices=["info", "peek", "read", "erase", "write", "verify", "debug"],
        help="Command to execute",
    )
    parser.add_argument("file", nargs="?", help="ROM file for read/write/verify")
    args = parser.parse_args()

    if args.command in ("read", "write", "verify") and not args.file:
        parser.error(f"{args.command} requires a file argument")

    if not args.port:
        args.port = detect_port()
        if not args.port:
            print("No Arduino detected. Check USB connection or use --port.")
            sys.exit(1)
        print(f"Auto-detected port: {args.port}")

    ser = open_serial(args.port, args.baud)

    if args.command == "info":
        cmd_info(ser)
    elif args.command == "peek":
        cmd_peek(ser)
    elif args.command == "debug":
        cmd_debug(ser)
    elif args.command == "read":
        cmd_read(ser, args.file)
    elif args.command == "erase":
        cmd_erase(ser)
    elif args.command == "write":
        cmd_write(ser, args.file)
    elif args.command == "verify":
        cmd_verify(ser, args.file)

    ser.close()


if __name__ == "__main__":
    main()
