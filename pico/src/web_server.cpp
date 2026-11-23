#include "web_server.h"
#include "pins.h"
#include "mux.h"
#include "clock.h"
#include "vga_capture.h"
#include "io_expander.h"
#include "gamepad.h"
#include "project_db.h"
#include "wifi_config.h"
#include "index_html.h"
#include <WebServer.h>
#include <ArduinoJson.h>

namespace tt {

namespace {

    constexpr int HTTP_PORT = 80;
    WebServer server(HTTP_PORT);

    // A single response must never hold the single-threaded loop hostage.
    // WiFiClient::write's 5 s timeout RESETS on any progress
    // (ClientContext::_write_from_source re-arms _op_start_time whenever
    // _write_some moves a byte), so a phone in WiFi power-save ACKing in a
    // trickle can keep one 77 KB frame fetch alive for MINUTES - freezing
    // the capture engine, the state polls, and (from the phone's side) the
    // AP, until the trickle ends. Instead: write only while the TCP send
    // buffer has room (instant), pump the background with delay(1) while
    // the peer drains, and cut the connection at the deadline. The phone
    // just repaints on its next poll.
    constexpr uint32_t STREAM_DEADLINE_MS = 2000;

    bool stream_body(const uint8_t* data, size_t len, uint32_t deadline_ms) {
        WiFiClient& c = server.client();
        while (len > 0) {
            if (!c.connected() ||
                static_cast<int32_t>(deadline_ms - millis()) <= 0) {
                c.stop();
                return false;
            }
            int avail = c.availableForWrite();
            if (avail <= 0) {
                delay(1);  // pump lwIP/WiFi background while the peer drains
                continue;
            }
            size_t chunk = len < static_cast<size_t>(avail)
                               ? len
                               : static_cast<size_t>(avail);
            size_t n = c.write(data, chunk);
            if (n == 0) {
                c.stop();
                return false;
            }
            data += n;
            len -= n;
        }
        return true;
    }

    void send_cors_headers() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    }

    void send_capture_in_progress() {
        server.send(409, "application/json", "{\"error\":\"capture in progress\"}");
    }

    void handle_state() {
        // Zombie-client gate: a full TCP send buffer means this client is
        // not draining (phone went to power-save / walked away). Cut it
        // instead of stalling the loop for 5 s per queued request - the
        // phone polls 4x/s, so backed-up clients would wedge the server
        // continuously.
        if (server.client().availableForWrite() <= 0) {
            server.client().stop();
            return;
        }
        send_cors_headers();
        JsonDocument doc;
        doc["project"] = current_project();
        doc["clock_running"] = clock_running();
        // Truthful clock reporting: while the capture engine owns the clock
        // pin (project 33, reset released, no free-run clock requested) the
        // pin IS being driven - at the capture rate during the DMA phase and
        // at the seek rate during the (transient) seek. Reporting 0 in the
        // seek gaps made the UI's clock readout flicker to 0 between
        // attempts even though the chip never stopped being clocked.
        doc["clock_freq_hz"] = clock_running()
                                   ? clock_frequency()
                                   : (current_project() == 33 &&
                                              digitalRead(PIN_U_RST_N) == HIGH
                                          ? vga_capture_clock_hz()
                                          : 0);
        doc["capture_hz"] = vga_capture_clock_hz();
        doc["gamepad"] = true;  // the virtual gamepad endpoint is available
        doc["uo_out"] = read_uo_out();
        doc["ui_in"] = ui_in_value();
        doc["uio_value"] = uio_value();
        doc["uio_direction"] = uio_direction();
        doc["u_rst_n"] = digitalRead(PIN_U_RST_N) == HIGH;
        doc["ui_in_native"] = true;  // all 8 bits direct on this host
        uint32_t serial_count = vga_frame_serial();
        doc["frame_serial"] = serial_count;
        // With continuous background recapture, an in-flight capture is
        // the normal state of affairs: "running" until the first frame
        // exists, "ok" afterwards. The latest ATTEMPT's failure is still
        // reported ("failed") even when a stale frame remains on the
        // canvas - otherwise a wiring fault or stall would leave the UI
        // showing "Captured ..." forever.
        auto status = vga_capture_status();
        if (status.state == CaptureState::DoneFail) {
            doc["capture_state"] = "failed";
            doc["capture_row"] = serial_count > 0
                                     ? static_cast<int>(tt::VGA_FRAME_H) - 1
                                     : -1;
        } else if (serial_count > 0) {
            doc["capture_state"] = "ok";
            doc["capture_row"] = static_cast<int>(tt::VGA_FRAME_H) - 1;
        } else if (vga_capture_busy()) {
            doc["capture_state"] = "running";
            doc["capture_row"] = -1;
        } else {
            doc["capture_state"] = "idle";
            doc["capture_row"] = -1;
        }
        // The seek-phase preview is gone: kept for UI compatibility.
        doc["preview_row"] = 0;
        doc["io_expander_present"] = io_expander_present();
        String body;
        serializeJson(doc, body);
        server.send(200, "application/json", body);
        // Connection: close semantics - close NOW instead of letting the
        // core sit in its 5 s HTTP_MAX_CLOSE_WAIT refusing new accepts
        // (lwIP drains queued bytes before the FIN, nothing is lost).
        server.client().stop();
    }

    void handle_select() {
        send_cors_headers();
        // No 409: select_project() aborts a running capture itself.
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }

        int project = doc["project"] | -1;
        if (project < 0 || project_by_address(static_cast<uint16_t>(project)) == nullptr) {
            server.send(400, "application/json", "{\"error\":\"unknown project address\"}");
            return;
        }

        select_project(static_cast<uint16_t>(project));
        handle_state();
    }

    void handle_clock() {
        send_cors_headers();
        // A running capture owns the clock pin; abort rather than 409.
        if (vga_capture_busy()) {
            vga_capture_abort();
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }

        bool running = doc["running"] | clock_running();
        uint32_t freq_hz = doc["freq_hz"] | clock_target_frequency();

        if (!running) {
            clock_stop();
        } else {
            clock_start(freq_hz);
        }

        handle_state();
    }

    void handle_capture_clock() {
        send_cors_headers();
        // A running capture owns the clock pin; abort rather than 409
        // (same semantics as the serial k<N> command).
        if (vga_capture_busy()) {
            vga_capture_abort();
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }
        uint32_t hz = doc["hz"] | 0;
        if (hz < 1000000 || hz > 30000000) {
            server.send(400, "application/json",
                        "{\"error\":\"hz must be 1000000..30000000\"}");
            return;
        }
        vga_capture_set_clock_hz(hz);
        handle_state();
    }

    void handle_step() {
        send_cors_headers();
        // A running capture owns the clock pin; abort rather than 409.
        if (vga_capture_busy()) {
            vga_capture_abort();
        }
        clock_step();
        handle_state();
    }

    void handle_ui_in() {
        send_cors_headers();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }

        int value = doc["ui_in"] | -1;
        if (value < 0 || value > 255) {
            server.send(400, "application/json", "{\"error\":\"ui_in must be 0..255\"}");
            return;
        }

        set_ui_in(static_cast<uint8_t>(value));
        handle_state();
    }

    void handle_uio() {
        send_cors_headers();
        // I2C is independent of the capture engine; no abort needed.
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }

        if (!io_expander_present()) {
            server.send(503, "application/json", "{\"error\":\"io expander not present\"}");
            return;
        }

        if (doc["direction"].is<int>()) {
            int direction = doc["direction"];
            if (direction < 0 || direction > 255) {
                server.send(400, "application/json", "{\"error\":\"direction must be 0..255\"}");
                return;
            }
            set_uio_direction(static_cast<uint8_t>(direction));
        }

        if (doc["value"].is<int>()) {
            int value = doc["value"];
            if (value < 0 || value > 255) {
                server.send(400, "application/json", "{\"error\":\"value must be 0..255\"}");
                return;
            }
            set_uio_value(static_cast<uint8_t>(value));
        }

        handle_state();
    }

    void handle_gamepad() {
        send_cors_headers();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }
        // Full state per request (the UI sends its complete button set on
        // every change). One ~60 us burst per change; the design holds the
        // latched state between bursts, and START is edge-detected there,
        // so a press/release pair toggles manual mode exactly once.
        GamepadState s;
        s.start = doc["start"] | false;
        s.up = doc["up"] | false;
        s.down = doc["down"] | false;
        s.left = doc["left"] | false;
        s.right = doc["right"] | false;
        gamepad_apply(s);

        String body;
        serializeJson(doc, body);
        server.send(200, "application/json", body);
        server.client().stop();  // no HC_WAIT_CLOSE linger
    }

    void handle_reset() {
        send_cors_headers();
        // Aborting keeps a mid-flight capture from sampling the design
        // while its state is being reset (the re-arm loop resumes capture
        // once reset is released on project 33).
        vga_capture_abort();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "application/json", "{\"error\":\"invalid json\"}");
            return;
        }

        bool reset_asserted = doc["reset"] | false;
        // u_rst_n is active-low: asserted reset = LOW on the wire.
        set_u_rst_n(!reset_asserted);
        handle_state();
    }

    void handle_capture() {
        send_cors_headers();
        if (!vga_capture_start_async()) {
            send_capture_in_progress();
            return;
        }
        handle_state();
    }

    void handle_projects() {
        send_cors_headers();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (size_t i = 0; i < project_count(); ++i) {
            JsonObject obj = arr.add<JsonObject>();
            obj["address"] = project_address(i);
            obj["title"] = project_title(i);
            obj["macro"] = project_macro(i);
        }
        String body;
        serializeJson(doc, body);
        server.send(200, "application/json", body);
    }

    void handle_project() {
        send_cors_headers();
        int address = server.arg("address").toInt();
        const ProjectDescriptor* desc = project_by_address(static_cast<uint16_t>(address));
        if (desc == nullptr) {
            server.send(404, "application/json", "{\"error\":\"project not found\"}");
            return;
        }
        if (desc->json_blob != nullptr) {
            server.send(200, "application/json", desc->json_blob);
        } else {
            server.send(200, "application/json", generic_project_json(static_cast<uint16_t>(address)));
        }
    }

    // Build the fixed part of the served BMP (file header + DIB header +
    // 256-entry palette).  NOTE: do not build the whole BMP into one String
    // (~78 KB); stream the pixel data row by row instead.
    void build_frame_bmp_header(String& out) {
        constexpr size_t palette_entries = 256;
        constexpr size_t palette_bytes = palette_entries * 4;
        constexpr size_t dib_header_size = 40;
        constexpr size_t file_header_size = 14;
        constexpr size_t header_size = file_header_size + dib_header_size + palette_bytes;
        constexpr size_t row_size = (tt::VGA_FRAME_W + 3) & ~3;  // 4-byte aligned
        constexpr size_t pixel_bytes = row_size * tt::VGA_FRAME_H;

        out.reserve(header_size);

        // BMP file header (14 bytes).
        out += 'B';
        out += 'M';
        uint32_t file_size = header_size + pixel_bytes;
        for (int i = 0; i < 4; ++i) {
            out += static_cast<char>(file_size & 0xFF);
            file_size >>= 8;
        }
        for (int i = 0; i < 4; ++i) out += static_cast<char>(0);  // reserved
        uint32_t data_offset = header_size;
        for (int i = 0; i < 4; ++i) {
            out += static_cast<char>(data_offset & 0xFF);
            data_offset >>= 8;
        }

        // DIB header: BITMAPINFOHEADER (40 bytes).
        uint32_t dib_size = dib_header_size;
        for (int i = 0; i < 4; ++i) {
            out += static_cast<char>(dib_size & 0xFF);
            dib_size >>= 8;
        }
        int32_t width = tt::VGA_FRAME_W;
        for (int i = 0; i < 4; ++i) {
            out += static_cast<char>(width & 0xFF);
            width >>= 8;
        }
        int32_t height = tt::VGA_FRAME_H;
        for (int i = 0; i < 4; ++i) {
            out += static_cast<char>(height & 0xFF);
            height >>= 8;
        }
        uint16_t planes = 1;
        for (int i = 0; i < 2; ++i) {
            out += static_cast<char>(planes & 0xFF);
            planes >>= 8;
        }
        uint16_t bpp = 8;
        for (int i = 0; i < 2; ++i) {
            out += static_cast<char>(bpp & 0xFF);
            bpp >>= 8;
        }
        uint32_t compression = 0;
        for (int i = 0; i < 4; ++i) {
            out += static_cast<char>(compression & 0xFF);
            compression >>= 8;
        }
        uint32_t image_size = pixel_bytes;
        for (int i = 0; i < 4; ++i) {
            out += static_cast<char>(image_size & 0xFF);
            image_size >>= 8;
        }
        uint32_t ppm = 2835;  // 72 DPI
        for (int i = 0; i < 4; ++i) {
            out += static_cast<char>(ppm & 0xFF);
            ppm >>= 8;
        }
        for (int i = 0; i < 4; ++i) {
            out += static_cast<char>(ppm & 0xFF);
            ppm >>= 8;
        }
        for (int i = 0; i < 4; ++i) out += static_cast<char>(0);  // colors used
        for (int i = 0; i < 4; ++i) out += static_cast<char>(0);  // important colors

        // Color palette: indices 0..63 are the 2-bit R/G/B combinations.
        // Remaining entries are black (unused).
        static const uint8_t expand[4] = {0, 85, 170, 255};
        for (size_t idx = 0; idx < palette_entries; ++idx) {
            uint8_t r = (idx >> 4) & 0x3;
            uint8_t g = (idx >> 2) & 0x3;
            uint8_t b = idx & 0x3;
            out += static_cast<char>(expand[b]);
            out += static_cast<char>(expand[g]);
            out += static_cast<char>(expand[r]);
            out += static_cast<char>(0);
        }
    }

    void handle_frame_bmp() {
        send_cors_headers();
        // Serve the last complete frame even while the next capture runs:
        // extraction and this handler share one loop thread, so the buffer
        // is always an internally consistent snapshot.

        String header;
        build_frame_bmp_header(header);
        server.setContentLength(header.length() +
                                static_cast<size_t>(tt::VGA_FRAME_W) * tt::VGA_FRAME_H);
        const uint32_t deadline = millis() + STREAM_DEADLINE_MS;
        server.send(200, "image/bmp", header);

        // Stream the pixel data row by row, bottom-up (BMP order),
        // bounded by the deadline (see stream_body).
        const uint8_t* fb = tt::vga_frame_buffer();
        for (int row = tt::VGA_FRAME_H - 1; row >= 0; --row) {
            if (!stream_body(fb + row * tt::VGA_FRAME_W, tt::VGA_FRAME_W,
                             deadline)) {
                return;
            }
        }
        // Close now (Connection: close): skips the core's 5 s
        // HTTP_MAX_CLOSE_WAIT on slow clients; queued bytes still drain.
        server.client().stop();
    }

    void handle_frame_raw() {
        send_cors_headers();

        // The frame buffer always holds the last complete frame (extraction
        // and this handler share one loop thread), so serve rows from
        // `from` unconditionally.
        const uint8_t* fb = tt::vga_frame_buffer();
        int from = server.arg("from").toInt();
        if (from < 0) from = 0;
        if (from > tt::VGA_FRAME_H) from = tt::VGA_FRAME_H;

        size_t rows = static_cast<size_t>(tt::VGA_FRAME_H - from);
        size_t body_len = rows * tt::VGA_FRAME_W;

        server.setContentLength(body_len);
        const uint32_t deadline = millis() + STREAM_DEADLINE_MS;
        server.send(200, "application/octet-stream", "");
        if (body_len > 0) {
            stream_body(fb + from * tt::VGA_FRAME_W, body_len, deadline);
        }
        server.client().stop();  // no HC_WAIT_CLOSE linger (see handle_state)
    }

    void handle_options() {
        send_cors_headers();
        server.send(204);
    }

    void handle_not_found() {
        send_cors_headers();
        // If the request came in with a non-local Host header (e.g.,
        // connectivitycheck.gstatic.com), redirect it to the captive portal.
        // Use the live IP: in station mode the device lives at the
        // router-assigned address, not 192.168.4.1.
        String ip = wifi_ip();
        if (server.hostHeader() != ip) {
            server.sendHeader("Location", "http://" + ip + "/", true);
            server.send(302, "text/plain", "");
            return;
        }
        server.send(404, "application/json", "{\"error\":\"not found\"}");
    }

    void handle_index() {
        send_cors_headers();
        server.send_P(200, "text/html", INDEX_HTML);
    }

    void handle_captive_redirect() {
        send_cors_headers();
        String ip = wifi_ip();
        server.sendHeader("Location", "http://" + ip + "/", true);
        server.send(302, "text/plain", "");
    }
}

void web_server_init() {
    server.on("/", HTTP_GET, handle_index);

    // Common captive-portal probe URLs: redirect to the web UI so the OS
    // pops up its "Sign in to network" sheet.
    server.on("/generate_204", HTTP_GET, handle_captive_redirect);
    server.on("/gen_204", HTTP_GET, handle_captive_redirect);
    server.on("/ncsi.txt", HTTP_GET, handle_captive_redirect);
    server.on("/connecttest.txt", HTTP_GET, handle_captive_redirect);
    server.on("/connectivity-check.html", HTTP_GET, handle_captive_redirect);
    server.on("/hotspot-detect.html", HTTP_GET, handle_captive_redirect);
    server.on("/library/test/success.html", HTTP_GET, handle_captive_redirect);

    server.on("/api/state", HTTP_GET, handle_state);
    server.on("/api/select", HTTP_POST, handle_select);
    server.on("/api/select", HTTP_OPTIONS, handle_options);
    server.on("/api/clock", HTTP_POST, handle_clock);
    server.on("/api/clock", HTTP_OPTIONS, handle_options);
    server.on("/api/capture_clock", HTTP_POST, handle_capture_clock);
    server.on("/api/capture_clock", HTTP_OPTIONS, handle_options);
    server.on("/api/step", HTTP_POST, handle_step);
    server.on("/api/step", HTTP_OPTIONS, handle_options);
    server.on("/api/ui_in", HTTP_POST, handle_ui_in);
    server.on("/api/ui_in", HTTP_OPTIONS, handle_options);
    server.on("/api/gamepad", HTTP_POST, handle_gamepad);
    server.on("/api/gamepad", HTTP_OPTIONS, handle_options);
    server.on("/api/uio", HTTP_POST, handle_uio);
    server.on("/api/uio", HTTP_OPTIONS, handle_options);
    server.on("/api/reset", HTTP_POST, handle_reset);
    server.on("/api/reset", HTTP_OPTIONS, handle_options);
    server.on("/api/capture", HTTP_POST, handle_capture);
    server.on("/api/capture", HTTP_OPTIONS, handle_options);
    server.on("/api/projects", HTTP_GET, handle_projects);
    server.on("/api/project", HTTP_GET, handle_project);
    server.on("/api/frame.bmp", HTTP_GET, handle_frame_bmp);
    server.on("/api/frame.bmp", HTTP_OPTIONS, handle_options);
    server.on("/api/frame_raw", HTTP_GET, handle_frame_raw);
    server.on("/api/frame_raw", HTTP_OPTIONS, handle_options);
    server.onNotFound(handle_not_found);
    server.begin();
    Serial.print("HTTP server started on port ");
    Serial.println(HTTP_PORT);
}

void web_server_loop() {
    server.handleClient();
}

}  // namespace tt
