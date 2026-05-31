# w25q16jw-arduino-flasher

Arduino-based SPI programmer for Winbond W25Q16JW (16Mbit / 2MB) flash chips.

## Why this exists

flashrom may report "Block protection is disabled" when BP bits are zero, but the W25Q16JW can still reject erase operations if **SR2 bit 6 (CMP)** is set.

This tool explicitly reads SR2 and clears CMP before erase/write:

```
SR2 0x42 → 0x02
```

If your chip reads fine but silently ignores all erase commands, even with Write Enable succeeding, check SR2 for CMP.

## Hardware

- Arduino Uno R3 (or compatible)
- 1.8V level shifter (green adapter) **required** — W25Q16JW is a 1.8V chip
- SOIC8 test clip

### Wiring

```
Arduino          Level Shifter        Chip (SOIC8)
───────────────────────────────────────────────────
D10  (SS)    →   [HV→LV]         →   Pin 1 (CS#)
D12  (MISO)  ←   [LV←HV]         ←   Pin 2 (DO)
D11  (MOSI)  →   [HV→LV]         →   Pin 5 (DI)
D13  (SCK)   →   [HV→LV]         →   Pin 6 (CLK)
3.3V         →   HV VCC
GND          →   HV GND
                 LV VCC           →   Pin 8 (VCC)
                 LV GND           →   Pin 4 (GND)
```

Pins 3 (WP#) and 7 (HOLD#) can be left floating — the W25Q16JW has QE=1 by factory default.

## Software

### Arduino

Upload `w25q16jw-arduino-flasher.ino` via Arduino IDE or arduino-cli:

```bash
arduino-cli compile --fqbn arduino:avr:uno .
arduino-cli upload -p /dev/ttyUSB0 --fqbn arduino:avr:uno .
```

### Python CLI

```bash
# Check chip status
python3 flasher.py --port /dev/ttyUSB0 info

# Peek first 64 bytes
python3 flasher.py --port /dev/ttyUSB0 peek

# Read entire chip to file
python3 flasher.py --port /dev/ttyUSB0 read backup.rom

# Full chip erase (~3.4s)
python3 flasher.py --port /dev/ttyUSB0 erase

# Write ROM with page-level verification
python3 flasher.py --port /dev/ttyUSB0 write stock.rom

# Verify chip against ROM
python3 flasher.py --port /dev/ttyUSB0 verify stock.rom

# Debug: clear CMP + test sector 0
python3 flasher.py --port /dev/ttyUSB0 debug
```

## Protocol

All commands are single ASCII characters. Multi-page operations (read/write) use a handshake:

- **Read**: Arduino prints `START` → sends 256-byte pages → waits for `N` ack between pages
- **Write**: Arduino prints `READY` → expects 256-byte pages → prints `NEXT` after each write

## Common Issues

| Symptom | Fix |
|---------|-----|
| Erase ignored (WEL clears, BUSY=0) | Run `debug` to clear CMP bit |
| All reads return 0x00 | Check clip connection / power |
| Write returns errors | Chip must be erased first |
| Arduino resets on connect | Use `ser.dtr = False` or 10µF cap on RESET |
