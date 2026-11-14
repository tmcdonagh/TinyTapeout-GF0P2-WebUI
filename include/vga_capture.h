#pragma once

#include <stdint.h>

namespace tt {

// Downscaled VGA frame dimensions served by the web UI.
constexpr uint16_t VGA_FRAME_W = 320;
constexpr uint16_t VGA_FRAME_H = 240;

enum class CaptureState : int { Idle = 0, Running, DoneOk, DoneFail };
struct CaptureStatus { CaptureState state; int row; };  // row: -1 = none yet

// Initialize frame-capture state. Call once at boot.
void vga_capture_init();

// Start a capture on the background task. Returns false if a capture is
// already running.
bool vga_capture_start_async();

// Current capture state and last completed row.
CaptureStatus vga_capture_status();

// True while a capture is running.
bool vga_capture_busy();

// Pointer to the 8-bit indexed frame buffer (VGA_FRAME_W * VGA_FRAME_H bytes).
const uint8_t* vga_frame_buffer();

// Number of successful captures since boot (0 = no frame yet).  The web UI
// uses it to notice when the cached image has been updated.
uint32_t vga_frame_serial();

// Number of preview rows published during the current seek phase (0 until
// preview rows are published; a failed seek leaves the last count until the
// next capture starts).  Wraps the frame buffer at VGA_FRAME_H.
uint32_t vga_preview_row();

// Effective clock frequency used during manual frame capture (Hz).
// A half-period of 2 µs gives a 4 µs full period → 250 kHz.
constexpr uint32_t VGA_CAPTURE_CLOCK_HZ = 250000;
uint32_t vga_capture_clock_hz();

// Request that a fresh frame be captured when project 33 is running.
// Call vga_cache_service() from the main loop to act on the request.
void vga_cache_request();

// Service a pending frame-cache request.  Starts an async capture for project
// 33 when u_rst_n is HIGH and no capture is running; defers the request if
// one is already in progress.  Safe to call every loop.
void vga_cache_service();

}  // namespace tt
