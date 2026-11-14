#include "vga_capture.h"
#include "pins.h"
#include "clock.h"
#include "mux.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace tt {

namespace {

// VGA timing constants from the screensaver's vga_sync_generator.v.
// Total frame is 525 lines of 800 clocks each (420k clocks/frame):
//   vpos 0-479   active video (480 lines)
//   vpos 480-489 bottom border
//   vpos 490-491 VSync pulse (active HIGH)
//   vpos 492-524 top border (33 lines)
// Within a line: HSync is active HIGH from hpos 656 to 751; the active
// video starts at hpos 0 after a 48-clock back porch.
constexpr int LINE_CLOCKS = 800;
constexpr int H_BACK = 48;   // back porch between HSync fall and active video
constexpr int V_TOP = 33;    // top border lines between VSync fall and display

// Sample every 2nd active pixel and line: 640x480 -> 320x240.
constexpr int PIXELS_TO_SKIP = 2;
constexpr int LINES_TO_SKIP = 2;

// Manual clock half-period.  2 us keeps a full frame capture under ~2 s.
constexpr unsigned long STEP_HALF_PERIOD_US =
    1000000UL / (2 * tt::VGA_CAPTURE_CLOCK_HZ);

// Largest number of manual clock steps we are willing to wait for a sync
// edge (a full line is 800 clocks, a full frame ~420k).
constexpr int HSYNC_TIMEOUT = 10000;

// The 320x240 frame buffer is allocated on the heap: as a static it would
// overflow the DRAM segment, and at boot there is plenty of contiguous heap.
uint8_t* g_frame_buffer = nullptr;

// Cache control: a fresh frame is requested when project 33 is selected or
// its inputs change; main.cpp calls vga_cache_service() every loop pass.
static volatile bool g_cache_requested = false;
static unsigned long g_last_cache_ms = 0;
static bool g_have_last_cache = false;

// Shared capture state. Written by the capture task with release ordering,
// read by the loop task / HTTP handlers with acquire ordering.
static std::atomic<uint32_t> g_frame_serial{0};           // successful captures
static std::atomic<CaptureState> g_capture_state{CaptureState::Idle};
static std::atomic<int> g_capture_row{-1};                // last completed row
static std::atomic<uint32_t> g_preview_row{0};            // preview rows published during seek

inline uint8_t step_and_read() {
    // Manual clocking protocol: drive one clock edge, wait for the outputs
    // to settle, drive the other edge, wait, then latch all 8 uo_out pins.
    digitalWrite(PIN_CLK, HIGH);
    delayMicroseconds(STEP_HALF_PERIOD_US);
    digitalWrite(PIN_CLK, LOW);
    delayMicroseconds(STEP_HALF_PERIOD_US);
    return read_uo_out();
}

inline void feed_wdt_every(int& counter, int interval = 5000) {
    if (++counter >= interval) {
        delay(1);
        counter = 0;
    }
}

// Advance to the active-video start (hpos 0) of the next line, consuming
// whatever remains of the current line.  Requires a complete HSync rise then
// fall, then skips the back porch.  Starting from the end of an active area
// it consumes exactly one line per call.
bool sync_to_next_active_start() {
    int wdt_counter = 0;
    bool prev = ((read_uo_out() >> 7) & 1) != 0;
    bool seen_rise = false;
    for (int i = 0; i < HSYNC_TIMEOUT; ++i) {
        uint8_t uo = step_and_read();
        bool hsync = (uo >> 7) & 1;
        if (!prev && hsync) seen_rise = true;
        if (seen_rise && prev && !hsync) {
            for (int p = 0; p < H_BACK; ++p) step_and_read();
            return true;
        }
        prev = hsync;
        feed_wdt_every(wdt_counter);
    }
    return false;
}

// uo_out mapping: {hsync, B0, G0, R0, vsync, B1, G1, R1}.  Each channel is
// 2-bit (MSB = uo[2,1,0], LSB = uo[6,5,4]); store it packed as an index into
// the 256-entry palette the BMP server builds.
inline uint8_t uo_to_index(uint8_t uo) {
    uint8_t r = (((uo >> 0) & 1) << 1) | ((uo >> 4) & 1);
    uint8_t g = (((uo >> 1) & 1) << 1) | ((uo >> 5) & 1);
    uint8_t b = (((uo >> 2) & 1) << 1) | ((uo >> 6) & 1);
    return (r << 4) | (g << 2) | b;
}

// Runs the existing blocking capture sequence on the background task.
// Returns true when a complete frame was written to g_frame_buffer.
bool run_capture() {
    if (g_frame_buffer == nullptr) {
        Serial.println("[capture] frame buffer not allocated");
        return false;
    }

    // Mark that no rows are ready yet.
    g_capture_row.store(-1, std::memory_order_release);

    Serial.println("[capture] starting");
    // Stop any free-running clock so PIN_CLK is under manual GPIO control.
    clock_stop();

    // Stepping seek: advance one line at a time, sampling every second line
    // into the rolling preview window, until we see a VSync HIGH line followed
    // by a VSync LOW line (the pulse fell -> top border start).
    bool prev_vsync = false;
    uint32_t preview_line = 0;
    int seek_wdt_counter = 0;
    for (;;) {
        // The first preview sample starts wherever the chip was parked: a torn,
        // arbitrary fragment. Preview-only; overwritten by the aligned capture.
        bool sample = (preview_line % 2) == 0;
        if (sample) {
            uint32_t sample_index = preview_line / 2;
            uint8_t* row_ptr = g_frame_buffer +
                               (sample_index % VGA_FRAME_H) * VGA_FRAME_W;
            for (uint16_t col = 0; col < VGA_FRAME_W; ++col) {
                row_ptr[col] = uo_to_index(step_and_read());
                for (int skip = 1; skip < PIXELS_TO_SKIP; ++skip) {
                    step_and_read();
                }
            }
            g_preview_row.store(sample_index + 1, std::memory_order_release);
        }
        if (!sync_to_next_active_start()) {
            Serial.println("[capture] hsync timeout during seek");
            return false;
        }
        bool vsync_now = ((read_uo_out() >> 3) & 1) != 0;
        if (prev_vsync && !vsync_now) {
            // VSync pulse fell: we are at the active start of the first top
            // border line.
            break;
        }
        prev_vsync = vsync_now;
        ++preview_line;
        feed_wdt_every(seek_wdt_counter, 100);
    }

    // Reset preview bookkeeping so the aligned fill starts clean, then
    // consume the 33 border lines so row 0 is display line 0.
    g_preview_row.store(0, std::memory_order_release);
    g_capture_row.store(-1, std::memory_order_release);
    for (int i = 0; i < V_TOP; ++i) {
        if (!sync_to_next_active_start()) {
            Serial.println("[capture] hsync timeout in top border");
            return false;
        }
    }

    // The screensaver's wallpaper slides slowly (~1 video line per animation
    // frame), so a capture is a snapshot of it at the current position.
    uint16_t row = 0;
    int row_wdt_counter = 0;
    uint32_t non_black_count = 0;
    while (row < VGA_FRAME_H) {
        for (uint16_t col = 0; col < VGA_FRAME_W; ++col) {
            uint8_t index = uo_to_index(step_and_read());
            g_frame_buffer[static_cast<size_t>(row) * VGA_FRAME_W + col] = index;
            if (index != 0) ++non_black_count;
            for (int skip = 1; skip < PIXELS_TO_SKIP; ++skip) {
                step_and_read();
            }
        }

        // Publish this completed row before advancing to the next one.
        g_capture_row.store(static_cast<int>(row), std::memory_order_release);

        ++row;
        feed_wdt_every(row_wdt_counter, 100);

        // Advance to the row LINES_TO_SKIP display lines later: one call
        // consumes the rest of this captured line, the remaining
        // LINES_TO_SKIP-1 calls each consume a full skipped line.
        for (int skip = 0; skip < LINES_TO_SKIP; ++skip) {
            if (!sync_to_next_active_start()) {
                Serial.println("[capture] hsync timeout skipping lines");
                return false;
            }
        }
    }

    Serial.print("[capture] done, non-black pixels: ");
    Serial.println(non_black_count);
    return true;
}

static void capture_task(void*) {
    bool ok = run_capture();
    if (ok) {
        g_frame_serial.fetch_add(1, std::memory_order_release);
    }
    g_capture_state.store(ok ? CaptureState::DoneOk : CaptureState::DoneFail,
                          std::memory_order_release);
    Serial.printf("[capture] stack high water mark: %u\n",
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    vTaskDelete(nullptr);
}

}  // namespace

void vga_capture_init() {
    if (g_frame_buffer != nullptr) return;
    size_t size = static_cast<size_t>(VGA_FRAME_W) * VGA_FRAME_H;
    g_frame_buffer = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
    if (g_frame_buffer == nullptr) {
        Serial.println("[vga_capture] failed to allocate frame buffer");
    } else {
        memset(g_frame_buffer, 0, size);
        Serial.println("[vga_capture] frame buffer allocated");
    }
}

bool vga_capture_start_async() {
    // Allow starting from Idle or any completed state; only Running blocks.
    CaptureState expected;
    do {
        expected = g_capture_state.load(std::memory_order_acquire);
        if (expected == CaptureState::Running) return false;
    } while (!g_capture_state.compare_exchange_weak(expected, CaptureState::Running,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_acquire));

    // Row counters reset at task start (before the seek begins).
    g_capture_row.store(-1, std::memory_order_release);
    g_preview_row.store(0, std::memory_order_release);

    TaskHandle_t handle = nullptr;
    if (xTaskCreatePinnedToCore(capture_task, "vga_cap", 8192, nullptr, 1, &handle, 0) != pdPASS) {
        g_capture_state.store(CaptureState::DoneFail, std::memory_order_release);
        return false;
    }
    return true;
}

CaptureStatus vga_capture_status() {
    CaptureState state = g_capture_state.load(std::memory_order_acquire);
    int row = -1;
    if (state == CaptureState::Running) {
        row = g_capture_row.load(std::memory_order_acquire);
    } else if (state == CaptureState::DoneOk || state == CaptureState::DoneFail) {
        row = g_capture_row.load(std::memory_order_acquire);
    }
    return {state, row};
}

bool vga_capture_busy() {
    return g_capture_state.load(std::memory_order_acquire) == CaptureState::Running;
}

uint32_t vga_frame_serial() {
    return g_frame_serial.load(std::memory_order_acquire);
}

uint32_t vga_preview_row() {
    return g_preview_row.load(std::memory_order_acquire);
}

const uint8_t* vga_frame_buffer() {
    return g_frame_buffer;
}

uint32_t vga_capture_clock_hz() {
    return VGA_CAPTURE_CLOCK_HZ;
}

void vga_cache_request() {
    g_cache_requested = true;
}

void vga_cache_service() {
    if (!g_cache_requested) return;
    if (current_project() != 33) {
        g_cache_requested = false;
        return;
    }
    // Not yet running: keep the request pending until reset is released.
    if (digitalRead(PIN_U_RST_N) != HIGH) return;

    // Defer if a capture is already running.
    if (vga_capture_busy()) return;

    // Debounce: at most one capture every 3 s.  Defer rather than drop so a
    // quick input toggle still refreshes the cached image.
    unsigned long now = millis();
    if (g_have_last_cache && now - g_last_cache_ms < 3000) return;

    g_have_last_cache = true;
    g_last_cache_ms = now;
    g_cache_requested = false;
    vga_capture_start_async();
}

}  // namespace tt
