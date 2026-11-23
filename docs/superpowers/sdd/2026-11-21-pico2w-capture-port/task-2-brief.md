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

