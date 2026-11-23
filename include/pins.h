#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace tt {

// Mux and reset control signals (ESP32 outputs)
constexpr int PIN_CTRL_ENA = 18;        // SCK
constexpr int PIN_CTRL_SEL_INC = 23;    // MOSI
constexpr int PIN_CTRL_SEL_RST_N = 19;  // MISO
constexpr int PIN_CLK = 0;              // D5 (was SCL/GPIO22; moved to free hardware I2C)
constexpr int PIN_U_RST_N = 15;         // A4 (was SDA/GPIO21; moved to free hardware I2C)

// ui_in dedicated inputs (ESP32 outputs -> TT inputs).
// Only the three wired inputs are mapped; others are -1 (not connected).
constexpr int PIN_UI_IN[8] = {
    4,   // D12 -> ui_in[0] (tile)
    16,  // D11 -> ui_in[1] (solid_color)
    -1,
    -1,
    17,  // D10 -> ui_in[4] (gamepad_latch, held LOW)
    -1,
    -1,
    -1,
};

// uo_out data bus (TT outputs, ESP32 inputs)
constexpr int PIN_UO_OUT[8] = {
    35,  // A3  -> uo_out[0]
    34,  // A2  -> uo_out[1]
    39,  // A1  -> uo_out[2]
    36,  // A0  -> uo_out[3]
    13,  // D7  -> uo_out[4]
    14,  // D6  -> uo_out[5]
    26,  // D3  -> uo_out[6]
    25,  // D2  -> uo_out[7]
};

// Initialize all TT-related pins.
void init_pins();

// Read the 8-bit uo_out bus.
uint8_t read_uo_out();

// Control signal helpers (active-high except where noted).
void set_ctrl_ena(bool high);
void set_ctrl_sel_inc(bool high);
void set_ctrl_sel_rst_n(bool high);  // active-low on the wire
void set_u_rst_n(bool high);          // active-low on the wire

// ui_in dedicated inputs (ESP32 -> TT). Wired bits drive native GPIOs;
// the MCP23017 (if present) drives all 8 bits.
void set_ui_in(uint8_t value);
uint8_t ui_in_value();

// uio bidirectional pins (via MCP23017 when present).
void set_uio_direction(uint8_t output_mask);
uint8_t uio_direction();
void set_uio_value(uint8_t value);
uint8_t uio_value();

// Print current pin states to Serial (for debugging).
void print_pin_diagnostics();

}  // namespace tt
