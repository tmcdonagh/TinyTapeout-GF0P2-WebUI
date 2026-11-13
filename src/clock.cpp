#include "clock.h"
#include "pins.h"
#include <Arduino.h>
#include <driver/gpio.h>

namespace tt {

namespace {
    bool g_running = false;
    uint32_t g_freq_hz = 0;
    hw_timer_t* g_timer = nullptr;
    volatile bool g_clk_state = false;

    // Minimum/maximum frequencies for the timer-based clock.
    // The timer ticks at 1 MHz, so the practical maximum is around 200 kHz
    // (alarm period of a few ticks).  Keep the ceiling low enough to be
    // reliable while still useful for manual/visual mux testing.
    constexpr uint32_t MIN_FREQ = 10;
    constexpr uint32_t MAX_FREQ = 100000;

    uint32_t clamp_freq(uint32_t freq_hz) {
        if (freq_hz < MIN_FREQ) return MIN_FREQ;
        if (freq_hz > MAX_FREQ) return MAX_FREQ;
        return freq_hz;
    }

    void IRAM_ATTR clock_isr() {
        g_clk_state = !g_clk_state;
        gpio_set_level(static_cast<gpio_num_t>(PIN_CLK), g_clk_state ? 1 : 0);
    }
}

void clock_start(uint32_t freq_hz) {
    // Stop any existing clock first so the pin is under GPIO control.
    clock_stop();

    g_freq_hz = clamp_freq(freq_hz);

    // Timer ticks at 1 MHz.  Alarm fires every half period (toggle mode).
    g_timer = timerBegin(1000000);
    timerAttachInterrupt(g_timer, clock_isr);
    uint64_t alarm = 1000000ULL / (2 * g_freq_hz);
    if (alarm < 2) alarm = 2;  // keep a small margin for ISR overhead
    timerAlarm(g_timer, alarm, true, 0);
    timerStart(g_timer);

    g_running = true;
}

void clock_stop() {
    if (g_running && g_timer != nullptr) {
        timerStop(g_timer);
        timerDetachInterrupt(g_timer);
        timerEnd(g_timer);
        g_timer = nullptr;
        g_running = false;
    }

    // Ensure the pin is GPIO-driven and held low with strong drive so the
    // TT card sees a clean edge after the timer peripheral releases it.
    g_clk_state = false;
    gpio_reset_pin(static_cast<gpio_num_t>(PIN_CLK));
    gpio_set_direction(static_cast<gpio_num_t>(PIN_CLK), GPIO_MODE_OUTPUT);
    gpio_set_drive_capability(static_cast<gpio_num_t>(PIN_CLK), GPIO_DRIVE_CAP_3);
    gpio_set_level(static_cast<gpio_num_t>(PIN_CLK), 0);
}

void clock_step() {
    // Stop any free-running clock and take manual control of the pin.
    clock_stop();

    constexpr unsigned long half_period_us = 10;
    gpio_set_level(static_cast<gpio_num_t>(PIN_CLK), 1);
    delayMicroseconds(half_period_us);
    gpio_set_level(static_cast<gpio_num_t>(PIN_CLK), 0);
    delayMicroseconds(half_period_us);
}

bool clock_running() {
    return g_running;
}

uint32_t clock_frequency() {
    return g_running ? g_freq_hz : 0;
}

}  // namespace tt
