#include <Arduino.h>
#include <hardware/watchdog.h>
#include <stdlib.h>
#include "pins.h"
#include "mux.h"
#include "clock.h"
#include "io_expander.h"
#include "project_db.h"
#include "vga_capture.h"
#include "wifi_config.h"
#include "dns_server.h"
#include "web_server.h"

void setup() {
    Serial.begin(115200);
    // Do NOT wait for the USB console to connect: on charger-power (no host)
    // `!Serial` never clears and the captive portal would never start - the
    // device's whole purpose. Boot lines appear once a console attaches.
    Serial.println("[boot] Serial up");
    tt::init_pins();
    Serial.println("[boot] pins ok");
    tt::io_expander_init();
    Serial.println("[boot] io expander ok");
    tt::project_db_init();
    Serial.println("[boot] project db ok");
    // NOTE: the RP2350 watchdog PERSISTS across soft resets. Any starvation
    // turns into a reboot loop (watchdog still armed after the reset, next
    // boot races its own init against the old budget). Disable it first
    // thing at boot; the firmware's long paths all feed it, but until the
    // feeding has been proven over a long soak, arming it does more harm
    // than good. The watchdog_update() calls stay (harmless no-ops).
    watchdog_disable();
    Serial.println("[boot] watchdog disabled");
    tt::vga_capture_init();
    Serial.println("[boot] vga capture ok");
    tt::wifi_init();
    Serial.println("[boot] wifi ok");
    tt::dns_server_init();
    Serial.println("[boot] dns ok");
    tt::web_server_init();
    Serial.println("[boot] web ok");
    delay(1000);
    Serial.println("Tiny Tapeout GF0P2 Web Showcase — Pico bring-up console");
    Serial.println("Serial commands:");
    Serial.println("  s<N>   select project N");
    Serial.println("  c<N>   start clock at N Hz (0 = stop)");
    Serial.println("  t      single-step clock");
    Serial.println("  r      read uo_out");
    Serial.println("  d      pin diagnostics");
    Serial.println("  i<XX>  set ui_in hex value (e.g. i01, i12)");
    Serial.println("  u<N>   set u_rst_n (0=low/assert, 1=high/release)");
    Serial.println("  w<XX>  set uio direction hex mask (1 = output)");
    Serial.println("  o<XX>  set uio value hex");
    Serial.println("  O      read uio back");
    Serial.println("  L      list projects");
    Serial.println("  W<s p> join home WiFi (W<ssid> <password>, reboots)");
    Serial.println("  WX     forget home WiFi (demo AP on next boot)");
    Serial.println("  N      network status");
    Serial.println("  v      capture one VGA frame (project 33)");
    Serial.println("  b      dump last captured 320x240 indexed frame as hex");
    Serial.println("  E      capture-engine synthetic self-test");
    Serial.println("  k<N>   set capture chip clock in Hz (1..30 MHz)");
    Serial.println("  a      auto-cycle projects 0-10 (for mux test)");
    Serial.println("  p      slow pulse ctrl_sel_inc for multimeter test");
}

void loop() {
    watchdog_update();
    tt::wifi_service();
    tt::dns_server_loop();
    tt::web_server_loop();
    tt::vga_capture_service();

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.startsWith("s")) {
            int project = line.substring(1).toInt();
            tt::select_project(static_cast<uint16_t>(project));
            Serial.print("Selected project: ");
            Serial.println(tt::current_project());
            tt::print_pin_diagnostics();
        } else if (line.startsWith("c")) {
            // A running capture owns the clock pin; abort rather than block.
            if (tt::vga_capture_busy()) {
                tt::vga_capture_abort();
            }
            int freq = line.substring(1).toInt();
            if (freq <= 0) {
                tt::clock_stop();
                Serial.println("Clock stopped");
            } else {
                tt::clock_start(static_cast<uint32_t>(freq));
                Serial.print("Clock started: ");
                Serial.print(tt::clock_frequency());
                Serial.println(" Hz");
            }
            tt::print_pin_diagnostics();
        } else if (line == "t") {
            if (tt::vga_capture_busy()) {
                tt::vga_capture_abort();
            }
            tt::clock_step();
            Serial.println("Clock stepped");
            tt::print_pin_diagnostics();
        } else if (line == "r") {
            Serial.print("uo_out: 0b");
            Serial.println(tt::read_uo_out(), BIN);
        } else if (line == "d") {
            tt::print_pin_diagnostics();
            tt::print_expander_diagnostics();
        } else if (line.startsWith("i")) {
            int value = strtol(line.substring(1).c_str(), nullptr, 16);
            tt::set_ui_in(static_cast<uint8_t>(value));
            Serial.print("ui_in set to: 0x");
            Serial.println(tt::ui_in_value(), HEX);
            tt::print_pin_diagnostics();
        } else if (line.startsWith("u")) {
            // Abort any running capture first: the engine must not sample
            // the design across a reset transition. The re-arm loop
            // resumes capture once reset is released on project 33.
            tt::vga_capture_abort();
            int val = line.substring(1).toInt();
            tt::set_u_rst_n(val != 0);
            Serial.print("u_rst_n set to: ");
            Serial.println(val != 0 ? "HIGH (released)" : "LOW (asserted)");
            tt::print_pin_diagnostics();
        } else if (line.startsWith("w")) {
            int value = strtol(line.substring(1).c_str(), nullptr, 16);
            tt::set_uio_direction(static_cast<uint8_t>(value));
            tt::print_expander_diagnostics();
        } else if (line.startsWith("o")) {
            int value = strtol(line.substring(1).c_str(), nullptr, 16);
            tt::set_uio_value(static_cast<uint8_t>(value));
            tt::print_expander_diagnostics();
        } else if (line == "O") {
            Serial.print("uio readback: 0x");
            Serial.println(tt::uio_value(), HEX);
        } else if (line == "L") {
            for (size_t i = 0; i < tt::project_count(); ++i) {
                Serial.print(tt::project_address(i));
                Serial.print(": ");
                Serial.println(tt::project_title(i));
            }
        } else if (line == "v") {
            if (tt::vga_capture_busy()) {
                Serial.println("capture in progress — wait for it to finish");
            } else {
                Serial.println("Capturing VGA frame...");
                tt::vga_capture_start_async();
                unsigned long start = millis();
                while (tt::vga_capture_busy() && millis() - start < 2000) {
                    tt::vga_capture_service();
                    delay(1);
                }
                auto status = tt::vga_capture_status();
                Serial.print("Capture result: ");
                if (status.state == tt::CaptureState::DoneOk) {
                    Serial.println("OK");
                    const uint8_t* fb = tt::vga_frame_buffer();
                    Serial.print("Captured in ");
                    Serial.print(millis() - start);
                    Serial.println(" ms");
                    Serial.print("Non-black pixels: ");
                    Serial.println(tt::vga_last_non_black());
                    Serial.print("First pixel index: 0x");
                    Serial.println(fb[0], HEX);
                    Serial.print("Center pixel index: 0x");
                    size_t center = (tt::VGA_FRAME_H / 2) * tt::VGA_FRAME_W +
                                    (tt::VGA_FRAME_W / 2);
                    Serial.println(fb[center], HEX);
                } else if (status.state == tt::CaptureState::Running) {
                    Serial.println("still running (continuing in background)");
                } else {
                    Serial.println("FAILED");
                }
            }
        } else if (line == "b") {
            const uint8_t* fb = tt::vga_frame_buffer();
            Serial.println("FRAME_BEGIN");
            for (size_t i = 0; i < tt::VGA_FRAME_W * tt::VGA_FRAME_H; ++i) {
                if (i % (64 * 64) == 0) watchdog_update();
                if (i % 64 == 0) Serial.println();
                if (fb[i] < 0x10) Serial.print('0');
                Serial.print(fb[i], HEX);
            }
            Serial.println();
            Serial.println("FRAME_END");
        } else if (line == "E") {
            tt::vga_capture_self_test();
        } else if (line == "C") {
            tt::vga_capture_debug_print();
        } else if (line == "R") {
            tt::vga_capture_ring_dump();
        } else if (line == "Q") {
            tt::vga_capture_alignment_dump();
        } else if (line.startsWith("k")) {
            // Abort-and-apply like c/t: in continuous mode the engine is
            // 'running' ~97% of the time, so a busy-guard would almost
            // never let the rate change.
            if (tt::vga_capture_busy()) {
                tt::vga_capture_abort();
            }
            unsigned long freq = strtoul(line.substring(1).c_str(), nullptr, 10);
            tt::vga_capture_set_clock_hz(static_cast<uint32_t>(freq));
        } else if (line == "a") {
            Serial.println("Auto-cycling projects 0-10, 1 second each...");
            for (int p = 0; p <= 10; ++p) {
                watchdog_update();  // the cycle runs 11 s: feed the watchdog
                tt::select_project(static_cast<uint16_t>(p));
                Serial.print("Project ");
                Serial.print(p);
                Serial.print(": uo_out=0b");
                Serial.print(tt::read_uo_out(), BIN);
                Serial.print(" (0x");
                Serial.print(tt::read_uo_out(), HEX);
                Serial.println(")");
                delay(1000);
            }
            Serial.println("Auto-cycle done");
        } else if (line == "p") {
            Serial.println("Toggling ctrl_sel_inc once per second for 10 seconds...");
            for (int i = 0; i < 10; ++i) {
                digitalWrite(tt::PIN_CTRL_SEL_INC, HIGH);
                Serial.println("ctrl_sel_inc = HIGH");
                delay(500);
                digitalWrite(tt::PIN_CTRL_SEL_INC, LOW);
                Serial.println("ctrl_sel_inc = LOW");
                delay(500);
            }
            Serial.println("Pulse test done");
        } else if (line.startsWith("W") && line != "WX") {
            // W<ssid> <pass>: join the home network and reboot.
            String rest = line.substring(1);
            int sp = rest.indexOf(' ');
            if (sp < 1) {
                Serial.println("usage: W<ssid> <password>");
            } else {
                tt::wifi_set_station(rest.substring(0, sp), rest.substring(sp + 1));
            }
        } else if (line == "WX") {
            tt::wifi_clear_station();
        } else if (line == "N") {
            tt::wifi_print_status();
        }
    }
    delay(1);
}
