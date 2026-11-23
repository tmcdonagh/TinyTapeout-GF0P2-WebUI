#include "mux.h"
#include "pins.h"
#include "vga_capture.h"
#include "project_db.h"

namespace tt {

namespace {
    uint16_t g_current_project = 0;

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

void select_project(uint16_t project) {
    // Reset ui_in and uio to safe defaults before changing projects.
    set_ui_in(0);
    set_uio_value(0);

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

    for (uint16_t i = 0; i < project; ++i) {
        pulse_high(PIN_CTRL_SEL_INC);
    }

    // Apply project-specific IO configuration from the descriptor.
    const ProjectDescriptor* desc = project_by_address(project);
    if (desc != nullptr) {
        set_uio_direction(desc->uio_direction);
    } else {
        set_uio_direction(0x00);  // default to inputs
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

uint16_t current_project() {
    return g_current_project;
}

}  // namespace tt
