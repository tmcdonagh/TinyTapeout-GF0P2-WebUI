#include "vga_capture.h"
#include "pins.h"
#include "clock.h"
#include "mux.h"

#include <Arduino.h>
#include <hardware/watchdog.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/pio.h>
#include <hardware/pio_instructions.h>

#include "capture.pio.h"

namespace tt {

namespace {

// ---- Geometry (from the screensaver's vga_sync_generator) --------------
// One frame = 800 clocks x 525 lines = 420k clocks. We sample every 2nd
// clock: 400 samples/line, 210,000 samples per frame period.
constexpr int LINE_CLOCKS = 800;
constexpr int SAMPLES_PER_LINE = LINE_CLOCKS / 2;         // 400
constexpr int RING_SAMPLES = SAMPLES_PER_LINE * 525;      // 210,000
constexpr int RING_WORDS = RING_SAMPLES / 4;              // 52,500 words
constexpr int ACTIVE_SAMPLES = 320;                        // 640 px / 2
constexpr int H_BACK_SAMPLES = 24;                         // 48 clocks / 2
constexpr int V_TOP_LINES = 33;                            // top border lines
constexpr int VSYNC_BIT = 3;                              // uo[3]
constexpr uint32_t SYS_HZ = 150000000;

// Samples from display row 0 col 0 through row 239 col 319, relative to
// the hardware alignment point (the VSync fall).
constexpr size_t DISPLAY_SPAN =
    static_cast<size_t>(V_TOP_LINES) * SAMPLES_PER_LINE + H_BACK_SAMPLES +
    static_cast<size_t>(2 * (VGA_FRAME_H - 1)) * SAMPLES_PER_LINE + ACTIVE_SAMPLES;
// The worst-case alignment origin must still leave the whole display span
// inside the ring: if the VSync fall lands this late, row 239 would read
// past the captured samples.
static_assert(DISPLAY_SPAN <= static_cast<size_t>(RING_SAMPLES),
              "display span overruns the capture ring");

// Origin verification: a legit arm jitters +-a few samples (absorbed);
// an arm that lands off by more than this means the frame's geometry is
// torn/rotated and the frame must not publish (~10 px worth of samples).
constexpr int ORIGIN_TOLERANCE_SAMPLES = 40;

// State machines inside PIO0.
constexpr uint SM_CLK = 0;
constexpr uint SM_SAMPLE = 1;

// The capture ring and frame buffer live in BSS: 210 KB + 76.8 KB of the
// RP2350's 520 KB SRAM, allocated at link time (no heap fragmentation).
uint32_t g_ring[RING_WORDS];
uint8_t g_frame_buffer[VGA_FRAME_W * VGA_FRAME_H];

volatile CaptureState g_state = CaptureState::Idle;
volatile int g_capture_row = -1;
volatile bool g_dma_done = false;
volatile bool g_rx_stalled = false;  // sampler blocked on full RX FIFO
uint32_t g_frame_serial_count = 0;
uint32_t g_reject_count = 0;     // RXSTALL-rejected frames
uint32_t g_seekfail_count = 0;   // seek/alignment failures
uint32_t g_clock_hz = VGA_CAPTURE_CLOCK_HZ;
uint8_t g_clk_delay = 5;     // ttclk half-period - 1, in SM cycles (div=1)
uint8_t g_sample_delay = 3;
float g_clk_div = 1.0f;      // only rises above 1 below ~2.34 MHz chip clock
unsigned long g_capture_start_ms = 0;
uint32_t g_non_black = 0;
unsigned long g_last_fail_ms = 0;

int g_dma_ch = -1;
dma_channel_config g_dma_cfg;
int g_off_clk = -1;
int g_off_sample = -1;
bool g_ready = false;

// Sample n lives in word n>>2; with shift-left + autopush at 32 bits the
// earliest sample is the MSB byte of each word.
inline uint8_t ring_sample(size_t n) {
    return static_cast<uint8_t>(g_ring[n >> 2] >> (24 - 8 * (n & 3)));
}

// uo mapping: {hsync, B0, G0, R0, vsync, B1, G1, R1}; 2 bits/channel
// packed as an index into the 64-entry 2-bit-RGB palette.
inline uint8_t uo_to_index(uint8_t uo) {
    uint8_t r = (((uo >> 0) & 1) << 1) | ((uo >> 4) & 1);
    uint8_t g = (((uo >> 1) & 1) << 1) | ((uo >> 5) & 1);
    uint8_t b = (((uo >> 2) & 1) << 1) | ((uo >> 6) & 1);
    return (r << 4) | (g << 2) | b;
}

void dma_irq_handler() {
    // Capture the backpressure flag FIRST (FDEBUG bits clear on read):
    // RXSTALL for the sampler means the SM blocked on a full RX FIFO at
    // some point during the span - the DMA fell behind, samples were
    // missed, and the frame's fixed-origin geometry is not contiguous.
    g_rx_stalled =
        pio0->fdebug & (1u << (PIO_FDEBUG_RXSTALL_LSB + SM_SAMPLE));
    // Quiesce the sampler NOW: until service() runs (potentially
    // milliseconds) it would keep pushing into a full FIFO, and that
    // post-completion stall must not read as a mid-frame failure. The
    // FIFO's four words cover the IRQ latency.
    pio_sm_set_enabled(pio0, SM_SAMPLE, false);
    dma_hw->ints1 = 1u << g_dma_ch;  // acknowledge
    g_dma_done = true;
}

void capture_pins_to_pio() {
    pio_gpio_init(pio0, PIN_CLK);
    pio_sm_set_consecutive_pindirs(pio0, SM_CLK, PIN_CLK, 1, true);
    for (int i = 0; i < 8; ++i) {
        pio_gpio_init(pio0, PIN_UO_OUT[i]);
    }
    pio_sm_set_consecutive_pindirs(pio0, SM_SAMPLE, PIN_UO_OUT_BASE, 8, false);
}

void capture_pins_to_sio() {
    gpio_set_function(PIN_CLK, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_CLK, GPIO_OUT);
    gpio_put(PIN_CLK, 0);
    for (int i = 0; i < 8; ++i) {
        gpio_set_function(PIN_UO_OUT[i], GPIO_FUNC_SIO);
        gpio_set_dir(PIN_UO_OUT[i], GPIO_IN);
    }
}

// Chip clock geometry: SM0 runs at sysclk (div = 1 whenever possible,
// because the PIO divider can only SLOW a state machine — a value below 1
// collapses to a CLKDIV of 0, which halts the SM outright) and its program
// delay sets the half period: C = SYS / (2 * (1 + clk_delay)). For very
// slow chip clocks the 5-bit delay field tops out at 31, so the divider
// carries the rest (always >= 1).
//
// The sampler samples ~(sample_delay + 2) SM cycles after the rising
// edge: ~30% into the chip period (registered outputs settle shortly
// after the edge; mid-eye for the sampled half).
void compute_delays(uint32_t chip_clk) {
    float half = static_cast<float>(SYS_HZ) /
                 (2.0f * static_cast<float>(chip_clk));  // SM cycles/half
    if (half <= 32.0f) {
        g_clk_div = 1.0f;
        int d = static_cast<int>(half + 0.5f) - 1;
        if (d < 0) d = 0;
        if (d > 31) d = 31;
        g_clk_delay = static_cast<uint8_t>(d);
    } else {
        g_clk_delay = 31;
        g_clk_div = half / 32.0f;
    }

    float period = 2.0f * (1.0f + g_clk_delay) * g_clk_div;  // SM cycles
    int s = static_cast<int>(period * 0.3f + 0.5f) - 2;
    if (s < 1) s = 1;
    if (s > 15) s = 15;
    g_sample_delay = static_cast<uint8_t>(s);
}

// Extraction origin lock: the first successful extraction calibrates the
// display origin (via the hsync-anchor search below); every later frame in
// this selection REUSES the constant. The per-frame anchor can flip between
// adjacent lines' hsync falls (arm/retarget jitter ~samples), which renders
// as every-other-frame jumping by ~1 line / a few px - the "old film out of
// sync" judder. A locked origin makes that impossible; the residual
// +-1-sample arm jitter is invisible. Reset on selection or capture-clock
// change (the confirm delay is rate-dependent in samples).
int g_locked_origin = -1;
uint32_t g_origin_mismatch = 0;

// Copy the frame into g_frame_buffer. The capture is seek-aligned to the
// VSync fall, but that fall lands at a fixed MID-LINE hpos (~56 on this
// chip), so the display origin must be anchored to HSync like the legacy
// build did: find the first HSync fall (uo[7]) in the ring's head, then
// +24 samples (the 48-clock back porch) = hpos 0 of the next line, then
// +33 border lines = display line 0's hpos 0.
bool extract_frame() {
    size_t h_fall = 0;
    bool found = false;
    for (size_t n = 1; n < 600; ++n) {
        if (((ring_sample(n - 1) >> 7) & 1) == 1 &&
            ((ring_sample(n) >> 7) & 1) == 0) {
            h_fall = n;
            found = true;
            break;
        }
    }
    if (!found) return false;

    const int found_origin = static_cast<int>(h_fall) + H_BACK_SAMPLES +
                             static_cast<int>(V_TOP_LINES) * SAMPLES_PER_LINE;
    if (g_locked_origin < 0) {
        g_locked_origin = found_origin;
        g_origin_mismatch = 0;
    } else if (found_origin - g_locked_origin > ORIGIN_TOLERANCE_SAMPLES ||
               g_locked_origin - found_origin > ORIGIN_TOLERANCE_SAMPLES) {
        // This capture's arm landed off-alignment: the ring content is
        // rotated relative to the locked origin, and publishing it would
        // show a frame torn/rolled by the offset. Reject (the continuous
        // re-arm recaptures within ~150 ms; the last good frame stays).
        // After several consecutive mismatches the geometry itself must
        // have changed (reset/glitch): re-lock on the next success.
        if (++g_origin_mismatch >= 5) g_locked_origin = -1;
        ++g_reject_count;
        Serial.println("[capture] REJECTED (origin mismatch - arm off alignment)");
        return false;
    } else {
        g_origin_mismatch = 0;
    }
    const int origin = g_locked_origin;
    g_non_black = 0;
    for (int r = 0; r < VGA_FRAME_H; ++r) {
        size_t line = origin + static_cast<size_t>(2 * r) * SAMPLES_PER_LINE;
        uint8_t* row = &g_frame_buffer[static_cast<size_t>(r) * VGA_FRAME_W];
        for (int c = 0; c < ACTIVE_SAMPLES; ++c) {
            uint8_t index = uo_to_index(ring_sample(line + c));
            row[c] = index;
            if (index != 0) ++g_non_black;
        }
        g_capture_row = r;
    }
    return true;
}

// Continuous background recapture: while the screensaver (project 33) is
// selected with its reset released and no free-run clock was requested,
// keep the engine fed. Every control path that needs the clock pin aborts
// via vga_capture_abort(); this check picks the loop back up.
void continuous_rearm_check() {
    if (current_project() != 33) return;
    if (digitalRead(PIN_U_RST_N) != HIGH) return;
    if (clock_running()) return;  // user explicitly asked for a free-run clock
    if (g_state == CaptureState::DoneFail &&
        millis() - g_last_fail_ms < 1000) {
        return;  // back off so a wiring fault cannot spam captures/prints
    }
    vga_capture_start_async();
}

}  // namespace

bool vga_capture_init() {
    memset(g_frame_buffer, 0, sizeof(g_frame_buffer));

    g_off_clk = pio_add_program(pio0, &ttclk_program);
    g_off_sample = pio_add_program(pio0, &ttsample_program);
    if (g_off_clk < 0 || g_off_sample < 0) {
        Serial.println("[vga_capture] PIO program load failed");
        return false;
    }
    // pio_add_program has been observed to return a valid offset WITHOUT
    // writing the instruction words on this core (instr_mem reads zero
    // right after boot). Copy them explicitly and verify.
    for (uint i = 0; i < ttclk_program.length; ++i) {
        pio0->instr_mem[g_off_clk + i] = ttclk_program_instructions[i];
    }
    for (uint i = 0; i < ttsample_program.length; ++i) {
        pio0->instr_mem[g_off_sample + i] = ttsample_program_instructions[i];
    }
    Serial.print("[vga_capture] program verify: clk0=0x");
    Serial.print(pio0->instr_mem[g_off_clk], HEX);
    Serial.print(" sample0=0x");
    Serial.println(pio0->instr_mem[g_off_sample], HEX);

    g_dma_ch = dma_claim_unused_channel(true);
    if (g_dma_ch < 0) {
        Serial.println("[vga_capture] no free DMA channel");
        return false;
    }
    g_dma_cfg = dma_channel_get_default_config(g_dma_ch);
    channel_config_set_transfer_data_size(&g_dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&g_dma_cfg, false);
    channel_config_set_write_increment(&g_dma_cfg, true);
    channel_config_set_dreq(&g_dma_cfg, pio_get_dreq(pio0, SM_SAMPLE, false));

    dma_channel_set_irq1_enabled(g_dma_ch, true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    compute_delays(g_clock_hz);
    g_ready = true;
    Serial.println("[vga_capture] PIO + DMA ready");
    return true;
}

bool vga_capture_start_async() {
    if (g_state == CaptureState::Running) return false;
    if (!g_ready) return false;

    // Take the pin away from any free-running clock so PIO owns it.
    clock_stop();

    // Patch the sampler's mid-eye delay (instruction index 1 = the nop
    // after the clock-edge wait). MOV Y,Y is the canonical encoding of nop
    // and touches nothing the sampler relies on.
    pio0->instr_mem[g_off_sample + 1] =
        pio_encode_mov(pio_y, pio_y) | pio_encode_delay(g_sample_delay);

    capture_pins_to_pio();

    // ---- Phase A: seek at a moderate chip clock. ----------------------
    // 8 MHz: the VSync pulse is 200 us (comfortably confirmable at the
    // 100 us re-check) and the seek averages half a frame period, keeping
    // the continuous loop at ~6 fps at the 4 MHz capture rate. SM0 runs
    // alone (the sampler stays disabled: no FIFO noise).
    constexpr uint32_t SEEK_HZ = 8000000;
    constexpr uint8_t SEEK_DELAY = 8;                      // 9 SM cycles/half
    const float SEEK_DIV = (SYS_HZ / (2.0f * SEEK_HZ)) / 9.0f;  // ~1.042
    pio0->instr_mem[g_off_clk + 0] =
        pio_encode_set(pio_pins, 1) | pio_encode_delay(SEEK_DELAY);
    pio0->instr_mem[g_off_clk + 1] =
        pio_encode_set(pio_pins, 0) | pio_encode_delay(SEEK_DELAY);
    pio_sm_config c0 = ttclk_program_get_default_config(g_off_clk);
    sm_config_set_clkdiv(&c0, SEEK_DIV);
    sm_config_set_set_pins(&c0, PIN_CLK, 1);
    pio_sm_init(pio0, SM_CLK, g_off_clk, &c0);

    pio_sm_config c1 = ttsample_program_get_default_config(g_off_sample);
    sm_config_set_in_pins(&c1, PIN_UO_OUT_BASE);
    sm_config_set_in_shift(&c1, false, true, 32);  // shift left, autopush 32
    sm_config_set_clkdiv(&c1, 1.0f);
    pio_sm_init(pio0, SM_SAMPLE, g_off_sample, &c1);

    pio_sm_set_enabled(pio0, SM_CLK, true);


    // Poll for the VSync pulse. At seek-rate clocking, every clock edge
    // couples a short spike into GP5 - a naive first-HIGH poll would
    // treat the first crosstalk spike as the pulse and arm at a random
    // phase. Require a SUSTAINED high: re-check 100 us later (the real
    // pulse is 2 lines wide - 800 us at the 2 MHz seek rate - while
    // crosstalk bursts decay far sooner). The yield keeps the USB/WiFi
    // background alive.
    const uint32_t vsync_mask = 1u << (PIN_UO_OUT_BASE + VSYNC_BIT);
    unsigned long t0 = millis();
    for (;;) {
        if ((gpio_get_all() & vsync_mask) != 0) {
            delayMicroseconds(100);
            if ((gpio_get_all() & vsync_mask) != 0) break;  // genuine pulse
        }
        yield();
        watchdog_update();  // bounded, but feed while waiting
        if (millis() - t0 > 500) {
            pio_sm_set_enabled(pio0, SM_CLK, false);
            capture_pins_to_sio();
            // Count as a capture failure so the re-arm loop's 1 s backoff
            // engages (otherwise a wiring fault spams seeks forever).
            g_last_fail_ms = millis();
            g_state = CaptureState::DoneFail;
            Serial.print("[capture] seek FAILED (no vsync pulse in 500 ms) at ");
            Serial.print(g_clock_hz);
            Serial.println(" Hz");
            ++g_seekfail_count;
            return false;
        }
    }
    {
        // ---- Phase B: fall detect + hot-retarget + arm, all masked. ---
        // Everything between the observed fall and the running capture is
        // deterministic. The fall is confirmed like the rise: a LOW seen
        // mid-pulse is usually a ringing dip, so re-check 100 us later.
        // The masked window is capped BY TIME: a flickering pin must
        // never hold interrupts off longer than ~2 ms (counting loop
        // iterations is not enough - each pass burns a 100 us confirm).
        uint32_t save = save_and_disable_interrupts();
        uint32_t t_masked = time_us_32();
        for (;;) {
            if ((gpio_get_all() & vsync_mask) == 0) {
                // busy_wait, NOT delayMicroseconds: sleep_us() waits on the
                // alarm pool's timer IRQ, which can never fire inside this
                // masked window (the wait is spin_unlock + __wfe) - the
                // loop deadlocked there whenever the WiFi core happened to
                // be quiet. busy_wait_us never sleeps.
                busy_wait_us(100);
                if ((gpio_get_all() & vsync_mask) == 0) break;  // genuine fall
            }
            if (time_us_32() - t_masked > 2000) {
                restore_interrupts(save);
                pio_sm_set_enabled(pio0, SM_CLK, false);
                capture_pins_to_sio();
                g_last_fail_ms = millis();
                g_state = CaptureState::DoneFail;
                Serial.println("[capture] FAILED (vsync fall not confirmed)");
                return false;
            }
        }

        // Hot-retarget the clock to the capture rate. The chip's counters
        // continue from the fall; only the rate changes.
        pio0->instr_mem[g_off_clk + 0] =
            pio_encode_set(pio_pins, 1) | pio_encode_delay(g_clk_delay);
        pio0->instr_mem[g_off_clk + 1] =
            pio_encode_set(pio_pins, 0) | pio_encode_delay(g_clk_delay);
        uint32_t div_int = static_cast<uint32_t>(g_clk_div);
        uint32_t div_frac =
            static_cast<uint32_t>((g_clk_div - div_int) * 256.0f + 0.5f);
        if (div_frac > 255) { div_frac = 0; ++div_int; }
        pio0->sm[SM_CLK].clkdiv = (div_int << 16) | (div_frac << 8);

        // Arm the capture inside the same window: sample 0 is now a
        // constant offset from the fall.
        pio_sm_clear_fifos(pio0, SM_SAMPLE);
        (void)pio0->fdebug;      // read-clears any stale RXSTALL flag
        g_rx_stalled = false;
        g_dma_done = false;
        dma_channel_acknowledge_irq1(g_dma_ch);
        dma_channel_configure(g_dma_ch, &g_dma_cfg,
                              g_ring,                    // write: ring
                              &pio0->rxf[SM_SAMPLE],     // read: PIO RX FIFO
                              RING_WORDS, true);
        pio_sm_set_enabled(pio0, SM_SAMPLE, true);
        // SM0 never stopped: clocking continues at the capture rate.
        restore_interrupts(save);
    }

    g_capture_row = -1;
    g_capture_start_ms = millis();
    g_state = CaptureState::Running;
    return true;
}

void vga_capture_service() {
    // Continuous recapture: after abort (Idle), failure (retry with
    // backoff), or a completed frame (DoneOk), re-arm while the
    // screensaver is live.
    if (g_state != CaptureState::Running) {
        continuous_rearm_check();
        return;
    }

    // Watchdog-style bounds check: a capture should complete within one
    // frame period plus the seek (the seek is bounded by its own timeout).
    // No DMA completion by ~3x that means the engine is wedged (wiring
    // fault) - fail cleanly instead of spinning as Running forever.
    if (!g_dma_done) {
        unsigned long expected_ms =
            3 * (525 + 513) * 800ULL * 1000ULL / g_clock_hz + 100;
        if (millis() - g_capture_start_ms > expected_ms) {
            vga_capture_abort();
            g_last_fail_ms = millis();
            g_state = CaptureState::DoneFail;
            Serial.print("[capture] FAILED (no vsync within ");
            Serial.print(millis() - g_capture_start_ms);
            Serial.println(" ms)");
        }
        return;
    }
    g_dma_done = false;

    // The frame period's samples are complete: quiesce the engine.
    // (The sampler was already disabled in the DMA IRQ; the clock stops
    // here.) g_rx_stalled was captured by the IRQ: RXSTALL means the SM
    // blocked on a full RX FIFO during the span - the DMA fell behind
    // the sampler (bus contention with WiFi/flash), samples were missed,
    // and the fixed-origin line geometry is no longer contiguous, so the
    // frame MUST NOT be published.
    pio_sm_set_enabled(pio0, SM_CLK, false);
    capture_pins_to_sio();

    bool ok = extract_frame();
    if (ok && g_rx_stalled) {
        ok = false;
        ++g_reject_count;
        Serial.print("[capture] REJECTED (RX FIFO stall - DMA fell behind) after ");
        Serial.print(millis() - g_capture_start_ms);
        Serial.println(" ms");
    }
    if (ok) {
        ++g_frame_serial_count;
    } else if (!g_rx_stalled) {
        g_last_fail_ms = millis();
        Serial.print("[capture] FAILED (no vsync) after ");
        Serial.print(millis() - g_capture_start_ms);
        Serial.println(" ms");
    }
    g_state = ok ? CaptureState::DoneOk : CaptureState::DoneFail;
}

void vga_capture_abort() {
    if (g_state != CaptureState::Running) return;
    dma_channel_abort(g_dma_ch);
    dma_channel_acknowledge_irq1(g_dma_ch);
    pio_sm_set_enabled(pio0, SM_CLK, false);
    pio_sm_set_enabled(pio0, SM_SAMPLE, false);
    capture_pins_to_sio();
    g_dma_done = false;
    g_state = CaptureState::Idle;
    Serial.println("[capture] aborted");
}

void vga_capture_reset_origin() {
    // A new selection means new video geometry (a different design's
    // mid-line vsync hpos): re-calibrate the extraction origin.
    g_locked_origin = -1;
}

bool vga_capture_busy() {
    return g_state == CaptureState::Running;
}

CaptureStatus vga_capture_status() {
    return {g_state, g_capture_row};
}

const uint8_t* vga_frame_buffer() {
    return g_frame_buffer;
}

uint32_t vga_frame_serial() {
    return g_frame_serial_count;
}

uint32_t vga_last_non_black() {
    return g_non_black;
}

uint32_t vga_reject_count() {
    return g_reject_count;
}

uint32_t vga_seekfail_count() {
    return g_seekfail_count;
}

uint32_t vga_capture_clock_hz() {
    return g_clock_hz;
}

void vga_capture_set_clock_hz(uint32_t chip_clk) {
    // Calibration range: the ramp experiments live between 1 and 30 MHz.
    if (chip_clk < 1000000) chip_clk = 1000000;
    if (chip_clk > 30000000) chip_clk = 30000000;
    g_clock_hz = chip_clk;
    // The locked origin is rate-dependent (the fall-confirm delay is a
    // fixed 100 us = a rate-dependent number of samples).
    g_locked_origin = -1;
    compute_delays(chip_clk);
    Serial.print("[capture] chip clock set to ");
    Serial.print(chip_clk);
    Serial.print(" Hz, clk_delay ");
    Serial.print(g_clk_delay);
    Serial.print(" (div ");
    Serial.print(g_clk_div, 3);
    Serial.print("), sample delay ");
    Serial.println(g_sample_delay);
}

bool vga_capture_self_test() {
    // The synthetic ring's hsync geometry differs from the live video's:
    // never inherit a locked origin from real captures.
    g_locked_origin = -1;
    // Fabricate a ring through an independent packing spec (words built
    // directly, not via the read path), then run the real extraction and
    // check the frame lands where the geometry says it must. The capture
    // is hardware-aligned: sample 0 corresponds to the VSync fall.
    memset(g_ring, 0, sizeof(g_ring));

    // Seek-aligned ring: the capture begins at the VSync fall, which lands
    // at a fixed mid-line hpos. Synthesize the same line structure the
    // hardware shows: the first HSync pulse (uo[7] = bit 7) high over
    // samples [300, 348), falling at 348 (matching the measured hpos-56
    // fall position). The extractor anchors to that fall.
    for (size_t n = 300; n < 348; ++n) {
        g_ring[n >> 2] |= static_cast<uint32_t>(1u << 7) << (24 - 8 * (n & 3));
    }
    const size_t origin = 348 + H_BACK_SAMPLES +
                          static_cast<size_t>(V_TOP_LINES) * SAMPLES_PER_LINE;
    // All display samples uo=0x01 -> palette index 0x20, except the very
    // first sample uo=0x02 -> palette index 0x08. A packing or origin bug
    // moves the 0x08 marker; the checks below pin it.
    const size_t span = static_cast<size_t>(2 * (VGA_FRAME_H - 1)) *
                            SAMPLES_PER_LINE + ACTIVE_SAMPLES;
    for (size_t n = origin; n < origin + span; ++n) {
        uint8_t v = (n == origin) ? 0x02 : 0x01;
        g_ring[n >> 2] |= static_cast<uint32_t>(v) << (24 - 8 * (n & 3));
    }

    bool ok = extract_frame();
    const uint8_t* fb = g_frame_buffer;
    size_t center =
        static_cast<size_t>(VGA_FRAME_H / 2) * VGA_FRAME_W + VGA_FRAME_W / 2;
    size_t last = static_cast<size_t>(VGA_FRAME_H - 1) * VGA_FRAME_W +
                  (VGA_FRAME_W - 1);
    bool checks = ok &&
                  fb[0] == 0x08 &&      // the single marker sample at (0,0)
                  fb[1] == 0x20 &&      // packing: marker must not shift
                  fb[3] == 0x20 &&      // (a byte-order bug puts it here)
                  fb[VGA_FRAME_W] == 0x20 &&   // row 1 start
                  fb[center] == 0x20 &&
                  fb[last] == 0x20 &&
                  g_non_black == static_cast<uint32_t>(VGA_FRAME_W) * VGA_FRAME_H;

    Serial.print("[capture] self-test ");
    Serial.print(checks ? "OK" : "FAILED");
    if (!checks) {
        Serial.print(" (ok=");
        Serial.print(ok ? 1 : 0);
        Serial.print(" fb0=0x");
        Serial.print(fb[0], HEX);
        Serial.print(" fb1=0x");
        Serial.print(fb[1], HEX);
        Serial.print(" fb3=0x");
        Serial.print(fb[3], HEX);
        Serial.print(" fbC=0x");
        Serial.print(fb[center], HEX);
        Serial.print(" fbL=0x");
        Serial.print(fb[last], HEX);
        Serial.print(" nonblack=");
        Serial.println(g_non_black);
    } else {
        Serial.println();
    }
    return checks;
}

void vga_capture_debug_print() {
    Serial.println("--- capture engine ---");
    Serial.print("state=");        Serial.print(static_cast<int>(g_state));
    Serial.print(" dma_ch=");      Serial.print(g_dma_ch);
    if (g_dma_ch >= 0) {
        uint32_t remaining = dma_channel_hw_addr(g_dma_ch)->transfer_count;
        Serial.print(" dma_remaining="); Serial.print(remaining);
    }
    Serial.print(" fifo_lvl_sm0="); Serial.print(pio0->flevel & 0xF);
    Serial.print(" fifo_lvl_sm1="); Serial.print((pio0->flevel >> 4) & 0xF);
    Serial.print(" sm0_en=");       Serial.print((pio0->ctrl >> 0) & 1);
    Serial.print(" sm1_en=");       Serial.print((pio0->ctrl >> 1) & 1);
    Serial.print(" sm0_clkdiv=0x"); Serial.print(pio0->sm[SM_CLK].clkdiv, HEX);
    Serial.print(" sm1_clkdiv=0x"); Serial.print(pio0->sm[SM_SAMPLE].clkdiv, HEX);
    Serial.print(" sm1_shiftctrl=0x"); Serial.print(pio0->sm[SM_SAMPLE].shiftctrl, HEX);
    Serial.print(" sm1_pc="); Serial.print(pio0->sm[SM_SAMPLE].addr);
    Serial.print(" off_sample="); Serial.print(g_off_sample);
    Serial.print(" off_clk="); Serial.print(g_off_clk);
    pio0->instr_mem[12] = 0xDEADBEEF;  // canary in a known-free slot
    Serial.print(" canary=0x"); Serial.println(pio0->instr_mem[12], HEX);
    Serial.print(" instr0=0x"); Serial.print(pio0->instr_mem[g_off_sample + 0], HEX);
    Serial.print(" instr1=0x"); Serial.print(pio0->instr_mem[g_off_sample + 1], HEX);
    Serial.print(" instr2=0x"); Serial.print(pio0->instr_mem[g_off_sample + 2], HEX);
    Serial.print(" instr3=0x"); Serial.println(pio0->instr_mem[g_off_sample + 3], HEX);
    Serial.print(" INSTR_MEM:");
    for (int i = 0; i < 32; ++i) {
        Serial.print(" ");
        Serial.print(pio0->instr_mem[i], HEX);
    }
    Serial.println();
    Serial.print(" sm0_pinctrl=0x"); Serial.print(pio0->sm[SM_CLK].pinctrl, HEX);
    Serial.print(" sm1_pinctrl=0x"); Serial.print(pio0->sm[SM_SAMPLE].pinctrl, HEX);
    Serial.print(" clk_pad_func=");  Serial.print(static_cast<int>(gpio_get_function(PIN_CLK)));
    Serial.print(" uo_pad2_func=");  Serial.print(static_cast<int>(gpio_get_function(PIN_UO_OUT[0])));
    Serial.print(" ring0=0x");       Serial.print(g_ring[0], HEX);
    Serial.print(" ring1=0x");       Serial.print(g_ring[1], HEX);
    Serial.print(" ringlast=0x");    Serial.print(g_ring[RING_WORDS - 1], HEX);
    Serial.print(" live_uo=0x");      Serial.print((gpio_get_all() >> PIN_UO_OUT_BASE) & 0xFF, HEX);
    Serial.print(" live_clk=");       Serial.print((gpio_get_all() >> PIN_CLK) & 1);
    Serial.print(" live_rst=");       Serial.print((gpio_get_all() >> PIN_U_RST_N) & 1);
    Serial.print(" live_gp5=");       Serial.println((gpio_get_all() >> (PIN_UO_OUT_BASE + 3)) & 1);
    Serial.print(" clock_hz=");      Serial.print(g_clock_hz);
    Serial.print(" clk_delay=");     Serial.print(g_clk_delay);
    Serial.print(" clk_div=");       Serial.print(g_clk_div, 3);
    Serial.print(" sample_delay=");  Serial.println(g_sample_delay);
    Serial.println("---------------------");
}

void vga_capture_ring_dump() {
    if (vga_capture_busy()) {
        vga_capture_abort();
    }
    // One fresh capture, synchronously.
    unsigned long t0 = millis();
    if (!vga_capture_start_async()) {
        Serial.println("[ring] could not start capture");
        return;
    }
    while (vga_capture_busy() && millis() - t0 < 2000) {
        // Service the engine ourselves; the loop's other work is paused.
        vga_capture_service();
        // Continuous rearm would kick in via service; suppress by keeping
        // project state out of the check - simplest: break as soon as the
        // DMA phase ends.
    }

    uint32_t nonzero = 0, hsync_words = 0, vsync_words = 0;
    for (size_t w = 0; w < RING_WORDS; ++w) {
        uint32_t v = g_ring[w];
        if (v != 0) ++nonzero;
        // Any byte with bit 7 set (hsync = uo[7]).
        if (v & 0x80808080u) ++hsync_words;
        // Any byte with bit 3 set (vsync = uo[3]).
        if (v & 0x08080808u) ++vsync_words;
    }
    Serial.print("[ring] words=");
    Serial.print(RING_WORDS);
    Serial.print(" nonzero=");
    Serial.print(nonzero);
    Serial.print(" hsync_words=");
    Serial.print(hsync_words);
    Serial.print(" vsync_words=");
    Serial.println(vsync_words);
    for (size_t w = 0; w < 8; ++w) {
        Serial.print(" w");
        Serial.print(w);
        Serial.print("=0x");
        Serial.println(g_ring[w], HEX);
    }
    Serial.print(" w100=0x");
    Serial.println(g_ring[100], HEX);
    Serial.print(" w10000=0x");
    Serial.println(g_ring[10000], HEX);
    Serial.print(" w30000=0x");
    Serial.println(g_ring[30000], HEX);
    Serial.print(" w51000=0x");
    Serial.println(g_ring[51000], HEX);
    Serial.print(" w52499=0x");
    Serial.println(g_ring[RING_WORDS - 1], HEX);
    Serial.print(" live_uo=0x");
    Serial.print((gpio_get_all() >> PIN_UO_OUT_BASE) & 0xFF, HEX);
    Serial.print(" live_gp5=");
    Serial.println((gpio_get_all() >> (PIN_UO_OUT_BASE + 3)) & 1);
}

void vga_capture_alignment_dump() {
    // Where is the (next) VSync fall inside the ring? A stable capture
    // start means a constant index; a jittering start smears it.
    long fall = -1;
    for (size_t n = 1; n < RING_SAMPLES; ++n) {
        if (((ring_sample(n - 1) >> VSYNC_BIT) & 1) == 1 &&
            ((ring_sample(n) >> VSYNC_BIT) & 1) == 0) {
            fall = static_cast<long>(n);
            break;
        }
    }
    // HSync pulse spacing: every 400 samples in a healthy capture.
    long first_hsync = -1, hsync_spacings_bad = 0, hsync_count = 0;
    long prev_hsync = -1;
    for (size_t n = 0; n < RING_SAMPLES; ++n) {
        if (((ring_sample(n) >> 7) & 1) == 1) {
            if (first_hsync < 0) {
                first_hsync = static_cast<long>(n);
            } else {
                long gap = static_cast<long>(n) - prev_hsync;
                // hsync pulse is 48 samples wide: a new pulse starts when
                // the gap approaches a line period.
                if (gap > 300) {
                    ++hsync_count;
                    // prev_hsync was the LAST high sample of the previous
                    // pulse, so a line-regular pulse shows gap ~352
                    // (400 - 48 wide).
                    if (gap < 340 || gap > 460) ++hsync_spacings_bad;
                }
            }
            prev_hsync = static_cast<long>(n);
        }
    }
    Serial.print("[align] vsync_fall_at=");
    Serial.print(fall);
    Serial.print(" hsync_first=");
    Serial.print(first_hsync);
    Serial.print(" lines=");
    Serial.print(hsync_count);
    Serial.print(" spacing_bad=");
    Serial.println(hsync_spacings_bad);
}

}  // namespace tt
