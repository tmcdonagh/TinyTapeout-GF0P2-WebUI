#include <Arduino.h>
#include <stdlib.h>
#include <esp_system.h>
#include "pins.h"
#include "mux.h"
#include "clock.h"
#include "vga_capture.h"
#include "wifi_config.h"
#include "web_server.h"
#include "dns_server.h"

void setup() {
    Serial.begin(115200);
    tt::init_pins();
    delay(1000);
    Serial.println("Tiny Tapeout GF0P2 Web Showcase — boot");
    Serial.print("Reset reason: ");
    Serial.println(esp_reset_reason());
    disableLoopWDT();
    tt::vga_capture_init();
    tt::wifi_init();
    tt::dns_server_init();
    tt::web_server_init();
    Serial.println("Serial commands:");
    Serial.println("  s<N>   select project N");
    Serial.println("  c<N>   start clock at N Hz (0 = stop)");
    Serial.println("  t      single-step clock");
    Serial.println("  r      read uo_out");
    Serial.println("  d      pin diagnostics");
    Serial.println("  i<XX>  set ui_in hex value (e.g. i01, i12); bit0=tile, bit1=solid_color");
    Serial.println("  u<N>   set u_rst_n (0=low/assert, 1=high/release)");
    Serial.println("  v      capture one VGA frame (project 33)");
    Serial.println("  b      dump last captured 320x240 indexed frame as hex");
    Serial.println("  a      auto-cycle projects 0-10 (for mux test)");
    Serial.println("  p      slow pulse ctrl_sel_inc for multimeter test)");
}

void loop() {
    tt::dns_server_loop();
    tt::web_server_loop();
    tt::vga_cache_service();

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.startsWith("s")) {
            if (tt::vga_capture_busy()) {
                Serial.println("capture in progress — wait for it to finish");
            } else {
                int project = line.substring(1).toInt();
                tt::select_project(static_cast<uint8_t>(project));
                Serial.print("Selected project: ");
                Serial.println(tt::current_project());
                tt::print_pin_diagnostics();
            }
        } else if (line.startsWith("c")) {
            if (tt::vga_capture_busy()) {
                Serial.println("capture in progress — wait for it to finish");
            } else {
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
            }
        } else if (line == "t") {
            if (tt::vga_capture_busy()) {
                Serial.println("capture in progress — wait for it to finish");
            } else {
                tt::clock_step();
                Serial.println("Clock stepped");
                tt::print_pin_diagnostics();
            }
        } else if (line == "r") {
            Serial.print("uo_out: 0b");
            Serial.println(tt::read_uo_out(), BIN);
        } else if (line == "d") {
            tt::print_pin_diagnostics();
        } else if (line.startsWith("i")) {
            int value = strtol(line.substring(1).c_str(), nullptr, 16);
            tt::set_ui_in(static_cast<uint8_t>(value));
            Serial.print("ui_in set to: 0x");
            Serial.println(tt::ui_in_value(), HEX);
            tt::print_pin_diagnostics();
        } else if (line.startsWith("u")) {
            if (tt::vga_capture_busy()) {
                Serial.println("capture in progress — wait for it to finish");
            } else {
                int val = line.substring(1).toInt();
                tt::set_u_rst_n(val != 0);
                Serial.print("u_rst_n set to: ");
                Serial.println(val != 0 ? "HIGH (released)" : "LOW (asserted)");
                tt::print_pin_diagnostics();
            }
        } else if (line == "v") {
            Serial.println("Capturing VGA frame...");
            tt::vga_capture_start_async();
            auto status = tt::vga_capture_status();
            unsigned long start = millis();
            while (status.state == tt::CaptureState::Running && millis() - start < 30000) {
                delay(10);
                tt::web_server_loop();
                tt::dns_server_loop();
                status = tt::vga_capture_status();
            }
            Serial.print("Capture result: ");
            if (status.state == tt::CaptureState::DoneOk) {
                Serial.println("OK");
                const uint8_t* fb = tt::vga_frame_buffer();
                Serial.print("First pixel index: 0x");
                Serial.println(fb[0], HEX);
                Serial.print("Center pixel index: 0x");
                size_t center = (tt::VGA_FRAME_H / 2) * tt::VGA_FRAME_W + (tt::VGA_FRAME_W / 2);
                Serial.println(fb[center], HEX);
            } else if (status.state == tt::CaptureState::Running) {
                Serial.println("still running (continuing in background)");
            } else {
                Serial.println("FAILED");
            }
        } else if (line == "b") {
            const uint8_t* fb = tt::vga_frame_buffer();
            Serial.println("FRAME_BEGIN");
            for (size_t i = 0; i < tt::VGA_FRAME_W * tt::VGA_FRAME_H; ++i) {
                if (i % 64 == 0) Serial.println();
                if (fb[i] < 0x10) Serial.print('0');
                Serial.print(fb[i], HEX);
            }
            Serial.println();
            Serial.println("FRAME_END");
        } else if (line == "a") {
            Serial.println("Auto-cycling projects 0-10, 1 second each...");
            for (int p = 0; p <= 10; ++p) {
                tt::select_project(static_cast<uint8_t>(p));
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
        }
    }
    delay(1);
}
