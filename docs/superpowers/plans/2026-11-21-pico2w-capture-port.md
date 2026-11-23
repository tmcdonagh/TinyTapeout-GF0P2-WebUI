# Pico 2 W Fast-Capture Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the TTGF0P2 web showcase to a Raspberry Pi Pico 2 W with PIO-driven VGA capture, so a full 320×240 frame takes ≤50 ms instead of ~5 s, all 52 projects are selectable from the web UI, and project 33 shows a continuously animating screensaver.

**Architecture:** New `pico/` PlatformIO sub-project (arduino-pico core) alongside the untouched ESP32 firmware. Two PIO state machines: SM0 generates the chip clock, SM1 samples the 8 `uo_out` pins (GP2–GP9, contiguous) every 2nd clock; DMA moves one full frame period (210,000 samples) into a static ring; the CPU extracts the aligned 320×240 frame and re-arms continuously while project 33 is selected. Web endpoints, captive portal, and serial commands keep the ESP32 API surface.

**Tech Stack:** PlatformIO (maxgerhardt platform-raspberrypi), arduino-pico (Earle Philhower) core, board `rpipico2w`, pico-sdk PIO/DMA/PWM APIs from Arduino sketches, ArduinoJson 7, WebServer + DNSServer + WiFi (CYW43), pyserial for host-side serial verification.

**Spec:** `docs/superpowers/specs/2026-11-21-pico2w-capture-design.md` — the design doc's wiring tables, constraints, and decisions are authoritative; this plan argues from it.

## Global Constraints

- 3.3 V logic only; the TT card is a TTPG variant (ROM commit `ff701baa`), both VDD pad types tied to 3V3(OUT) pin 36. Only the Pico drives the card — the FireBeetle stays disconnected.
- Pin map is fixed by the spec: `uo_out[0..7]` = GP2..GP9 (contiguous), `clk` = GP10, `ctrl_ena` = GP11, `ctrl_sel_inc` = GP12, `ctrl_sel_rst_n` = GP13, `u_rst_n` = GP14, `ui_in[0..7]` = GP15..GP22, expander I2C = GP26 (SDA) / GP27 (SCL). GP0/GP1/GP28 stay unwired (spares).
- Never present a corrupt frame: capture is self-timed (PIO FIFO backpressure pauses the clock); extraction reads only completed buffers; the web may see a torn frame mid-extract but it self-heals next poll (established behavior from the ESP32 build).
- Web API is contract-frozen: same endpoints, same JSON field names (`/api/state` keys include `preview_row: 0` so the old seek-preview UI branch stays dormant). The shared `data/index.html` must keep working on the ESP32 build too — UI changes must degrade gracefully when a new state field is absent.
- VGA timing constants (project 33): 800 clocks × 525 lines per frame; hsync = `uo[7]`, vsync = `uo[3]`; top border 33 lines; back porch 48 clocks; output 320×240 (every 2nd pixel, every 2nd line).
- Free-run clock range 10 Hz–25,000,000 Hz (floor matches today's UI; a 150 MHz PWM slice cannot go below ~9 Hz).
- **The executor never runs `git commit` or `git push`** — Tom commits. Each task ends by summarizing changes and suggesting a commit message.
- No hard deadline: calibrate carefully rather than cutting corners, and keep the capture chip-clock adjustable.
- All verification commands run from the repo root unless noted. Serial device: `/dev/ttyACM*` (Pico USB CDC). If the device is missing, follow the WSL2 usbipd workflow (`wsl2-usb-serial` skill) before proceeding.

## Review Focus

Most likely bite-points and where each is pinned by a test:

1. **Frame buffer read during extraction** (web polls `/api/frame_raw` while the CPU rewrites the 76.8 KB buffer): a torn frame must self-heal — `capture_state` flips to `running` during re-arm/extract and the UI repaints on next poll. Tested in Task 8 by polling twice and checking the second result is consistent.
2. **Control during capture** (`select`/`clock`/`step`/`reset` arriving mid-capture): must abort-and-restart, never 409-lock or leave PIO/DMA in a wedged state. Tested in Task 8 by interleaving selects with a running continuous capture.
3. **Expander absent or mis-wired**: firmware must boot, `ui_in` still works (direct GPIOs), `/api/uio` returns 503, `/api/state` reports `io_expander_present: false`. Tested in Task 4 with the expander's VCC wire pulled for one boot.
4. **Clock extremes / pin handover**: `freq_hz` below 10 or above 25e6 must clamp, not crash; switching GP10 between PWM (free-run), SIO (step), and PIO (capture) repeatedly must stay clean. Tested in Task 8 (`c1`, `c999999999`, `v`, `c0`, `v` cycle).
5. **Generic project card** (49 un-curated slots): `/api/project?address=37` must return valid JSON the existing UI renders. Tested in Task 7 by browsing an un-curated slot's card in the web UI.

---

### Task 1: Pico project scaffold + serial helper

**Files:**
- Create: `pico/platformio.ini`, `pico/src/main.cpp` (minimal), `pico/tools/serctl.py`
- Modify: nothing else. The root ESP32 project is untouched.

**Interfaces:**
- Produces: a buildable/flashable `pico/` project and `python3 pico/tools/serctl.py "<command>"` which prints the Pico's serial response (auto-detects the first `2e8a:` USB CDC port, 115200 baud, ~2 s settle timeout). Later tasks use it for every serial verification.

- [ ] **Step 1: Create `pico/platformio.ini`**

```ini
[env:pico2w]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = rpipico2w
framework = arduino
board_build.core = earlephilhower
board_build.filesystem_size = 0m
monitor_speed = 115200
lib_deps =
    bblanchon/ArduinoJson @ ^7.0.0
```

- [ ] **Step 2: Write minimal `pico/src/main.cpp`**

`setup()`: `Serial.begin(115200)` then print `Pico TT showcase scaffold` on a 1 s interval in `loop()` (heartbeat for flash/serial verification).

- [ ] **Step 3: Write `pico/tools/serctl.py`**

pyserial helper: find port by `serial.tools.list_ports` VID `0x2E8A`; open 115200; send the given line + `\n`; stream response lines until 1 s of silence; exit code 0. Host setup: `python3 -m pip install --user pyserial` if missing.

- [ ] **Step 4: Build**

Run: `pio run -d pico`
Expected: `SUCCESS` (first run downloads the platform/core; takes a while — let it finish).

- [ ] **Step 5: Flash**

Ask Tom to hold **BOOTSEL** while plugging the Pico in (first flash only), confirm `/dev/ttyACM*` appears (usbipd attach from Windows if needed — `wsl2-usb-serial` skill).
Run: `pio run -d pico -t upload`
Expected: upload succeeds via picotool; subsequent flashes auto-reboot (if auto-reboot flakes: BOOTSEL again — known arduino-pico/PlatformIO quirk).
Run: `python3 pico/tools/serctl.py ""`
Expected: heartbeat line printed.

- [ ] **Step 6: Suggest commit**

Suggest to Tom: `feat: scaffold Pico 2 W PlatformIO project and serial helper`

### Task 2: pins + mux + serial console (hardware bring-up)

**Files:**
- Create: `pico/include/pins.h`, `pico/src/pins.cpp`, `pico/include/mux.h`, `pico/src/mux.cpp`
- Modify: `pico/src/main.cpp` (full serial console)

**Interfaces:**
- Produces: `namespace tt` with `init_pins()`, `read_uo_out()` (`(uint8_t)(gpio_get_all() >> 2)` — GP2..GP9 contiguous; keep a `static_assert` on the pin map), `set_ctrl_ena/set_ctrl_sel_inc/set_ctrl_sel_rst_n/set_u_rst_n(bool)`, `set_ui_in(uint8_t)` (drives GP15..GP22 directly, all 8 bits), `ui_in_value()`, `print_pin_diagnostics()`, `select_project(uint16_t)`, `current_project()`, plus pin constants in `pins.h` per Global Constraints. Port `mux.cpp` verbatim from the ESP32 version (same pulse timings and sequence — they match the TT reference firmware) minus the `vga_capture.h` include (not yet present; Task 6 adds it back).
- Serial commands: `s<N>`, `c<N>` (Task 3), `t`, `r`, `d`, `i<XX>`, `u<N>`, `a`, `p` — same semantics and prints as `src/main.cpp` on the ESP32.

- [ ] **Step 1: Write `pins.h`/`pins.cpp` and port `mux.cpp`**

Direct GPIO via Arduino `digitalWrite`/`gpio_get_all`. No strapping pins on RP2350 — no pull-up resistors needed. Port the pulse sequence from `src/mux.cpp` unchanged.

- [ ] **Step 2: Write the serial console in `main.cpp`**

Copy the command parser shape from `src/main.cpp` (lines 47–166), omitting `v` and `b` (capture not yet built).

- [ ] **Step 3: Build and flash**

Run: `pio run -d pico -t upload`
Expected: SUCCESS.

- [ ] **Step 4: Hardware verification — mux + outputs + ROM readback**

With the TT card wired per the spec:
Run: `python3 pico/tools/serctl.py "a"` (auto-cycle 0–10)
Expected: each project prints a `uo_out` byte; at least some differ per project (mux counting works).
Run: `python3 pico/tools/serctl.py "s33"` then `"t"` ×5 then `"r"`
Expected: pin diagnostics print; `uo_out` shows the hsync/vsync bits of project 33 changing as the clock steps (its `uo[7]`/`uo[3]` are in the byte).
Then the TT guide's ROM recipe — the definitive check that all 8 `uo_out` wires are correct: `serctl "s0"`, `"u1"` (release reset), then loop `t`+`r` ~80 times and collect the bytes.
Expected: the bytes spell ASCII starting `shuttle=ttgf0p2` (the chip ROM header, TTPG variant `ff701baa`). A mis-wired `uo` pin shows up immediately as garbled characters; the 256-byte header is enough — no need to dump the full ROM.

- [ ] **Step 5: Hardware verification — ui_in**

Run: `python3 pico/tools/serctl.py "s33"` then `"i01"` then `"d"`
Expected: `ui_in=0x1` echoed and no errors (tile bit now direct on GP15).

- [ ] **Step 6: Suggest commit**

Suggest: `feat: port pins, mux select, and serial console to Pico`

### Task 3: PWM free-run clock

**Files:**
- Create: `pico/include/clock.h`, `pico/src/clock.cpp`
- Modify: `pico/src/main.cpp` (enable `c` command)

**Interfaces:**
- Produces: `clock_start(uint32_t freq_hz)` (clamped 10..25,000,000; PWM slice for GP10, 50% duty), `clock_stop()` (restores GP10 to SIO, driven LOW), `clock_step()`, `clock_running()`, `clock_frequency()`, `clock_target_frequency()` — same signatures as `include/clock.h` on the ESP32.

- [ ] **Step 1: Implement via one PWM slice**

`pwm_gpio_to_slice_num(10)`, channel A. Frequency = 150e6 / (div × (wrap+1)). For `freq ≥ 1000`: wrap = round(150e6/freq) − 2 clamped to ≥2, div = 150e6 / (freq × (wrap+1)). For `freq < 1000`: wrap = 65535, div = 150e6 / (freq × 65536). Set div with `pwm_set_clkdiv_int_frac` (integer + 4/16 fraction). Level = (wrap+1)/2 for 50% duty. `clock_step()` = `clock_stop()`, 10 µs high, low — same as ESP32.

- [ ] **Step 2: Build, flash, verify range + step**

Run: `pio run -d pico -t upload`
Run: `serctl "c1"` → expect `Clock started: 10 Hz`; `"c999999999"` → `25000000 Hz`; `"c0"` → `Clock stopped`; `"t"` → `Clock stepped`.
Run: `"c10"` then `"r"` twice, 1 s apart
Expected: with project 33 selected, `uo_out` differs between reads at 10 Hz (counters advancing).

- [ ] **Step 3: Suggest commit**

Suggest: `feat: adjustable PWM clock 10 Hz to 25 MHz on Pico`

### Task 4: MCP23017 expander over I2C1

**Files:**
- Create: `pico/include/io_expander.h`, `pico/src/io_expander.cpp`
- Modify: `pico/src/main.cpp` (boot call, expander line in `d` diagnostics, new `w<XX>`/`o<XX>`/`O` uio serial commands)

**Interfaces:**
- Consumes: `Wire1` (GP26 = I2C1 SDA, GP27 = I2C1 SCL; call `Wire1.setSDA(26); Wire1.setSCL(27); Wire1.begin();` before probing, clock 400 kHz).
- Produces: `io_expander_init()` (probe address `0x27` first, then `0x20`; remember the found address), `io_expander_present()`, `ui_in_write(uint8_t)` (no-op port A on the Pico build — `ui_in` is direct; keep the symbol so `pins.cpp` ports cleanly), `uio_set_direction(uint8_t)`, `uio_write(uint8_t)`, `uio_read()` — same semantics as `src/io_expander.cpp` (IODIRB/GPIOB registers).

- [ ] **Step 1: Port `src/io_expander.cpp` to Wire1 with address probing**

- [ ] **Step 2: Build, flash, verify present + uio loopback-ish test**

Run: `pio run -d pico -t upload`
Run: `serctl "d"` → expect `MCP23017 detected at 0x27` (Waveshare default, jumpers open).
Run: `serctl "s51"` (Simon's slot — the descriptor arrives in Task 5; here just drive it raw) then `serctl "w7F"` (new `w<XX>` command: set uio direction mask, `0x7F` = segments as outputs) → `d` shows `uio_dir=0x7F`.
Then drive patterns and read back: add `o<XX>` command (sets uio value) and `O` (prints uio readback).
Expected: writes succeed; `uio_read` returns the pin state (segments driven read back matching where the design doesn't override; differences explained by the design actively driving). If `uio[7]` never reads: flag GP28 fallback for Tom (spec's risk table).

- [ ] **Step 3: Absent-expander boot test**

Ask Tom to pull the expander VCC wire for one reboot.
Run: `serctl "d"`
Expected: `MCP23017 not detected`, firmware otherwise healthy, `ui_in` still works. Restore wiring.

- [ ] **Step 4: Suggest commit**

Suggest: `feat: MCP23017 uio via I2C1 with 0x27/0x20 probing on Pico`

### Task 5: 52-slot project database with generic descriptors

**Files:**
- Create: `pico/include/project_db.h`, `pico/src/project_db.cpp`
- Modify: `pico/src/main.cpp` (boot init + `L` command listing slots)

**Interfaces:**
- Produces: `project_count()` = 52; `project_by_address(uint16_t)` returns `nullptr` only for address ≥ 52; curated descriptors keep their fields with addresses re-keyed — Wafer.space stays 33, Simon 519→**51**, LEDs Racer 384→**20** (their JSON blobs' `address` fields updated to match; titles/macros/widgets unchanged); `ProjectDescriptor.json_blob` is `nullptr` for un-curated slots; new `String generic_project_json(uint16_t address)` builds the generic card JSON; `project_address/title/macro/index` cover all 52.

- [ ] **Step 1: Define the generic card JSON (rendered by the existing UI)**

`generic_project_json(N)` returns: `address=N`, `title="Project N"`, `macro=""`, `description="Un-curated design — generic controls"`, `clock_hz=10000`, pinout arrays with raw bit labels (`ui.0`..`ui.7`, `uo.0`..`uo.7`, `uio.0`..`uio.7`), `uio_direction="0x00"`, `inputs` = eight `{"id":"ui_bit_<i>","type":"toggle","bit":<i>,"label":"ui.<i>"}` entries, `outputs` = `[{"type":"leds","bits":[0..7],"labels":["uo.0"..."uo.7"]}]`. Built with ArduinoJson (not hand-concatenated strings).

- [ ] **Step 2: Build, flash, verify**

Run: `pio run -d pico -t upload`
Run: `serctl "L"` (new command: prints one line per slot `N: title`)
Expected: 52 lines; lines 33/51/20 show curated titles; line 37 shows `Project 37`.
Run: `serctl "s37"` then `"d"`
Expected: select succeeds (validates `select` no longer 400s on generic slots), `uio_dir=0x00`.

- [ ] **Step 3: Suggest commit**

Suggest: `feat: all 52 slots selectable with generic project cards`

### Task 6: PIO + DMA capture engine with extraction

**Files:**
- Create: `pico/src/capture.pio` (two programs), `pico/include/vga_capture.h`, `pico/src/vga_capture.cpp`
- Modify: `pico/src/mux.cpp` (re-add the project-33 capture request on select), `pico/src/main.cpp` (`v`, `b`, `E` commands; `k<N>` capture-clock override)

**Interfaces:**
- Produces: same public API as `include/vga_capture.h` minus preview: `vga_capture_init()`, `vga_capture_start_async()`, `vga_capture_status()` (`CaptureStatus{state,row}`; row −1 during DMA, 0..239 published during extraction), `vga_capture_busy()`, `vga_frame_buffer()`, `vga_frame_serial()`, `vga_capture_clock_hz()` (`VGA_CAPTURE_CLOCK_HZ` default 12,500,000, runtime-settable), `vga_capture_set_clock_hz(uint32_t)`, `vga_capture_service()` (called from `loop()`: completion → extract → publish → re-arm if project 33 selected), `vga_capture_abort()`. `VGA_FRAME_W=320`, `VGA_FRAME_H=240`.
- Internal constants: `RING_WORDS = 52500` (210,000 samples = 800/2 × 525), `SAMPLES_PER_LINE = 400`, `ACTIVE_SAMPLES = 320`, `H_BACK_SAMPLES = 24`, `V_TOP_LINES = 33`, vsync = `uo[3]` (bit 3 of each sample byte).

- [ ] **Step 1: Write the two PIO programs in `capture.pio`**

```
.program ttclk          ; SM0: drives GP10, 50% duty
    set pins, 1 [9]
    set pins, 0 [9]
```
Chip clock C = sysclk / (20 × div0) → `div0 = 150e6 / (20 × C)` (8.8 fixed-point; supports 750 kHz–66 MHz; capture uses 4–25.175 MHz).

```
.program ttsample       ; SM1: div=1, samples every 2nd clock
wrap:
    wait 1 gpio 10      ; rising edge of sampled clock
    nop [SAMPLE_DELAY]  ; mid-eye delay, ~30% of chip period in sysclk cycles
    in pins, 8          ; latch GP2..GP9 (uo_out[0..7], GP2 = bit 0)
    wait 0 gpio 10
    wait 1 gpio 10      ; skipped clock
    wait 0 gpio 10
    jmp wrap
```
`in`-shift left with autopush threshold 32 → each FIFO word packs 4 samples: `(s0<<24)|(s1<<16)|(s2<<8)|s3`. `SAMPLE_DELAY` default 3 (≈20 ns; auto-adjust with C: `delay = clamp(chip_period_sm_cycles × 0.3 − 2, 1, 15)` recomputed when C changes).
Program budget check at C = 25.175 MHz: 7 instructions ≈ 7+S SM cycles per 2 chip clocks ≈ 11.9 available — fits with S ≤ 4.

- [ ] **Step 2: Implement the engine**

`pio0`; SM0 `set`-pin base 10 (output, `pindirs` out), SM1 `in`-pin base 2, both via `pio_add_program`/`pio_sm_config`. DMA: claimed channel, `DMA_SIZE_32`, read `&pio->rxf[sm1]`, write `g_ring` (static BSS `uint32_t g_ring[RING_WORDS]`), DREQ = `pio_get_dreq(pio, sm1, false)`, transfer count 52500, completion IRQ sets a flag consumed by `vga_capture_service()`. Capture sequence: `clock_stop()` → reconfigure GP10 to PIO → enable SM1 then SM0 → arm DMA. Abort: disable both SMs, `dma_channel_abort`, GP10 back to SIO-LOW.
Extraction from the completed ring: scan for the first index `i` where vsync bit falls (sample[i−1].bit3 == 1 && sample[i].bit3 == 0). Frame origin = `i + V_TOP_LINES×400 + H_BACK_SAMPLES`. Row `r` (0..239) = origin + `(2r)×400`, copy `ACTIVE_SAMPLES` bytes (unpacking each word little-endian: byte `j` of sample `n` = `(g_ring[n>>2] >> (24 − 8*(n&3))) & 0xFF`... i.e. byte order s0=MSB), map each through `uo_to_index` (same bit math as the ESP32 `vga_capture.cpp`) into `static uint8_t g_frame_buffer[76800]`. Publish `status.row` per completed row. No vsync found → `DoneFail`.
Memory: `g_ring` 210 KB + `g_frame_buffer` 76.8 KB static — verify the linker fits them in BSS (520 KB SRAM).

- [ ] **Step 3: `E` self-test on a synthetic ring (no clock needed)**

Fabricate a ring: line pattern with hsync/vsync bits and a ramp of color indices; run extraction; print first/center pixel + rows completed.
Run: `serctl "E"` → Expected: `self-test OK` with expected pixel values (deterministic from the fabricated pattern; treat mismatch as a byte-order/extraction bug).

- [ ] **Step 4: Real capture verification**

Run: `serctl "s33"` then `"v"`
Expected: `Capture result: OK`, non-black pixel count > 0, `First pixel` / `Center pixel` printed; total wall time printed < 50 ms at 12.5 MHz (add `captured in N ms` print).
Run: `serctl "b"` → hex dump begins with non-trivial data.

- [ ] **Step 5: Suggest commit**

Suggest: `feat: PIO+DMA VGA capture with self-timed sampling on Pico`

### Task 7: WiFi AP, captive portal, web server, inlined UI

**Files:**
- Create: `pico/include/wifi_config.h`, `pico/src/wifi_config.cpp`, `pico/include/dns_server.h`, `pico/src/dns_server.cpp`, `pico/include/web_server.h`, `pico/src/web_server.cpp`, `pico/include/index_html.h`, `pico/scripts/gen_headers.py`
- Modify: `pico/platformio.ini` (`extra_scripts = pre:scripts/gen_headers.py`), `pico/src/main.cpp` (boot/loop calls), shared `data/index.html` (two graceful-degradation tweaks)

**Interfaces:**
- Produces: `wifi_init()`, `dns_server_init()/dns_server_loop()`, `web_server_init()/web_server_loop()`, and the full endpoint set from `src/web_server.cpp`: `/api/state` (all existing keys incl. `preview_row: 0`, plus `ui_in_native: true`), `/api/select`, `/api/clock`, `/api/step`, `/api/ui_in`, `/api/uio`, `/api/reset`, `/api/capture`, `/api/projects` (52 entries), `/api/project` (curated blob or `generic_project_json`), `/api/frame.bmp` (same BMP header+palette builder), `/api/frame_raw` (row streaming — buffer is complete per Task 6, serve all 240 rows), captive redirects, `/` serving inlined HTML.
- `gen_headers.py` (PlatformIO pre-action) regenerates `pico/include/index_html.h` from `data/index.html` as a `PROGMEM` string named `INDEX_HTML`, so the shared file is the single source.

- [ ] **Step 1: Port wifi/dns/web modules**

SoftAP `TT-GF0P2-Showcase` / `tinytapeout`, `WiFi.softAPConfig(192.168.4.1, 192.168.4.1, 255.255.255.0)` **before** `WiFi.softAP(...)`; DNSServer on 53 wildcard → 192.168.4.1. WebServer handlers port near-1:1 (ESP8266-style API: `server.on`, `send`, `sendContent`, `sendHeader`, `hostHeader`); if an API is missing in arduino-pico, shim it (spec's risk table). Frame endpoints and the 409 guards port as-is for now (Task 8 revisits).

- [ ] **Step 2: Two `data/index.html` tweaks (both must be no-ops on the ESP32 build)**

`isBitAvailable`: `if (state.ui_in_native) return true;` before the existing logic (ESP32 omits the field → falls through). Clock input `max` raises to 25000000 (UI-only; firmware clamps anyway — ESP32 firmware still clamps to its own 100 kHz).

- [ ] **Step 3: Build, flash, browser verification**

Run: `pio run -d pico -t upload`
Connect a laptop/phone to `TT-GF0P2-Showcase`; expect the captive portal to open `192.168.4.1`.
Verify: project dropdown lists 52 entries; select **33** → auto-captured frame appears on the canvas; select **51** (Simon) → buttons drive LEDs, 7-seg renders; select an un-curated slot → generic card with 8 `ui.<i>` toggles and `uo.0..uo.7` LEDs (Review Focus #5); clock controls start/stop the free-run clock; `/api/frame.bmp` downloads a valid BMP.

- [ ] **Step 4: Suggest commit**

Suggest: `feat: web showcase on Pico — AP, captive portal, all endpoints, inlined UI`

### Task 8: Continuous recapture + abort-on-control

**Files:**
- Modify: `pico/src/vga_capture.cpp` (continuous re-arm in `vga_capture_service`), `pico/src/web_server.cpp` (remove 409 guards; abort before control), `pico/src/mux.cpp` (select aborts + restarts), shared `data/index.html` (capture UX text only if needed)

**Interfaces:**
- Consumes: `vga_capture_abort()` from Task 6.
- Produces: while `current_project() == 33` and `u_rst_n` released, captures run back-to-back (re-arm immediately after extraction); any control (`/api/select`, `/api/clock`, `/api/step`, `/api/reset`, serial equivalents) calls `vga_capture_abort()` first, applies the change, and lets the service loop re-arm. 409 remains only on `/api/capture` when already running.

- [ ] **Step 1: Implement continuous mode + abort precedence**

- [ ] **Step 2: Verify animated screensaver**

Browser on project 33: watch the canvas for ~10 s.
Expected: image visibly animates (screensaver motion; ~2–4 fps at the 250 ms poll), no manual capture button needed.

- [ ] **Step 3: Verify abort paths (Review Focus #2, #4)**

From the browser on project 33 with captures running: fire select/clock/step rapidly; then serial `serctl "c1"`, `"c999999999"`, `"v"`, `"c0"`, `"v"`.
Expected: no 409 lockups, no wedged captures (`/api/state` shows `capture_state` cycling ok/idle), frames keep flowing afterwards; clamps print as 10 Hz / 25 MHz.

- [ ] **Step 4: Verify torn-read self-heal (Review Focus #1)**

Rapid `/api/frame_raw` polls during capture; compare consecutive reads.
Expected: occasional partial rows at exactly one capture boundary; the next read is complete (self-heals), same as the ESP32 behavior.

- [ ] **Step 5: Suggest commit**

Suggest: `feat: continuous background recapture with abort-on-control`

### Task 9: Clock-rate calibration, defaults, and docs

**Files:**
- Modify: `pico/src/vga_capture.cpp` (final default C and `SAMPLE_DELAY`), `docs/WIRING.md` (Pico section), `docs/USAGE.md`, `docs/ai-build-log.md` (Phase 3), `README.md` (features + photo/table refresh), `.github/workflows/build.yml` (add `pio run -d pico` job)

**Interfaces:**
- Consumes: `vga_capture_set_clock_hz` + `k<N>` serial override from Task 6.

- [ ] **Step 1: Ramp the chip clock**

For C in {4e6, 8e6, 12.5e6, 25.175e6}: `serctl "k<C>"` then three `v` captures; compare `non-black` counts and frame CRC (add a CRC32 print to `v`).
Expected: identify the highest C where three consecutive CRCs match and the frame is visually correct (dump via `b` and render the hex if unsure). Record the winner and its `SAMPLE_DELAY`.

- [ ] **Step 2: Bake in the winner as the default** in `vga_capture.h`; keep `k<N>` for future experiments.

- [ ] **Step 3: Docs**

WIRING.md: add the Pico pin tables from the spec (replace the ESP32 table's "host" role, keep ESP32 tables under a legacy heading); USAGE.md: continuous-capture behavior + all-52 dropdown; ai-build-log: Phase 3 narrative (what was built, bugs found, calibration results); README: updated feature list ("Select any project 0–51", "fast/animated VGA capture").

- [ ] **Step 4: CI**

Extend `.github/workflows/build.yml` with a `pio run -d pico` job mirroring the ESP32 job's PlatformIO setup.

- [ ] **Step 5: Full end-to-end pass**

Fresh boot → AP → phone captive portal → select 33 (animated), select 51 (Simon playable), generic slot renders, clock controls work, `d` diagnostics sane.
Expected: everything above with no serial errors over a 5-minute soak.

- [ ] **Step 6: Suggest commit**

Suggest: `docs: calibrate capture clock, document Pico host, CI for pico project`
