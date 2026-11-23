# How this firmware was built

This document is a self-contained narrative of the AI-assisted development that produced `TinyTapeout-GF0P2-WebUI`. It covers the two major development phases, the bugs found during hardware bring-up, and the design decisions that shaped the final firmware. The original specs, plans, and session handoffs were kept in a separate development archive, but everything needed to understand the project is summarized here.

## Overview

The goal was a self-contained WiFi controller for the Tiny Tapeout TTGF0P2 business-card chip. A DFRobot FireBeetle 2 ESP32-E (DFR0654) selects any of the 52 muxed designs, drives the project clock, reads the `uo_out` byte, and serves a single-page web UI from a captive portal. Later work added live VGA frame capture for the Wafer.space screensaver project (project 33).

Development happened in two phases:

1. Mux control, clock, output readback, and the web UI.
2. Async VGA frame capture with row streaming.

## Phase 1: Mux control, clock, and output readback

The first phase scaffolded a PlatformIO project, defined the GPIO pinout, implemented the mux selection sequence, added an adjustable clock, and built a web UI served from the ESP32 in access-point mode.

Key implementation points:

- `pins.h` / `pins.cpp` map every FireBeetle pin to its TTGF0P2 signal and avoid ESP32 strapping pins.
- `mux.cpp` pulses `ctrl_sel_rst_n`, increments `ctrl_sel_inc` N times, then asserts `ctrl_ena` to select project N.
- `clock.cpp` uses LEDC for running clocks and bit-bangs single steps. High-frequency requests (above ~500 kHz) switch to 1-bit LEDC duty resolution so the VGA pixel clock (25.175 MHz) can be requested without error.
- `web_server.cpp` serves `data/index.html` as an inlined PROGMEM string and exposes `/api/state`, `/api/select`, `/api/clock`, and `/api/step`.
- WiFi runs in AP mode as `TT-GF0P2-Showcase` / `tinytapeout` so the showcase works without depending on a 2.4 GHz home router.

## Phase 1½: Stepping fix and board variant

Hardware bring-up revealed that stepping the clock did not advance `uo_out`. The root cause was that `select_project()` released `u_rst_n` HIGH, putting the chip ROM into address-supply mode instead of auto-increment mode. The fix was to keep `u_rst_n` LOW after selection and expose a serial `u<N>` command to release it manually for non-ROM projects.

During this session the board was identified as a **TTPG** (power-gated) variant by reading the 256-byte ROM header (`commit=ff701baa`). This matters because only the selected project is powered on TTPG boards.

## Phase 2: Live VGA frame capture

The original "Capture frame" button blocked for ~9.6 s while the firmware sampled a full 320×240 frame. The redesign moved capture to a FreeRTOS task pinned to core 0 and streamed partial rows to the web UI as they were captured, so the image fills in top-to-bottom and the server stays responsive.

Key implementation points:

- `read_uo_out()` was rewritten to read two GPIO registers in one shot instead of eight `digitalRead` calls, with a `static_assert` that fails the build if `pins.h` mappings drift.
- `vga_capture.cpp` runs an async task that seeks to VSync, samples 240 rows into a shared buffer, and publishes `capture_row` via `std::atomic`.
- `web_server.cpp` added `/api/capture`, `/api/frame_raw?from=N`, and 409 guards on endpoints that would corrupt a running capture.
- `data/index.html` switched from an `<img>` to a `<canvas>` and paints new rows on each poll tick, showing a rolling preview during the seek and snapping to the aligned frame once VSync is found.

Measured on hardware: a fresh selection takes ~5.2 s end-to-end (down from ~9.6 s), with ~2.0 s of visible "syncing… (live preview)" followed by the aligned fill.

## Phase 3: The Pico 2 W host (fast capture)

The ESP32's 250 kHz bit-bang capture (~5.2 s per frame) hit its ceiling, so
the showcase moved to a Raspberry Pi Pico 2 W (RP2350). The design spec and
implementation plan live in `docs/superpowers/`; the short version of the
journey:

- Wiring: `uo_out` contiguous on GP2–GP9 so one PIO instruction latches the
  byte, `clk` on GP10 driven by a PIO state machine, all 8 `ui_in` bits
  direct, the Waveshare MCP23017 on I2C1 carrying `uio` only.
- The capture engine: one PIO SM generates the chip clock, a second samples
  the 8 pins every 2nd clock into DMA (four samples packed per FIFO word,
  210 KB ring in BSS). If the DMA ever falls behind the sampler (bus
  contention with WiFi/flash), the frame is detected via the PIO's RXSTALL
  flag and REJECTED rather than published - a stalled sampler misses
  samples, which would silently corrupt the fixed-origin line geometry.
- Bring-up found and fixed, in order: (1) `pio_add_program`'s words needed
  an explicit copy to actually land (instr_mem read-back is blind, so this
  was verified by behavior — full-rate DMA timing — not by reading memory
  back); (2) a fractional SM divider below 1 collapses to a CLKDIV of 0,
  which halts the state machine outright — the clock rate is now set by
  patching the PIO program's delay fields at runtime, divider always ≥ 1;
  (3) PIO `wait` instructions never release on the externally-driven sync
  pins in this setup (verified with correct encodings, three wait forms,
  and hsync/vsync bisects), so frame alignment is done by a CPU seek at
  2 MHz (the VSync pulse is 2 lines wide there, safely pollable with
  yields while keeping the USB/WiFi background alive; the final fall-to-arm
  sequence runs with interrupts masked so frames never jitter) — and a
  review pass then caught the seek running before the mux enable, which
  had made every selection of project 33 align against an undriven bus;
  (4) long busy-polls starve the USB background — every long poll now
  yields; (5) the `uio_direction` descriptor convention is the *chip's*
  pin directions — the host expander mirrors it inverted, or it fights
  the design's own uio outputs (bus contention that broke Simon's
  segments; the pass-through bug was inherited from the ESP32 code,
  which had never run against real expander hardware).
- Calibration ramp: clean continuous extracts at 4, 8, 12.5, and 25.175 MHz
  over dupont jumpers. The shipped default is 4 MHz — favoring a
  rock-stable image over raw speed, per Tom's call — with the serial `k`
  command raising it on demand. A frame period at the default is ~105 ms
  (plus an ~8 MHz seek averaging half a frame period), ~6 fps continuous.
  A later 1 MHz default (and a "radio destabilization" theory) came from
  misreading a real firmware hang - see the Phase 4 note below.
- The web showcase gained the real 52-design address list (the mux space
  is sparse: 0, 1, 32, ... 615 — the old firmware's 519/384 were the true
  Simon/LEDs-Racer addresses all along, not counter-wrap tricks; dead
  slots like 5 select nothing and are rejected by the API), generic cards
  for un-curated designs, `u_rst_n` released on selection for every design
  (ROM auto-increment stays at address 0), continuous background recapture
  while project 33 is selected, and
  abort-on-control semantics replacing the ESP32's 409 guards (the
  Pico's single-loop design also means frame reads can never tear, unlike
  the ESP32's cross-core capture task).

## Key decisions and gotchas

- **Board definition:** The `firebeetle32` PlatformIO board assumes 16 MB flash, but this module has 4 MB and boot-loops with that definition. The project uses `board = esp32dev` with `board_build.partitions = huge_app.csv`.
- **Web server library:** `ESPAsyncWebServer` was planned but is no longer resolvable in the PlatformIO registry, so the built-in synchronous `WebServer` is used. Traffic is low enough that this is fine.
- **WiFi mode:** Station mode was abandoned because the user's router exposes only 5 GHz to clients and the ESP32 is 2.4 GHz only. AP mode makes the device portable.
- **`u_rst_n` default:** Kept LOW after `select_project()` so ROM auto-increment works out of the box. Non-ROM projects need reset release via the web UI or serial `u1`.
- **`uo_out` pin choice:** `uo_out[0..3]` are read through input-only ADC pins (GPIO34–GPIO39). They work as digital inputs but can never be driven as outputs.
- **Frame buffer size:** A 320×240 indexed frame is 76.8 KB. A single shared buffer is used instead of double-buffering to save heap; the rare torn-refresh race self-heals on the next poll.

## Source files to read

If you want to trace a specific feature through the code:

| Feature | Files |
|---|---|
| GPIO pin definitions | `include/pins.h`, `src/pins.cpp` |
| Mux/project selection | `include/mux.h`, `src/mux.cpp` |
| Clock control | `include/clock.h`, `src/clock.cpp` |
| WiFi access point | `include/wifi_config.h`, `src/wifi_config.cpp` |
| Web API and UI | `include/web_server.h`, `src/web_server.cpp`, `data/index.html` |
| VGA frame capture | `include/vga_capture.h`, `src/vga_capture.cpp` |
| Serial command parser | `src/main.cpp` |
