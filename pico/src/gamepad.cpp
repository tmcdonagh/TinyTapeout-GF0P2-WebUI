#include "gamepad.h"
#include "pins.h"
#include "clock.h"
#include "mux.h"
#include "vga_capture.h"

#include <Arduino.h>

namespace tt {

namespace {

// Burst timing: data SETUP before the clk rising edge (sampled there),
// latch rising AFTER bit 12 commits the shift register. 3 us pulses =
// >= 3 chip-clock periods at any chip clock >= 1 MHz - comfortable for
// the design's 2-FF synchronizers.
constexpr unsigned long PULSE_US = 3;

inline void clk_high() { digitalWrite(PIN_UI_IN[5], HIGH); }
inline void clk_low() { digitalWrite(PIN_UI_IN[5], LOW); }
inline void latch_high() { digitalWrite(PIN_UI_IN[4], HIGH); }
inline void latch_low() { digitalWrite(PIN_UI_IN[4], LOW); }
inline void data_put(bool v) { digitalWrite(PIN_UI_IN[6], v ? HIGH : LOW); }

GamepadState g_target = {false, false, false, false, false};
GamepadState g_sent = {false, false, false, false, false};
bool g_dirty = false;
unsigned long g_hold_until_ms = 0;

bool state_equal(const GamepadState& a, const GamepadState& b) {
    return a.start == b.start && a.up == b.up && a.down == b.down &&
           a.left == b.left && a.right == b.right;
}

bool any_pressed(const GamepadState& s) {
    return s.start || s.up || s.down || s.left || s.right;
}

// Chip clock currently in effect (capture rate while the engine drives
// the pin, free-run clock otherwise, 1 MHz floor).
uint32_t chip_clock_hz() {
    if (clock_running()) return clock_frequency() > 0 ? clock_frequency() : 1000000;
    if (current_project() == 33 && digitalRead(PIN_U_RST_N) == HIGH) {
        return vga_capture_clock_hz();
    }
    return 1000000;
}

// Hold long enough that at least one design frame sample lands inside the
// pressed window: 1.2x the frame period (420k clocks/frame), clamped.
unsigned long hold_ms() {
    uint32_t hz = chip_clock_hz();
    if (hz < 100000) hz = 100000;
    uint32_t ms = 420000UL * 1200UL / hz;  // 1.2 frame periods in ms
    if (ms < 25) ms = 25;
    if (ms > 600) ms = 600;
    return ms;
}

void burst(const GamepadState& state) {
    const bool bits[12] = {
        false,        // b
        false,        // y
        false,        // select
        state.start,  // start
        state.up,     // up
        state.down,   // down
        state.left,   // left
        state.right,  // right
        false,        // a
        false,        // x
        false,        // l
        false,        // r
    };
    data_put(false);
    clk_low();
    latch_low();
    busy_wait_us(PULSE_US);
    for (int i = 0; i < 12; ++i) {
        data_put(bits[i]);
        busy_wait_us(PULSE_US);  // data setup before the rising edge
        clk_high();
        busy_wait_us(PULSE_US);  // sampled here (rising edge)
        clk_low();
    }
    latch_high();
    busy_wait_us(PULSE_US);  // shift -> data_reg commits here
    latch_low();
    busy_wait_us(PULSE_US);
}

}  // namespace

void gamepad_apply(const GamepadState& state) {
    g_target = state;
    if (any_pressed(state)) {
        // Immediate burst + extend the hold: even a sub-frame tap stays
        // visible for at least one design frame sample.
        burst(state);
        g_sent = state;
        g_dirty = false;
        g_hold_until_ms = millis() + hold_ms();
    } else {
        g_dirty = !state_equal(g_target, g_sent);
        // Releases wait for the hold deadline in gamepad_service().
    }
}

void gamepad_service() {
    if (!g_dirty) return;
    if (static_cast<int32_t>(millis() - g_hold_until_ms) < 0) return;
    burst(g_target);
    g_sent = g_target;
    g_dirty = false;
}

}  // namespace tt
