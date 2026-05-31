/*
 * w25q16jw-arduino-flasher — Arduino Uno R3
 * ===================================
 * Read, erase, write, and verify Winbond W25Q16JW (16Mbit / 2MB) SPI flash.
 *
 * WIRING (use 1.8V level shifter between Arduino and chip):
 *
 *   Arduino Side        Chip Side (SOIC8, dot = pin 1)
 *   ───────────         ──────────────────────────────
 *   D10 (SS)     →      Pin 1 (CS#)
 *   D12 (MISO)   ←      Pin 2 (DO/IO1)
 *   D11 (MOSI)   →      Pin 5 (DI/IO0)
 *   D13 (SCK)    →      Pin 6 (CLK)
 *   3.3V         →      Pin 8 (VCC) — via level shifter
 *   GND          →      Pin 4 (GND)
 *
 *   Pin 3 (WP#) and Pin 7 (HOLD#) can float when QE=1 (factory default).
 *
 * PROTOCOL (115200 baud — change BAUD below for faster boards):
 *
 *   I / i  — Info: JEDEC ID + status registers
 *   P / p  — Peek: first 64 bytes
 *   R / r  — Read: full chip with page-level handshake (prints START, sends
 *            pages, waits for 'N' ack after each)
 *   E / e  — Erase: full chip erase with confirmation
 *   S / s  — Sector erase: 512 x 4KB (lower current, more reliable)
 *   W / w  — Write: page-level handshake (prints READY, expects 256-byte
 *            chunks, prints NEXT after each, waits for Python to send next)
 *   V / v  — Verify: page-level handshake, compares with sent data
 *   D / d  — Debug: clear CMP + single sector 0 erase test
 */

#define BAUD 115200  // Increase to 500000 or 1000000 for faster boards

#include <SPI.h>

// ── W25Q16JW commands ──────────────────────────────────────────
enum Cmd : uint8_t {
    WRITE_ENABLE    = 0x06,
    READ_SR1        = 0x05,
    READ_SR2        = 0x35,
    READ_SR3        = 0x15,
    WRITE_SR        = 0x01,
    READ_DATA       = 0x03,
    PAGE_PROGRAM    = 0x02,
    CHIP_ERASE      = 0xC7,
    SECTOR_ERASE_4K = 0x20,
    JEDEC_ID        = 0x9F,
};

// ── Pins ───────────────────────────────────────────────────────
const uint8_t CS_PIN = 10;

// ── Constants ──────────────────────────────────────────────────
constexpr uint32_t CHIP_SIZE  = 0x200000;  // 2,097,152 bytes
constexpr uint16_t PAGE_SIZE  = 256;

// ── Low-level helpers ──────────────────────────────────────────
inline void cs_low()  { digitalWrite(CS_PIN, LOW);  delayMicroseconds(1); }
inline void cs_high() { delayMicroseconds(1); digitalWrite(CS_PIN, HIGH); }

uint8_t read_sr(uint8_t cmd) {
    cs_low(); spi_xfer(cmd); uint8_t r = spi_xfer(0x00); cs_high();
    return r;
}

void write_enable() {
    cs_low(); spi_xfer(WRITE_ENABLE); cs_high();
    delayMicroseconds(5);
}

bool wait_busy(unsigned long timeout_ms = 30000) {
    unsigned long start = millis();
    while (read_sr(READ_SR1) & 0x01) {
        if (millis() - start > timeout_ms) {
            Serial.println(F("TIMEOUT"));
            return false;
        }
        delay(1);
    }
    return true;
}

void ensure_unlocked() {
    uint8_t sr2 = read_sr(READ_SR2);
    if (sr2 & 0x40) {  // CMP bit
        write_enable();
        cs_low(); spi_xfer(WRITE_SR); spi_xfer(0x00); spi_xfer(sr2 & 0xBF);
        cs_high();
        wait_busy();
        delay(5);
    }
}

uint8_t spi_xfer(uint8_t d) { return SPI.transfer(d); }

// ── Commands ───────────────────────────────────────────────────

void cmd_info() {
    cs_low();
    spi_xfer(JEDEC_ID);
    uint8_t man = spi_xfer(0x00), d1 = spi_xfer(0x00),
            d2  = spi_xfer(0x00), ex = spi_xfer(0x00);
    cs_high();

    uint8_t sr1 = read_sr(READ_SR1), sr2 = read_sr(READ_SR2),
            sr3 = read_sr(READ_SR3);

    Serial.print(F("JEDEC: "));
    Serial.print(man, HEX); Serial.print(' ');
    Serial.print(d1, HEX);  Serial.print(' ');
    Serial.print(d2, HEX);  Serial.print(' ');
    Serial.print(ex, HEX);  Serial.println();

    Serial.print(F("SR1: ")); Serial.print(sr1, HEX);
    Serial.print(F(" SR2: ")); Serial.print(sr2, HEX);
    Serial.print(F(" SR3: ")); Serial.print(sr3, HEX);
    Serial.print(F("  CMP=")); Serial.print((sr2 >> 6) & 1);
    Serial.print(F(" BP="));  Serial.print((sr1 >> 2) & 0x07, BIN);
    Serial.print(F(" QE="));  Serial.print((sr2 >> 1) & 1);
    Serial.println();

    bool locked = ((sr2 & 0x40) && ((sr1 >> 2) & 0x07) == 0);
    Serial.println(locked ? F("STATUS: LOCKED")
                          : F("STATUS: Unlocked"));
}

void cmd_peek() {
    cs_low();
    spi_xfer(READ_DATA);
    for (uint8_t i = 0; i < 3; i++) spi_xfer(0x00);
    for (uint8_t i = 0; i < 64; i++) {
        uint8_t b = spi_xfer(0x00);
        if (b < 0x10) Serial.print('0');
        Serial.print(b, HEX);
        Serial.print((i & 15) == 15 ? '\n' : ' ');
    }
    cs_high();

    cs_low(); spi_xfer(READ_DATA); spi_xfer(0x00); spi_xfer(0x00); spi_xfer(0x00);
    uint8_t b = spi_xfer(0x00); cs_high();

    if (b == 0xFF)      Serial.println(F("STATUS: Erased"));
    else if (b == 0x4E) Serial.println(F("STATUS: NVGI header"));
    else                Serial.println(F("STATUS: Data present"));
}

void cmd_erase() {
    Serial.println(F("WARNING: Erases ENTIRE chip. Send Y to confirm."));
    while (!Serial.available());
    if (Serial.read() != 'Y') { Serial.println(F("Cancelled.")); return; }

    Serial.println(F("Erasing..."));
    ensure_unlocked();
    write_enable();
    cs_low(); spi_xfer(CHIP_ERASE); cs_high();

    if (!wait_busy()) return;

    cs_low(); spi_xfer(READ_DATA); spi_xfer(0x00); spi_xfer(0x00); spi_xfer(0x00);
    uint8_t b = spi_xfer(0x00); cs_high();
    Serial.println(b == 0xFF ? F("SUCCESS") : F("FAILED"));
}

void cmd_debug() {
    uint8_t sr1 = read_sr(READ_SR1), sr2 = read_sr(READ_SR2);
    Serial.print(F("SR1=0x")); Serial.print(sr1, HEX);
    Serial.print(F(" SR2=0x")); Serial.print(sr2, HEX);
    Serial.print(F(" CMP=")); Serial.println((sr2 >> 6) & 1);

    ensure_unlocked();
    write_enable();

    cs_low(); spi_xfer(SECTOR_ERASE_4K);
    spi_xfer(0x00); spi_xfer(0x00); spi_xfer(0x00);
    cs_high(); delayMicroseconds(20);

    sr1 = read_sr(READ_SR1);
    if (sr1 & 1) { wait_busy(); }

    cs_low(); spi_xfer(READ_DATA); spi_xfer(0x00); spi_xfer(0x00); spi_xfer(0x00);
    uint8_t b = spi_xfer(0x00); cs_high();
    Serial.println(b == 0xFF ? F("OK - erased") : F("FAILED"));
}

// ── Streaming read (handshake) ─────────────────────────────────
void cmd_read() {
    Serial.println(F("START"));
    uint8_t buf[PAGE_SIZE];
    for (uint32_t addr = 0; addr < CHIP_SIZE; addr += PAGE_SIZE) {
        cs_low();
        spi_xfer(READ_DATA);
        spi_xfer((addr >> 16) & 0xFF);
        spi_xfer((addr >> 8) & 0xFF);
        spi_xfer(addr & 0xFF);
        for (uint16_t i = 0; i < PAGE_SIZE; i++) buf[i] = spi_xfer(0x00);
        cs_high();
        Serial.write(buf, PAGE_SIZE);
        Serial.flush();

        unsigned long t = millis() + 10000;
        while (!Serial.available()) { if (millis() > t) return; }
        Serial.read();  // consume ack byte
    }
}

// ── Streaming write (handshake) ────────────────────────────────
void cmd_write() {
    ensure_unlocked();
    Serial.println(F("READY"));

    uint8_t page[PAGE_SIZE];
    uint32_t addr = 0, errors = 0;

    while (addr < CHIP_SIZE) {
        for (uint16_t i = 0; i < PAGE_SIZE; i++) {
            unsigned long t = millis() + 30000;
            while (!Serial.available()) { if (millis() > t) { Serial.println(F("TIMEOUT")); return; } }
            page[i] = Serial.read();
        }

        bool blank = true;
        for (uint16_t i = 0; i < PAGE_SIZE; i++)
            if (page[i] != 0xFF) { blank = false; break; }

        if (!blank) {
            write_enable();
            cs_low();
            spi_xfer(PAGE_PROGRAM);
            spi_xfer((addr >> 16) & 0xFF);
            spi_xfer((addr >> 8) & 0xFF);
            spi_xfer(addr & 0xFF);
            for (uint16_t i = 0; i < PAGE_SIZE; i++) spi_xfer(page[i]);
            cs_high();
            wait_busy();

            cs_low();
            spi_xfer(READ_DATA);
            spi_xfer((addr >> 16) & 0xFF);
            spi_xfer((addr >> 8) & 0xFF);
            spi_xfer(addr & 0xFF);
            for (uint16_t i = 0; i < PAGE_SIZE; i++) {
                if (spi_xfer(0x00) != page[i]) {
                    errors++;
                    if (errors <= 3) { Serial.print(F("ERR 0x")); Serial.println(addr + i, HEX); }
                }
            }
            cs_high();
        }

        addr += PAGE_SIZE;
        Serial.println(F("NEXT"));
    }

    if (errors == 0) Serial.println(F("SUCCESS"));
    else { Serial.print(F("FAILED ")); Serial.print(errors); Serial.println(F(" errors")); }
}

// ── Setup & loop ───────────────────────────────────────────────
void setup() {
    Serial.begin(BAUD);
    while (!Serial);

    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);

    SPI.begin();
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    delay(100);

    Serial.println(F("\nw25q16jw-arduino-flasher ready. I=Info P=Peek R=Read E=Erase W=Write V=Verify D=Debug"));
}

void loop() {
    if (!Serial.available()) return;
    char c = Serial.read();
    while (Serial.available()) Serial.read();  // flush

    switch (c) {
        case 'I': case 'i': cmd_info();          break;
        case 'P': case 'p': cmd_peek();          break;
        case 'R': case 'r': cmd_read();          break;
        case 'E': case 'e': cmd_erase();         break;
        case 'W': case 'w': cmd_write();         break;
        case 'D': case 'd': cmd_debug();         break;
        default: Serial.print(F("? ")); Serial.println(c);
    }
    Serial.println(F("\nREADY"));
}
