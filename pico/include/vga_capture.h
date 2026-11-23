#pragma once

#include <stdint.h>

namespace tt {

// Downscaled VGA frame dimensions served by the web UI.
constexpr uint16_t VGA_FRAME_W = 320;
constexpr uint16_t VGA_FRAME_H = 240;

enum class CaptureState : int { Idle = 0, Running, DoneOk, DoneFail };
struct CaptureStatus { CaptureState state; int row; };  // row: -1 = none yet

// Initialize PIO programs, the DMA channel, and buffers. Call once at boot.
bool vga_capture_init();

// Start one frame-period capture on the hardware engine. Returns false if
// a capture is already running.
bool vga_capture_start_async();

// True while a capture is running.
bool vga_capture_busy();

// Current capture state and last completed extracted row.
CaptureStatus vga_capture_status();

// Pointer to the 8-bit indexed frame buffer (VGA_FRAME_W * VGA_FRAME_H).
const uint8_t* vga_frame_buffer();

// Number of successful captures since boot.
uint32_t vga_frame_serial();

// Non-black pixel count of the last extraction (diagnostics).
uint32_t vga_last_non_black();

// TEMP diagnostics counters for the sync-loss investigation.
uint32_t vga_reject_count();
uint32_t vga_seekfail_count();

// Capture chip clock: runtime-settable (clamped to 1..30 MHz, serial 'k').
// 4 MHz default: the calibration ramp ran clean at 4/8/12.5/25.175 MHz
// over dupont jumpers. (An earlier 1 MHz default came from misreading the
// masked-window delayMicroseconds() deadlock as radio/USB instability; the
// real cause was sleep_us() waiting on an alarm IRQ that can never fire
// with interrupts masked - see the Phase B comment in vga_capture.cpp.)
constexpr uint32_t VGA_CAPTURE_CLOCK_HZ = 4000000;
uint32_t vga_capture_clock_hz();
void vga_capture_set_clock_hz(uint32_t chip_clk);

// Call from the main loop: extracts completed captures, publishes rows,
// and keeps the continuous recapture loop armed while project 33 runs.
void vga_capture_service();

// Abort a running capture (control-precedence path).
void vga_capture_abort();

// Synthetic-ring extraction self-test ('E' serial command).
bool vga_capture_self_test();

// Snapshot the live engine state for the 'C' serial diagnostic.
void vga_capture_debug_print();

// Run one capture synchronously and dump raw ring statistics ('R').
void vga_capture_ring_dump();

// Ring alignment forensics ('Q'): index of the VSync fall and hsync
// spacing regularity of the last completed capture.
void vga_capture_alignment_dump();

}  // namespace tt
