#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace tt {

// Control outputs (Pico -> TTGF0P2)
constexpr int PIN_CTRL_ENA = 11;        // physical pin 15
constexpr int PIN_CTRL_SEL_INC = 12;    // physical pin 16
constexpr int PIN_CTRL_SEL_RST_N = 13;  // physical pin 17
constexpr int PIN_CLK = 10;             // physical pin 14
constexpr int PIN_U_RST_N = 14;          // physical pin 19

// uo_out data bus (TT -> Pico), contiguous GP2..GP9 so a single
// gpio_get_all() shift latches the whole byte.
constexpr int PIN_UO_OUT_BASE = 2;
constexpr int PIN_UO_OUT[8] = {2, 3, 4, 5, 6, 7, 8, 9};

// ui_in dedicated inputs (Pico -> TT), all 8 bits direct on GP15..GP22.
constexpr int PIN_UI_IN[8] = {15, 16, 17, 18, 19, 20, 21, 22};

// Initialize all TT-related pins.
void init_pins();

// Read the 8-bit uo_out bus (one register snapshot).
uint8_t read_uo_out();

// Control signal helpers (active-high except where noted).
void set_ctrl_ena(bool high);
void set_ctrl_sel_inc(bool high);
void set_ctrl_sel_rst_n(bool high);  // active-low on the wire
void set_u_rst_n(bool high);          // active-low on the wire

// ui_in dedicated inputs (Pico -> TT), all 8 bits direct.
void set_ui_in(uint8_t value);
uint8_t ui_in_value();

// Print current pin states to Serial (for debugging).
void print_pin_diagnostics();

}  // namespace tt
