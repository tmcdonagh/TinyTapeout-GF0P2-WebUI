#pragma once

#include <stdint.h>

namespace tt {

// Free-running adjustable clock on PIN_CLK (PWM slice, 10 Hz - 25 MHz).
void clock_start(uint32_t freq_hz);
void clock_stop();
void clock_step();
bool clock_running();
uint32_t clock_frequency();
uint32_t clock_target_frequency();

}  // namespace tt
