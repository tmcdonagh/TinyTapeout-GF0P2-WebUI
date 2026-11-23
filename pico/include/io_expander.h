#pragma once

#include <stdint.h>

namespace tt {

// MCP23017 (Waveshare X00332 board) on I2C1: GP26 = SDA, GP27 = SCL.
// Port A is unused on the Pico host (ui_in is fully direct GPIO); port B
// carries uio[0:7]. The board defaults to address 0x27 (jumpers open);
// init probes 0x27 first, then 0x20.

bool io_expander_init();
bool io_expander_present();

// Low-level register access mirroring the ESP32 io_expander API.
void uio_set_direction(uint8_t output_mask);
void uio_write(uint8_t value);
uint8_t uio_read();

// High-level cached wrappers (the ESP32 pins.h API surface, consumed by the
// web server and the serial console).
void set_uio_direction(uint8_t output_mask);
uint8_t uio_direction();
void set_uio_value(uint8_t value);
uint8_t uio_value();

void print_expander_diagnostics();

}  // namespace tt
