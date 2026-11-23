#include "io_expander.h"
#include <Arduino.h>
#include <Wire.h>

namespace tt {

namespace {

constexpr uint8_t MCP23017_ADDR = 0x20;
constexpr uint32_t I2C_FREQ = 400000;

// MCP23017 register addresses (BANK=0, the default).
constexpr uint8_t REG_IODIRA   = 0x00;
constexpr uint8_t REG_IODIRB   = 0x01;
constexpr uint8_t REG_IPOLA    = 0x02;
constexpr uint8_t REG_IPOLB    = 0x03;
constexpr uint8_t REG_GPPUA    = 0x0C;
constexpr uint8_t REG_GPPUB    = 0x0D;
constexpr uint8_t REG_GPIOA    = 0x12;
constexpr uint8_t REG_GPIOB    = 0x13;
constexpr uint8_t REG_OLATA    = 0x14;
constexpr uint8_t REG_OLATB    = 0x15;

bool g_present = false;
uint8_t g_ui_in_value = 0;
uint8_t g_uio_value = 0;
uint8_t g_uio_dir = 0;

bool write_reg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(MCP23017_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool read_reg(uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(MCP23017_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(static_cast<int>(MCP23017_ADDR), 1) != 1) return false;
    value = static_cast<uint8_t>(Wire.read());
    return true;
}

}  // namespace

bool io_expander_init() {
    Wire.begin();
    Wire.setClock(I2C_FREQ);

    // Probe the device by reading IODIRA.
    uint8_t iodira = 0;
    g_present = read_reg(REG_IODIRA, iodira);
    if (!g_present) {
        Serial.println("[io_expander] MCP23017 not detected");
        return false;
    }

    Serial.print("[io_expander] MCP23017 detected at 0x");
    Serial.println(MCP23017_ADDR, HEX);

    // GPIOA -> ui_in, all outputs.
    write_reg(REG_IODIRA, 0x00);
    write_reg(REG_GPIOA, 0x00);
    write_reg(REG_OLATA, 0x00);

    // GPIOB -> uio, all inputs by default.
    write_reg(REG_IODIRB, 0xFF);
    write_reg(REG_GPIOB, 0x00);
    write_reg(REG_OLATB, 0x00);

    // No input polarity inversion, no pull-ups.
    write_reg(REG_IPOLA, 0x00);
    write_reg(REG_IPOLB, 0x00);
    write_reg(REG_GPPUA, 0x00);
    write_reg(REG_GPPUB, 0x00);

    g_ui_in_value = 0;
    g_uio_value = 0;
    g_uio_dir = 0;
    return true;
}

bool io_expander_present() {
    return g_present;
}

void ui_in_write(uint8_t value) {
    if (!g_present) return;
    g_ui_in_value = value;
    write_reg(REG_GPIOA, value);
}

void uio_set_direction(uint8_t output_mask) {
    if (!g_present) return;
    g_uio_dir = output_mask;
    // Direction: 1 = output, 0 = input. MCP23017 IODIR: 1 = input, 0 = output.
    write_reg(REG_IODIRB, static_cast<uint8_t>(~output_mask));
}

void uio_write(uint8_t value) {
    if (!g_present) return;
    g_uio_value = value;
    write_reg(REG_GPIOB, value);
}

uint8_t uio_read() {
    if (!g_present) return 0;
    uint8_t value = 0;
    if (read_reg(REG_GPIOB, value)) {
        g_uio_value = value;
    }
    return g_uio_value;
}

}  // namespace tt
