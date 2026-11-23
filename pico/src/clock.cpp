#include "clock.h"
#include "pins.h"
#include <Arduino.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>

namespace tt {

namespace {

bool g_running = false;
uint32_t g_freq_hz = 0;
uint32_t g_target_freq_hz = 0;

// Floor matches the web UI/ESP32 firmware (and a PWM slice cannot go much
// lower anyway: 150 MHz / (256 x 65536) ~= 8.9 Hz). Ceiling: quarter of the
// 100 MHz TT pad guideline, comfortably above the 25.175 MHz VGA rate.
constexpr uint32_t MIN_FREQ = 10;
constexpr uint32_t MAX_FREQ = 25000000;
constexpr uint32_t SYS_HZ = 150000000;

// Above this frequency a divider of 1 with a 16-bit wrap is exact to <1 tick.
constexpr uint32_t DIV1_MIN_FREQ = SYS_HZ / 65536 + 1;  // ~2289 Hz

uint32_t clamp_freq(uint32_t freq_hz) {
    if (freq_hz < MIN_FREQ) return MIN_FREQ;
    if (freq_hz > MAX_FREQ) return MAX_FREQ;
    return freq_hz;
}

}  // namespace

void clock_start(uint32_t freq_hz) {
    // Stop any existing clock first so the pin is under GPIO control.
    clock_stop();

    g_target_freq_hz = freq_hz;
    uint32_t f = clamp_freq(freq_hz);

    // Frequency = SYS_HZ / (div * (wrap+1)), 50% duty.
    // - f >= ~2289 Hz: div = 1, wrap = round(SYS/f) - 1 (exact to <1 tick).
    // - below: wrap = 65535, fractional divider carries the rest
    //   (1/16 step -> worst-case ~0.8% error around 1 kHz, negligible at
    //   tens of Hz where demo clocks live).
    uint8_t int_div = 1;
    uint8_t frac_div = 0;
    uint16_t wrap;
    if (f >= DIV1_MIN_FREQ) {
        wrap = static_cast<uint16_t>((SYS_HZ + f / 2) / f - 1);
        g_freq_hz = SYS_HZ / (static_cast<uint32_t>(wrap) + 1);
    } else {
        wrap = 65535;
        float div = static_cast<float>(SYS_HZ) /
                    (static_cast<float>(f) * 65536.0f);
        uint32_t scaled = static_cast<uint32_t>(div * 16.0f + 0.5f);
        int_div = static_cast<uint8_t>(scaled >> 4);
        frac_div = static_cast<uint8_t>(scaled & 0xF);
        if (int_div == 0) int_div = 1;  // unreachable given MIN_FREQ, kept safe
        float actual_div = int_div + frac_div / 16.0f;
        g_freq_hz = static_cast<uint32_t>(
            SYS_HZ / (actual_div * 65536.0f));
    }

    uint slice = pwm_gpio_to_slice_num(PIN_CLK);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&cfg, int_div, frac_div);
    pwm_config_set_wrap(&cfg, wrap);
    pwm_init(slice, &cfg, false);
    pwm_set_chan_level(slice, PWM_CHAN_A,
                       static_cast<uint>(wrap + 1) / 2);  // ~50% duty
    pwm_set_enabled(slice, true);
    gpio_set_function(PIN_CLK, GPIO_FUNC_PWM);

    g_running = true;
}

void clock_stop() {
    if (g_running) {
        uint slice = pwm_gpio_to_slice_num(PIN_CLK);
        pwm_set_enabled(slice, false);
        g_running = false;
    }

    // Ensure the pin is GPIO-driven and held low so the TT card sees a
    // clean edge after the PWM peripheral releases it.
    gpio_set_function(PIN_CLK, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_CLK, GPIO_OUT);
    gpio_put(PIN_CLK, 0);
}

void clock_step() {
    // Stop any free-running clock and take manual control of the pin.
    clock_stop();

    constexpr unsigned long half_period_us = 10;
    gpio_put(PIN_CLK, 1);
    delayMicroseconds(half_period_us);
    gpio_put(PIN_CLK, 0);
    delayMicroseconds(half_period_us);
}

bool clock_running() {
    return g_running;
}

uint32_t clock_frequency() {
    return g_running ? g_freq_hz : 0;
}

uint32_t clock_target_frequency() {
    return g_target_freq_hz;
}

}  // namespace tt
