#include "mux.h"
#include "pins.h"
#include "vga_capture.h"

namespace tt {

namespace {
    uint8_t g_current_project = 0;

    constexpr uint8_t MAX_PROJECT = 51;

    // Pulse the pin high then return it low. The TT mux increments on the
    // rising edge of ctrl_sel_inc, and the pin should rest low afterwards.
    // Timings match the Tiny Tapeout reference Pico firmware.
    void pulse_high(int pin) {
        digitalWrite(pin, LOW);
        delayMicroseconds(10);
        digitalWrite(pin, HIGH);
        delayMicroseconds(100);
        digitalWrite(pin, LOW);
        delayMicroseconds(100);
    }
}

void select_project(uint8_t project) {
    if (project > MAX_PROJECT) {
        project = MAX_PROJECT;
    }

    // Match the Tiny Tapeout GF0p2 guide bring-up pseudocode:
    //   1. ctrl_sel_rst_n = HIGH
    //   2. ctrl_sel_inc = LOW
    //   3. pulse ctrl_sel_rst_n LOW-HIGH
    //   4. ctrl_ena = LOW
    //   5. pulse ctrl_sel_inc N times (if N > 0)
    //   6. u_rst_n = LOW
    //   7. ctrl_ena = HIGH
    // u_rst_n stays LOW so the chip ROM auto-increments on each clk edge.
    set_ctrl_sel_rst_n(true);
    set_ctrl_sel_inc(false);
    delayMicroseconds(100);

    set_ctrl_sel_rst_n(false);
    delay(1);
    set_ctrl_sel_rst_n(true);
    delay(1);

    set_ctrl_ena(false);
    delayMicroseconds(100);

    for (uint8_t i = 0; i < project; ++i) {
        pulse_high(PIN_CTRL_SEL_INC);
    }

    if (project == 33) {
        // VGA screensaver project: release reset and default to tiled mode so
        // the logo fills the frame.  Request a cached frame capture so the web
        // UI can serve /api/frame.bmp immediately.
        set_ui_in(0x01);
        set_u_rst_n(true);
        vga_cache_request();
    } else {
        set_u_rst_n(false);
    }
    delayMicroseconds(100);

    set_ctrl_ena(true);
    delayMicroseconds(100);

    g_current_project = project;
}

uint8_t current_project() {
    return g_current_project;
}

}  // namespace tt
