# Project 33 Input Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Exercise project 33's documented input modes from the web UI: tile/solid-color toggles, a virtual gamepad (pause/move the logo), truthful clock reporting, and a capture-rate selector.

**Architecture:** Descriptor-driven toggles (shared, both hosts); a Pico-only gamepad emulator module that bit-bangs the design's passive-listener protocol on ui[4..6] from the `/api/gamepad` handler; `/api/state` reports the capture clock whenever the engine owns the pin; a capture-rate selector posts to a new `/api/capture_clock` endpoint.

**Tech Stack:** arduino-pico firmware (C++), shared vanilla-JS web UI (`data/index.html`), PlatformIO.

**Spec:** `ai-dev-archive/projects/tinytapeout-gf0p2-web-showcase/specs/project33-input-controls/spec.md`

## Global Constraints

- Shared UI must degrade gracefully when Pico-only fields are absent (ESP32 build).
- Never sleep inside interrupt-masked windows; burst pulses use `busy_wait_us(3)`.
- User `ui_in` writes mask to 0x8F; emulator owns bits 4-6; readback merges.
- Both trees build-green: `~/.venv...` pio = `/home/tom/dev/.venv-platformio/bin/pio`; `pio run -d pico` and `pio run`.
- Tom commits; agents stage and suggest messages.

## Review Focus

- **ui_in clobber:** a user toggle (bits 0/1) must never disturb an in-flight gamepad burst (bits 4-6) and vice versa — expect independent pin writes. Test: Task 3 step toggling tile while bursts run.
- **Start double-toggle:** a browser that retries `/api/gamepad` with start=1 must not flip manual mode twice — expect one toggle per press/release pair. Test: Task 3 verification.
- **Clock reporting regression:** with a free-run clock set (Simon), `clock_freq_hz` must show the free-run value, not the capture rate. Test: Task 2.
- **ESP32 graceful absence:** ESP32 `/api/state` has no `gamepad`/`capture_hz` — UI must hide the d-pad and selector, and 33's toggles must still render (bits 0/1 native on ESP32). Test: Task 4 (build + code inspection of the UI guards).
- **Burst vs masked window:** a gamepad burst arriving while a capture is in flight must not corrupt the frame or the masked window — expect either a clean burst before/after the window (service ordering). Test: Task 3 soak with capture running.

---

### Task 1: Descriptor toggles for project 33 (both hosts)

**Files:**
- Modify: `pico/src/project_db.cpp` (JSON_33)
- Modify: `src/project_db.cpp` (ESP32 JSON_33)

**Interfaces:**
- Produces: `/api/project?address=33` JSON with `"inputs":[{"id":"tile","type":"toggle","bit":0,"label":"Tile logos"},{"id":"solid_color","type":"toggle","bit":1,"label":"Solid color"}]` — the UI's existing descriptor renderer builds the toggles.

- [ ] **Step 1: Edit both JSON_33 blobs** — replace `"inputs":[]` with the two toggles (exact JSON above; keep PROGMEM style on ESP32).
- [ ] **Step 2: Build both trees** — `pio run -d pico && pio run`. Expected: SUCCESS both.
- [ ] **Step 3: Verify served JSON** — `curl -s 'http://10.0.0.219/api/project?address=33'` shows the two inputs (after Task 4 flash; before flashing, verify via build only).

### Task 2: Truthful clock reporting + capture-rate selector

**Files:**
- Modify: `pico/src/web_server.cpp` (handle_state, new handle_capture_clock, route)
- Modify: `data/index.html` (clock display source + selector)

**Interfaces:**
- Produces: `/api/state` fields `clock_freq_hz` (capture rate when project 33 + u_rst_n high + no free-run clock; free-run clock otherwise; 0 only when nothing drives the pin) and `capture_hz` (configured capture rate, Pico-only); `/api/capture_clock` POST `{"hz": N}` (abort capture if busy, clamp 1-30 MHz via `vga_capture_set_clock_hz`).

- [ ] **Step 1: handle_state clock logic** — if `current_project()==33 && digitalRead(PIN_U_RST_N)==HIGH && !clock_running()` then `clock_freq_hz = vga_capture_clock_hz()`; add `"capture_hz": vga_capture_clock_hz()`.
- [ ] **Step 2: handle_capture_clock** — abort busy capture, clamp, apply, respond with state; register route POST + OPTIONS (CORS like the others).
- [ ] **Step 3: UI** — clock readout unchanged (it reads clock_freq_hz); add a rate `<select>` (1 MHz, 2 MHz, 4 MHz, 8 MHz, 12.5 MHz, 25.175 MHz) visible only when `state.capture_hz` exists and project is 33; onchange POSTs `/api/capture_clock`.
- [ ] **Step 4: Build both** — expected SUCCESS; graceful absence on ESP32 (selector hidden when `capture_hz` undefined).

### Task 3: Virtual gamepad emulator (Pico)

**Files:**
- Create: `pico/src/gamepad.cpp`, `pico/include/gamepad.h` (namespace tt: `gamepad_init()`, `gamepad_set_state(const GamepadState&)`, `gamepad_burst()`, `GamepadState{start,up,down,left,right}`)
- Modify: `pico/src/pins.cpp` (`set_ui_in` masks 0x8F; `ui_in_value()` merges emulator bits), `pico/src/web_server.cpp` (route + `"gamepad": true` in state), `data/index.html` (d-pad UI on project 33), `pico/src/main.cpp` (include + init call after pins)

**Interfaces:**
- Consumes: PIN_UI_IN pins (GP15-GP22; gamepad = bits 4,5,6 = GP19,GP20,GP21), `busy_wait_us`.
- Produces: `/api/gamepad` POST full state; `/api/state` `"gamepad": true`.

- [ ] **Step 1: gamepad module** — `gamepad_set_state` stores the state and runs one burst: 12 bits in order b,y,select,start,up,down,left,right,a,x,l,r — data = (state bit for that position, pressed=1); per bit: drive ui[6]=data, `busy_wait_us(3)`, clk(ui[5]) high, `busy_wait_us(3)`, clk low; after 12 bits: latch(ui[4]) high, `busy_wait_us(3)`, latch low.
- [ ] **Step 2: pins.cpp split-ownership** — user `set_ui_in` masks 0x8F; `ui_in_value()` returns `user_bits | gamepad_bits` (emulator caches its 3 bits).
- [ ] **Step 3: web wiring** — route POST `/api/gamepad` (JSON full state -> `gamepad_set_state` -> respond state) + OPTIONS; `handle_state` adds `"gamepad": true`.
- [ ] **Step 4: UI d-pad** — on project 33 + `state.gamepad`: render Start + cross-layout d-pad; pointerdown/pointerup/pointercancel/pointerleave post the full state; `visibilitychange` hidden posts all-released.
- [ ] **Step 5: Build** — `pio run -d pico` SUCCESS.
- [ ] **Step 6: Hardware verify** — flash (Task 4), select 33, then:
  - `curl -X POST .../api/gamepad -d '{"start":true,...}'` then release → frame_serial climbing but image static (manual mode), logo position frozen.
  - hold `{"up":true,"start":true}`-style state, fetch 5 frames over 2 s → logo moves up ~1 px/frame in tile=0 mode.
  - toggle tile/solid from the UI → frames show single logo / solid color.

### Task 4: Flash + verification + archive

**Files:**
- Modify: `ai-dev-archive/.../specs/project33-input-controls/` (ledger note), handoff addendum.

- [ ] **Step 1: Flash via COM9 1200bps touch + RP2350 drive E: UF2 copy.**
- [ ] **Step 2: curl verification per Tasks 1-3; [beat] heartbeat continuous.**
- [ ] **Step 3: Archive docs** — ledger ruling + handoff update; suggest commit `feat: project 33 input controls - tile/solid toggles, virtual gamepad, capture-rate selector, truthful clock reporting`.
