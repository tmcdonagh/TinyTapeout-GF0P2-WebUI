#include "io_expander.h"
#include <Arduino.h>
#include <Wire.h>

namespace tt {

namespace {

// Waveshare default (A0/A1/A2 pulled high), fallback for bridged boards.
constexpr uint8_t ADDR_DEFAULT = 0x27;
constexpr uint8_t ADDR_ALT = 0x20;
uint8_t g_addr = ADDR_DEFAULT;

constexpr uint32_t I2C_FREQ = 400000;

// MCP23017 register addresses (BANK=0, the default).
constexpr uint8_t REG_IODIRA = 0x00;
constexpr uint8_t REG_IODIRB = 0x01;
constexpr uint8_t REG_IPOLA = 0x02;
constexpr uint8_t REG_IPOLB = 0x03;
constexpr uint8_t REG_GPPUA = 0x0C;
constexpr uint8_t REG_GPPUB = 0x0D;
constexpr uint8_t REG_GPIOA = 0x12;
constexpr uint8_t REG_GPIOB = 0x13;
constexpr uint8_t REG_OLATA = 0x14;
constexpr uint8_t REG_OLATB = 0x15;

bool g_present = false;
uint8_t g_uio_value = 0;
uint8_t g_uio_dir = 0;

bool write_reg(uint8_t reg, uint8_t value) {
    Wire1.beginTransmission(g_addr);
    Wire1.write(reg);
    Wire1.write(value);
    return Wire1.endTransmission() == 0;
}

bool read_reg(uint8_t reg, uint8_t& value) {
    Wire1.beginTransmission(g_addr);
    Wire1.write(reg);
    if (Wire1.endTransmission() != 0) return false;
    if (Wire1.requestFrom(static_cast<int>(g_addr), 1) != 1) return false;
    value = static_cast<uint8_t>(Wire1.read());
    return true;
}

}  // namespace

bool io_expander_init() {
    Wire1.setSDA(26);  // GP26 = I2C1 SDA
    Wire1.setSCL(27);  // GP27 = I2C1 SCL
    Wire1.begin();
    Wire1.setClock(I2C_FREQ);
    // Bounded waits WITH bus recovery: on a timeout the core's handler
    // pulses SCL up to 9x to release a slave stuck mid-transaction (the
    // software equivalent of the power cycle) and re-inits the
    // peripheral. Without reset_with_timeout=true a wedged MCP23017
    // stays wedged until the next unplug.
    Wire1.setTimeout(25, true);

    // Probe: try the Waveshare default address first, then the
    // solder-bridged alternative.
    uint8_t iodirb = 0;
    g_addr = ADDR_DEFAULT;
    g_present = read_reg(REG_IODIRB, iodirb);
    if (!g_present) {
        g_addr = ADDR_ALT;
        g_present = read_reg(REG_IODIRB, iodirb);
    }
    if (!g_present) {
        Serial.println("[io_expander] MCP23017 not detected");
        return false;
    }

    Serial.print("[io_expander] MCP23017 detected at 0x");
    Serial.println(g_addr, HEX);

    // Port A unused on this host: leave it as inputs (power-on default).
    write_reg(REG_IODIRA, 0xFF);

    // Port B -> uio, all inputs by default.
    write_reg(REG_IODIRB, 0xFF);
    write_reg(REG_GPIOB, 0x00);
    write_reg(REG_OLATB, 0x00);

    // No input polarity inversion, no pull-ups.
    write_reg(REG_IPOLA, 0x00);
    write_reg(REG_IPOLB, 0x00);
    write_reg(REG_GPPUA, 0x00);
    write_reg(REG_GPPUB, 0x00);

    g_uio_value = 0;
    g_uio_dir = 0;
    return true;
}

bool io_expander_present() {
    return g_present;
}

void uio_set_direction(uint8_t output_mask) {
    if (!g_present) return;
    // Direction: 1 = output. MCP23017 IODIR: 1 = input, 0 = output.
    write_reg(REG_IODIRB, static_cast<uint8_t>(~output_mask));
}

void uio_write(uint8_t value) {
    if (!g_present) return;
    write_reg(REG_GPIOB, value);
}

uint8_t uio_read() {
    if (!g_present) return g_uio_value;
    uint8_t value = 0;
    if (read_reg(REG_GPIOB, value)) {
        g_uio_value = value;
    }
    return g_uio_value;
}

void set_uio_direction(uint8_t output_mask) {
    g_uio_dir = output_mask;
    uio_set_direction(output_mask);
}

uint8_t uio_direction() {
    return g_uio_dir;
}

void set_uio_value(uint8_t value) {
    g_uio_value = value;
    uio_write(value);
}

uint8_t uio_value() {
    g_uio_value = uio_read();
    return g_uio_value;
}

void print_expander_diagnostics() {
    if (!g_present) {
        Serial.println("expander=none");
        return;
    }
    Serial.print("expander=0x");
    Serial.print(g_addr, HEX);
    Serial.print(" uio_dir=0x");
    Serial.print(g_uio_dir, HEX);
    Serial.print(" uio_val=0x");
    Serial.println(g_uio_value, HEX);
}

}  // namespace tt
