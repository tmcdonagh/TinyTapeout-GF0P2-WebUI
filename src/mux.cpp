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
    // The descriptor's uio_direction describes the CHIP's pin directions
    // (1 = the design drives that pin), so the host expander must MIRROR
    // it: inputs wherever the chip drives, outputs wherever it reads.
    // Passing the mask through un-inverted makes the expander fight the
    // chip's outputs - bus contention that broke Simon's segments.
    //
    // Generic descriptors (json_blob == nullptr) describe an UNDOCUMENTED
    // design: the chip's real uio directions are unknown, so a stored
    // 0x00 must NOT be mirrored into 0xFF (all host pins driving low -
    // direct contention with whatever the design drives). Unknown
    // directions leave the host expander high-Z (all inputs).
    const ProjectDescriptor* desc = project_by_address(project);
    if (desc != nullptr && desc->json_blob != nullptr) {
        set_uio_direction(static_cast<uint8_t>(~desc->uio_direction & 0xFF));
    } else {
        set_uio_direction(0x00);  // unknown design: host keeps uio as inputs
    }

    if (project == 0) {
        // Chip ROM: keep u_rst_n LOW so the ROM auto-increments on each
        // clock edge (the bring-up recipe).
        set_u_rst_n(false);
    } else if (project == 33) {
        // VGA screensaver project: release reset and default to tiled mode so
        // the logo fills the frame.  Request a cached frame capture so the web
        // UI can serve /api/frame.bmp immediately.
        set_ui_in(0x01);
        set_u_rst_n(true);
        vga_cache_request();
    } else {
        // Every real design runs with its reset released; the ROM
        // auto-increment mode is only needed at address 0.
        set_u_rst_n(true);
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
