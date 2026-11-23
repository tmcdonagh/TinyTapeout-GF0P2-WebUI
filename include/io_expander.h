#pragma once

#include <stdint.h>

namespace tt {

// MCP23017 16-bit I2C IO expander driver.
// GPIOA[0:7] -> ui_in[0:7] (outputs to Tiny Tapeout)
// GPIOB[0:7] -> uio[0:7]  (bidirectional; direction set per project)

// Initialize the MCP23017 on the default I2C bus.
// Returns true if the chip responded at address 0x20.
bool io_expander_init();

// True if the MCP23017 was detected during init.
bool io_expander_present();

// Write all 8 ui_in bits to GPIOA.
void ui_in_write(uint8_t value);

// Set GPIOB direction: 1 = output, 0 = input.
void uio_set_direction(uint8_t output_mask);

// Write GPIOB output bits (only affects pins configured as outputs).
void uio_write(uint8_t value);

// Read all 8 GPIOB pins.
uint8_t uio_read();

}  // namespace tt
