# Design: Pico 2 W host for TTGF0P2 — fast VGA capture + full web showcase

Status: builder check complete — awaiting approval
Date: 2026-11-21
Owner: Tom

## Intent and why

Replace the FireBeetle 2 ESP32-E host with a Raspberry Pi Pico 2 W (RP2350) as
the controller for the Tiny Tapeout TTGF0P2 business-card chip, so the Wafer
screensaver (project 33) VGA capture is as fast as the silicon allows, while
keeping (and improving) the web showcase that demos all 52 projects.

Today's capture bit-bangs the chip clock from the ESP32 at 250 kHz and reads
the 8 `uo_out` pins per step, taking ~5.2 s end-to-end. The RP2350's PIO
peripheral can drive the chip clock at the design's real VGA rate (25.175 MHz)
and sample all 8 pins in parallel into DMA — turning capture into a ~17 ms
operation and making real-time frame serving possible. Tiny Tapeout's own demo
boards generate the design clock with an RP2040/RP2350-class MCU via
PWM/PIO (their worked example is a 25.179 MHz waveform), and their reference
bring-up firmware for this exact card is a Pico project, so this is the
ecosystem-standard direction, not a novelty.

The context for all of it: Tom is TA-ing the Tiny Tapeout workshop at
Hackaday Supercon 2026 and wants a self-contained demo station — captive-portal
web UI, no app — that visitors can drive, and that can eventually be reached
remotely.

## Goals

1. **Capture speed:** full 320×240 frame of project 33 in ≤50 ms; stretch
   goal real-time (60 fps-class) capture at a 25 MHz chip clock, subject to
   wiring signal integrity. No hard deadline — calibrate carefully and
   pursue the 25 MHz target rather than front-loading a shortcut.
2. **Feature parity** with the current ESP32 firmware: adjustable free-run
   clock (run/stop/frequency/single-step), `uo_out` LED readback, `ui_in`
   control, `uio` direction+value via the Waveshare MCP23017, `u_rst_n`
   release, BMP and raw-row frame endpoints, captive-portal AP
   (`TT-GF0P2-Showcase` / `tinytapeout`, 192.168.4.1), serial command
   console, ROM/TTPG bring-up check.
3. **Every project selectable from the web (decided):** the dropdown lists
   all 52 slots, each with a generic control card (ui_in bit toggles,
   clock controls, uo_out LEDs). Curated metadata/widgets for the three
   known designs — re-keyed to their true slot addresses (Wafer.space = 33,
   Simon = 51, LEDs Racer = 20; today's 519/384 entries only work via a
   mod-52 counter-wrap quirk) — and more can be curated over time.
4. **Living screensaver (decided):** while project 33 is selected,
   recapture continuously in the background so the existing 250 ms UI poll
   shows an animated ~2–4 fps screensaver with no UI redesign. Capture
   aborts and restarts on control changes instead of returning 409.
5. **Full-width inputs:** all 8 `ui_in` bits driven directly by Pico GPIOs
   (today only 3 are native). `uio[0:7]` via the MCP23017 the user already
   owns.
6. **Non-destructive migration:** the ESP32 firmware stays in the repo,
   untouched and buildable; the Pico firmware lives in a new `pico/`
   sub-project sharing the same `data/` web assets.

## Non-goals (this phase)

- Internet exposure / remote tunnel (the AP is enough for now; a relay is a
  later project).
- VGA capture for projects other than 33 (others keep LED-style `uo_out`
  readback).
- 640×480 full-resolution capture (stays 320×240 like today; a future bump).
- Curated metadata for all 52 projects (generic cards cover the rest).
- A true live-video streaming endpoint (30–60 fps MJPEG/WebSocket-class);
  continuous background recapture is this phase's cadence — the streaming
  feature can build on it later.
- Designing a PCB or using anything beyond soldered pin headers + dupont
  jumpers.

## Anti-goals

- **Never present a corrupt frame.** Because we drive the chip clock
  ourselves, the capture path must be self-timed: if DMA can't keep up, the
  clock pauses (PIO FIFO backpressure), it never skips a sample.
- Don't churn or discard the uncommitted ESP32 work or rewrite git history.
- No two-board hybrid.

## Constraints

### Pico 2 W hardware
- 26 usable GPIOs: GP0–GP22 (digital-only), GP26–GP28 (digital or ADC).
  GP23/GP24/GP25/GP29 are reserved for the wireless module. Onboard LED is
  `WL_GPIO0` ("LED" constant in the core), not GP25.
- RP2350A: 150 MHz dual M33, 520 KB SRAM, **3 PIO blocks**, 4-byte FIFOs,
  DMA with per-channel pacing; flash 4 MB (UF2 flashing via BOOTSEL).
- GPIOs are 3.3 V; GP26–GP28 have an internal reverse diode to IOVDD (fine
  for 3.3 V I2C use).
- WiFi 4 via CYW43439 on SPI; SoftAP supports up to 4 stations — enough for
  a workshop demo table.
- 3V3(OUT) pin budget ~300 mA: TT card VIO + MCP23017 is trivially inside.

### TTGF0P2 card
- TTPG variant (commit `ff701baa`), both VDD pad types tied to 3.3 V,
  power-gated: only the selected project is powered.
- Mux protocol: pulse `ctrl_sel_rst_n` low, pulse `ctrl_sel_inc` N times,
  assert `ctrl_ena`. `u_rst_n` LOW enables ROM auto-increment; released HIGH
  for normal projects.
- Clock ceiling ~66 MHz per TT specs (sky130 pad limit; GF 180 nm should be
  equal or better). TT demo boards clock designs at 25.175 MHz routinely.
- VGA timing of project 33: 800 clocks × 525 lines; `hsync` = `uo[7]`
  (active high, hpos 656–751), `vsync` = `uo[3]` (active high, vpos 490–491);
  back porch 48 clocks; top border 33 lines after VSync falls; color = 2-bit
  RGB packed across `uo[0..2]` and `uo[4..6]`.

### Waveshare MCP23017 board (X00332 rev S3A7)
- Default I2C address **0x27** (A0/A1/A2 pulled high; solder bridges pull
  them low). The new firmware will simply use 0x27 — no solder bridges
  needed.
- Onboard 10 kΩ pull-ups on SDA/SCL and on RESET (RESET pin not exposed,
  not wired).
- Power VCC from 3.3 V. 16 IOs on two headers: PA0–PA7, PB0–PB7.
- Datasheet rev D note (to verify at bring-up): GPA7/GPB7 may be
  output-only. Only affects *reading* `uio[7]` via PB7; fallback is wiring
  `uio[7]` to a spare Pico GPIO (GP0/GP1/GP28).

### Firmware/ecosystem
- Arduino core: arduino-pico (Earle Philhower) — provides WiFi
  (`CYW43`/lwIP), `WebServer` (ESP8266-style API, same shape as the ESP32's),
  `DNSServer`, `Wire` (any GPIOs), `ArduinoJson` compatibility, and full
  pico-sdk access for PIO/DMA from Arduino sketches.
- PlatformIO target: community Raspberry Pi platform (maxgerhardt
  platform-raspberrypi), board `pico2_w`. Fallback if PlatformIO fights:
  arduino-cli with the same core; upload is always available as UF2
  drag-and-drop via BOOTSEL.
- Web assets stay where they are: `data/index.html` (inlined as
  `include/index_html.h`, PROGMEM) and `data/projects.json` /
  `project_db.cpp`.

## Approaches considered

1. **ESP32 + software speedup** (rewrite the step loop with direct register
   access, ~1–4 MHz bit-bang ceiling): rejected — still CPU-bound, ~0.5 s
   frames at best, and the original ESP32 has no peripheral that can sample
   8 parallel pins at MHz rates.
2. **Pico 2 W + PIO capture** (chosen): the capture engine exists in
   hardware; the chip clock and sampling are generated and latched by the
   same clock domain, self-timed via FIFO backpressure; WiFi stack runs
   independently on the second core. 30–100× today's speed, reference-host
   alignment with TT's own tooling, and enough GPIOs that only `uio` needs
   the expander.
3. **Hybrid: ESP32 web host + Pico capture over serial**: rejected — the
   Pico 2 W already has WiFi, so a second board and a link protocol buys
   nothing but failure modes.

## The design

### Wiring

Solder a standard 40-pin header onto the Pico 2 W. All connections are
dupont jumpers. **Only the Pico drives the TT card** — disconnect the
FireBeetle first (shared pins must never be driven by two hosts).

#### Pico 2 W ↔ TTGF0P2 card (direct, all fast/interactive signals)

| TTGF0P2 signal | Pico GPIO | Physical pin | Direction |
|---|---|---|---|
| `uo_out[0]` | GP2 | pin 4 | TT → Pico |
| `uo_out[1]` | GP3 | pin 5 | TT → Pico |
| `uo_out[2]` | GP4 | pin 6 | TT → Pico |
| `uo_out[3]` | GP5 | pin 7 | TT → Pico |
| `uo_out[4]` | GP6 | pin 9 | TT → Pico |
| `uo_out[5]` | GP7 | pin 10 | TT → Pico |
| `uo_out[6]` | GP8 | pin 11 | TT → Pico |
| `uo_out[7]` | GP9 | pin 12 | TT → Pico |
| `clk` | GP10 | pin 14 | Pico → TT |
| `ctrl_ena` | GP11 | pin 15 | Pico → TT |
| `ctrl_sel_inc` | GP12 | pin 16 | Pico → TT |
| `ctrl_sel_rst_n` | GP13 | pin 17 | Pico → TT |
| `u_rst_n` | GP14 | pin 19 | Pico → TT |
| `ui_in[0]` | GP15 | pin 20 | Pico → TT |
| `ui_in[1]` | GP16 | pin 21 | Pico → TT |
| `ui_in[2]` | GP17 | pin 22 | Pico → TT |
| `ui_in[3]` | GP18 | pin 24 | Pico → TT |
| `ui_in[4]` | GP19 | pin 25 | Pico → TT |
| `ui_in[5]` | GP20 | pin 26 | Pico → TT |
| `ui_in[6]` | GP21 | pin 27 | Pico → TT |
| `ui_in[7]` | GP22 | pin 29 | Pico → TT |
| VDD (both VDD pad types, incl. the barred one) | 3V3(OUT) | pin 36 | Power |
| GND | any GND (pins 3, 8, 13, 18, 23, 28, 33, 38) | — | Ground |

`uo_out` is deliberately **contiguous (GP2–GP9)** so one PIO instruction
(`in pins, 8`) latches the whole byte. Sparing GP0/GP1 (UART0, debug) and
GP28.

#### Pico 2 W ↔ Waveshare MCP23017 board

| MCP23017 board | Connect to |
|---|---|
| VCC | Pico 3V3(OUT) (pin 36) |
| GND | Pico GND |
| SDA | GP26 (pin 31, I2C1 SDA) |
| SCL | GP27 (pin 32, I2C1 SCL) |
| INTA / INTB | leave unconnected |
| A0/A1/A2 jumpers | leave open (device answers at 0x27) |
| PB0–PB7 | `uio[0:7]` on the TT card |
| PA0–PA7 | unused (future expansion) |

Port A is intentionally unused now: `ui_in` is fully direct on the Pico,
so the expander only carries `uio`. (Contrast with the ESP32 firmware,
where the expander also drove `ui_in` — on the Pico it must **not**, to
avoid two drivers on the same pin.)

Total: 21 direct TT signals + 2 I2C = 23 of 26 GPIOs; spares GP0/GP1/GP28
(debug UART / `uio[7]` fallback).

### Firmware architecture (`pico/` PlatformIO project)

Same module shapes as the ESP32 tree, adapted:

- **pins/mux** — direct GPIO ports of the current logic; mux pulses are
  slow, unchanged semantics. New: all 8 `ui_in` bits direct.
- **clock** — free-run clock via a PWM slice on GP10, 10 Hz–25 MHz
  (floor matches today's UI/firmware; replaces the 100 kHz timer ceiling,
  web UI cap raised). Single-step unchanged.
- **io_expander** — Wire on GP26/GP27 (`Wire.setSDA/setSCL`), probes
  address `0x27` first (Waveshare factory default) then `0x20`,
  `uio` on port B only. Same register-level code otherwise. Note: the
  expander path has never run on hardware (the Waveshare board just
  arrived), so bring-up is a first-time validation, not a regression
  check.
- **project_db** — all 52 slots selectable from the web: the descriptor
  list covers the three known designs re-keyed to true slots (33, 51, 20);
  unknown slots get a generated generic descriptor (raw pinout, ui_in bit
  toggles, uo_out LEDs) so the dropdown and `/api/select` accept 0–51.
- **vga_capture (the core rewrite)** —
  - PIO block 0, SM0 (clock): toggles GP10 at the target chip-clock rate C
    (divider-configurable; 25 MHz at sysclk 150 with a 3-cycle half-period,
    slower by divider).
  - SM1 (sampler): waits on the clock pin's rising edge, delays a
    calibrated mid-eye constant, `in pins, 8`, repeats every 2nd clock
    (matching the 320-wide subsample), packs 4 samples per 32-bit FIFO
    word, autopush.
  - DMA: one channel paced by the PIO RX FIFO, single-shot of one full
    frame period: 210,000 samples (800 clocks/2 × 525 lines) = 52,500
    words = 210 KB, in a static BSS buffer. Completion IRQ marks the frame
    ready; while project 33 is selected, the capture re-arms immediately
    (continuous background recapture). FIFO backpressure means bus
    contention with WiFi never drops a sample — the clock simply pauses.
  - Extraction (CPU, a few ms): scan the ring for the VSync falling edge,
    skip 33 top-border lines, take every 2nd display line (240 rows) × the
    320 active samples per line, convert each `uo` byte to the same 6-bit
    palette index as today (`uo_to_index`), write into the existing
    76.8 KB frame buffer. Horizontal alignment via the 48-clock back porch
    (24 samples); ±1-sample phase is acceptable for a screensaver and is a
    calibration constant.
  - Frame rate at C = 25 MHz: 16.8 ms/frame; at 12.5 MHz: 33.6 ms. Either
    is fast enough that every web poll sees a fresh frame; the seek-phase
    preview streaming from the ESP32 version becomes unnecessary (kept
    only where harmless for API compatibility).
  - **Control precedence:** captures are cheap, so control endpoints no
    longer 409 while a capture runs — they abort it (stop the SMs, quiesce
    the DMA), perform the control change, and let the continuous loop start
    a fresh capture. This replaces the ESP32's 409 guards; 409 remains only
    where genuinely unrecoverable.
  - Auto-capture on project 33 select (same `vga_cache_request` pattern),
    then continuous recapture while it stays selected; other projects stop
    the loop and resume the free-run clock.
- **web_server** — endpoint-for-endpoint port to arduino-pico's
  ESP8266-style `WebServer`: `/api/state`, `/api/select`, `/api/clock`,
  `/api/step`, `/api/ui_in`, `/api/uio`, `/api/reset`, `/api/capture`,
  `/api/projects`, `/api/project`, `/api/frame.bmp`, `/api/frame_raw`,
  captive-portal probe redirects, `index.html` from PROGMEM. JSON via
  ArduinoJson. CORS headers preserved.
- **wifi/dns** — SoftAP + DNSServer captive portal, same SSID/password
  (documented in USAGE.md), gateway 192.168.4.1.
- **main/serial console** — same command set (`s`, `c`, `t`, `r`, `d`,
  `i`, `u`, `v`, `b`, `a`, `p`), now over USB CDC.
- **project_db / index_html** — shared, regenerated/kept in sync with
  `data/` (same inlining approach as the ESP32 build).

### Bring-up and calibration order

1. Solder headers, wire per tables, smoke test: ROM readback (address 0,
   `u_rst_n` high) must print `commit=ff701baa` (TTPG) — the same recipe
   the TT guide prescribes, replicated from the existing serial commands.
2. Port mux/clock/pins/serial first, validate project select + LED
   readback for a few known projects, then expander: `uio` gamepad-style
   project + PB7 readback test (GPA7/GPB7 caveat).
3. PIO capture at a conservative 4 MHz chip clock; verify vsync/hsync
   decode and a clean extracted frame; then ramp C over
   {4, 8, 12.5, 25.175} MHz, keeping the highest rate that yields stable,
   correctly-aligned frames (compare successive frame CRCs). Store the
   winner as the default; keep C adjustable.
4. Web port + browser validation from a phone captive portal.
5. Update `docs/WIRING.md` (Pico section), `USAGE.md`, and the build log
   (phase 3).

### Risks and mitigations

| Risk | Mitigation |
|---|---|
| 25 MHz over dupont jumpers is marginal | Calibrate down to 12.5/8/4 MHz; every step is still 15–100× today |
| Sample lands off mid-eye (chip clock-to-out over wires) | Mid-eye delay constant in SM1, calibrated during ramp test |
| PB7 cannot read `uio[7]` | Move `uio[7]` to GP28 (or GP0/GP1) direct |
| arduino-pico WebServer API gaps (e.g. `send_P`/`hostHeader`) | Near-1:1 ESP8266 heritage; trivial shims if absent |
| WiFi DMA contention corrupts capture | Impossible by construction (PIO FIFO backpressure pauses the clock) |
| Continuous capture vs. user controls | Abort-and-restart replaces 409 blocking; captures are ~17–50 ms, so interruptions are cheap |
| PlatformIO pico platform friction | arduino-cli + UF2 BOOTSEL flashing is the always-works fallback |
| 210 KB + 76.8 KB static buffers vs heap | Both in BSS; ~230 KB remains for lwIP/stacks on a 520 KB part |

## What's left to the builder

- Exact PIO program instructions/delays and DMA channel configuration.
- PWM slice details for the adjustable free-run clock.
- The index_html/projects.json inclusion mechanics in the Pico build
  (pre-build generation script preferred over hand-synced headers).
- Whether to keep `/api/frame_raw` preview semantics or simplify now that
  capture is instant (keep if it costs nothing).
- Calibration values (chip clock rate, mid-eye delay, back-porch phase).
- Minor: whether to expose the capture chip-clock rate in the web UI
  (today's UI already shows the capture clock rate; keep).
