#ifndef TT_GAMEPAD_H
#define TT_GAMEPAD_H

#include <stdint.h>

namespace tt {

// Virtual gamepad for project 33 (Wafer.space VGA screensaver).
//
// The design's gamepad_pmod block is a PASSIVE listener: it samples
// pmod_data (ui_in[6]) on pmod_clk (ui_in[5]) RISING edges into a shift
// register and commits shift->data_reg on pmod_latch (ui_in[4]) RISING
// edge. Bit order (first sent = MSB of the 12-bit reg):
//   b, y, select, start, up, down, left, right, a, x, l, r
// Pressed = 1. The design acts on the buttons ONCE PER FRAME at the frame
// start: a START rising edge toggles manual mode; held d-pads move the
// logo 1 px/frame. Button state PERSISTS in the design's data_reg between
// bursts. The pins idle low between bursts (ui_in reads back 0 on bits
// 4-6; the design holds the latched state internally).
//
// MINIMUM HOLD: a press released faster than one design-frame period is
// INVISIBLE to the design (no frame sample lands inside the window).
// gamepad_apply() bursts pressed states immediately and arms a hold
// deadline (1.2x the design's frame period, adaptive to the chip clock);
// gamepad_service() (call from the main loop) sends deferred CHANGES -
// typically the release - only after the deadline. Every press/release
// pair is then exactly one visible toggle regardless of tap speed.

struct GamepadState {
    bool start, up, down, left, right;
};

// Record the desired state. Bursts immediately when any button is pressed;
// defers all-released (or changed) states to gamepad_service() after the
// hold deadline. Never call from an interrupt-masked window (busy_wait).
void gamepad_apply(const GamepadState& state);

// Send any deferred burst whose hold deadline has passed. Call from the
// main loop. Bounded: at most one ~60 us burst per call.
void gamepad_service();

}  // namespace tt

#endif  // TT_GAMEPAD_H
