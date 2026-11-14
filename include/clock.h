#pragma once

#include <stdint.h>

namespace tt {

// Start a free-running clock on PIN_CLK at the requested frequency.
// freq_hz is clamped to a hardware-safe range (1 Hz .. 40 MHz).
void clock_start(uint32_t freq_hz);

// Stop the free-running clock and hold the pin low.
void clock_stop();

// Bit-bang exactly one full clock cycle. The half-period is short but
// slow enough to be visible on a scope/LED (~10 µs each half).
void clock_step();

// True if the LEDC clock is currently running.
bool clock_running();

// Return the current clock frequency, or 0 if stopped.
uint32_t clock_frequency();

// Return the last configured clock frequency (set by clock_start()), even if
// the clock is currently stopped. Returns 0 if no frequency has been set.
uint32_t clock_target_frequency();

}  // namespace tt
