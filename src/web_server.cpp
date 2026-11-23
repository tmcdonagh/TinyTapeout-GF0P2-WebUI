#include "web_server.h"
#include "pins.h"
#include "mux.h"
#include "clock.h"
#include "vga_capture.h"
#include "io_expander.h"
#include "project_db.h"
#include "index_html.h"
#include <WebServer.h>
#include <ArduinoJson.h>

namespace tt {

namespace {
    constexpr int HTTP_PORT = 80;
    WebServer server(HTTP_PORT);

    void send_cors_headers() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    }

    void send_capture_in_progress() {
        server.send(409, "application/json", "{\"error\":\"capture in progress\"}");
    }

    const char* capture_state_to_string(CaptureState state) {
        switch (state) {
            case CaptureState::Idle: return "idle";
            case CaptureState::Running: return "running";
            case CaptureState::DoneOk: return "ok";
            case CaptureState::DoneFail: return "failed";
        }
        return "idle";
    }

    void handle_state() {
        send_cors_headers();
        JsonDocument doc;
        doc["project"] = current_project();
        doc["clock_running"] = clock_running();
        doc["clock_freq_hz"] = vga_capture_busy() ? vga_capture_clock_hz()
                                                  : clock_frequency();
        doc["uo_out"] = read_uo_out();
        doc["ui_in"] = ui_in_value();
        doc["uio_value"] = uio_value();
        doc["uio_direction"] = uio_direction();
        doc["u_rst_n"] = digitalRead(PIN_U_RST_N) == HIGH;
        doc["frame_serial"] = vga_frame_serial();
        CaptureStatus status = vga_capture_status();
        doc["capture_state"] = capture_state_to_string(status.state);
        doc["capture_row"] = status.row;
        doc["preview_row"] = vga_preview_row();
        doc["io_expander_present"] = io_expander_present();
        String body;
        serializeJson(doc, body);
        server.send(200, "application/json", body);
    }

    void handle_select() {
        send_cors_headers();
        if (vga_capture_busy()) {
            send_capture_in_progress();
            return;
        }
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
        if (vga_capture_busy()) {
            send_capture_in_progress();
            return;
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

    void handle_step() {
        send_cors_headers();
        if (vga_capture_busy()) {
            send_capture_in_progress();
            return;
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
        if (vga_capture_busy()) {
            send_capture_in_progress();
            return;
        }
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

    void handle_reset() {
        send_cors_headers();
        if (vga_capture_busy()) {
            send_capture_in_progress();
            return;
        }
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
        if (desc == nullptr || desc->json_blob == nullptr) {
            server.send(404, "application/json", "{\"error\":\"project not found\"}");
            return;
        }
        server.send(200, "application/json", desc->json_blob);
    }

    // Build the fixed part of the served BMP (file header + DIB header +
    // 256-entry palette).  NOTE: do not build the whole BMP into one String
    // (~78 KB) — next to the frame buffer and the WiFi stack the largest
    // contiguous heap block is only ~64 KB, and the String silently
    // truncates, producing a BMP whose top rows the browser renders as
    // blank.  The pixel data must be streamed row by row instead.
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

        if (vga_capture_busy()) {
            send_capture_in_progress();
            return;
        }

        if (tt::vga_frame_buffer() == nullptr) {
            server.send(500, "application/json", "{\"error\":\"frame buffer not allocated\"}");
            return;
        }

        String header;
        build_frame_bmp_header(header);
        server.setContentLength(header.length() +
                                static_cast<size_t>(tt::VGA_FRAME_W) * tt::VGA_FRAME_H);
        server.send(200, "image/bmp", header);

        // Stream the pixel data row by row, bottom-up (BMP order).  320-byte
        // chunks; no large heap allocation anywhere on this path.
        const uint8_t* fb = tt::vga_frame_buffer();
        for (int row = tt::VGA_FRAME_H - 1; row >= 0; --row) {
            server.sendContent(reinterpret_cast<const char*>(fb + row * tt::VGA_FRAME_W),
                               tt::VGA_FRAME_W);
        }
    }

    void handle_frame_raw() {
        send_cors_headers();

        if (tt::vga_frame_buffer() == nullptr) {
            server.send(500, "application/json", "{\"error\":\"frame buffer not allocated\"}");
            return;
        }

        CaptureStatus status = tt::vga_capture_status();
        const uint8_t* fb = tt::vga_frame_buffer();

        int from = server.arg("from").toInt();
        if (from < 0) from = 0;

        // During the seek phase (running but no aligned row yet) the buffer is
        // a rolling preview window.  Serve rows in circular-buffer order from
        // the oldest sample, with `from` as a logical offset into that window.
        bool seeking = (status.state == CaptureState::Running) && (status.row < 0);
        if (seeking) {
            uint32_t preview = tt::vga_preview_row();
            int available = (preview < tt::VGA_FRAME_H)
                                ? static_cast<int>(preview)
                                : tt::VGA_FRAME_H;
            int oldest = (static_cast<int>(preview) - available + tt::VGA_FRAME_H)
                         % tt::VGA_FRAME_H;     // 0 before first wrap, preview%240 after
            if (from > available) from = available;
            int rows_to_send = available - from;
            int start = (oldest + from) % tt::VGA_FRAME_H;

            size_t body_len = static_cast<size_t>(rows_to_send) * tt::VGA_FRAME_W;
            server.setContentLength(body_len);
            server.send(200, "application/octet-stream", "");
            if (rows_to_send > 0) {
                int first_len = (rows_to_send < tt::VGA_FRAME_H - start)
                                    ? rows_to_send
                                    : (tt::VGA_FRAME_H - start);
                server.sendContent(
                    reinterpret_cast<const char*>(fb + start * tt::VGA_FRAME_W),
                    static_cast<size_t>(first_len) * tt::VGA_FRAME_W);
                int second_len = rows_to_send - first_len;
                if (second_len > 0) {
                    server.sendContent(
                        reinterpret_cast<const char*>(fb),
                        static_cast<size_t>(second_len) * tt::VGA_FRAME_W);
                }
            }
            return;
        }

        int available = (status.state == CaptureState::Running)
                            ? (status.row + 1)
                            : tt::VGA_FRAME_H;
        if (available < 0) available = 0;
        if (available > tt::VGA_FRAME_H) available = tt::VGA_FRAME_H;

        if (from > available) from = available;

        size_t rows = static_cast<size_t>(available - from);
        size_t body_len = rows * tt::VGA_FRAME_W;

        server.setContentLength(body_len);
        server.send(200, "application/octet-stream", "");
        if (body_len > 0) {
            server.sendContent(reinterpret_cast<const char*>(fb + from * tt::VGA_FRAME_W),
                               body_len);
        }
    }

    void handle_options() {
        send_cors_headers();
        server.send(204);
    }

    void handle_not_found() {
        send_cors_headers();
        // If the request came in with a non-local Host header (e.g.,
        // connectivitycheck.gstatic.com), redirect it to the captive portal.
        // API calls from the web UI target 192.168.4.1 explicitly.
        if (server.hostHeader() != "192.168.4.1") {
            server.sendHeader("Location", "http://192.168.4.1/", true);
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
        server.sendHeader("Location", "http://192.168.4.1/", true);
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
    server.on("/api/step", HTTP_POST, handle_step);
    server.on("/api/step", HTTP_OPTIONS, handle_options);
    server.on("/api/ui_in", HTTP_POST, handle_ui_in);
    server.on("/api/ui_in", HTTP_OPTIONS, handle_options);
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
