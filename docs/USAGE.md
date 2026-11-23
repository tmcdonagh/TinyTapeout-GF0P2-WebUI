# Using the web UI

1. On your phone or laptop, connect to the WiFi network:
   - **SSID:** `TT-GF0P2-Showcase`
   - **Password:** `tinytapeout`
2. The phone should show a "Sign in to network" / captive portal notification within a few seconds. Tap it — the web UI opens automatically.
   - If the portal notification does not appear, open a browser and go to `http://192.168.4.1/`.
   - If the page still does not load, make sure mobile data is off for this test; the captive portal normally routes the request over WiFi, but some phones need a moment to switch.
3. Choose a project from the **Project** dropdown — it lists exactly the
   52 real TTGF0P2 mux addresses from the chip datasheet (the address space
   is sparse: 0, 1, 32, 33, 38, ... 615 — anything not listed is an empty
   slot with outputs held low). Curated showcases:
   - **Wafer.space Logo VGA Screensaver** (33)
   - **Simon Says memory game** (519)
   - **LEDs Racer** (384)
   Every other design still gets a generic card (raw `ui_in` bit toggles,
   clock controls, `uo_out` LEDs) so you can poke at it from a phone.
4. Use the clock presets or click the pencil to enter a custom frequency.
5. Click **Run** to start the clock, **Stop** to halt it, or **Step** to advance one cycle.
6. The page renders project-specific input and output widgets automatically.
   - On the Pico 2 W host, all 8 `ui_in` bits are direct GPIO and always
     usable; `uio[0:7]` comes through the MCP23017 IO expander.
   - On the legacy ESP32 host without the expander, only the native
     `ui_in` bits (0, 1, 4) are usable; missing controls are shown disabled.

## Live frame capture

Selecting the Wafer.space screensaver starts **continuous background
recapture** on the Pico 2 W host: a PIO state machine clocks the chip, a
second samples all 8 pins into DMA, and a CPU-side seek aligns each capture
to the VSync edge (the whole fall-to-capture arm runs with interrupts
masked, so frames do not jitter horizontally). The default capture clock
is 4 MHz (~6 fps with the 8 MHz alignment seek); the serial `k<N>`
command raises it (the calibration ramp ran clean at 4/8/12.5/25.175 MHz
on this wiring). The page polls at 250 ms and the screensaver animates —
no button needed.
Control endpoints (`select`, `clock`, `step`, `reset`, `uio`) abort-and-
restart any running capture instead of returning 409; the manual
`/api/capture` endpoint is the one place that can still answer 409 while
a capture is mid-flight. On the legacy ESP32 host this section behaves
as before: a manual capture button, a stepping seek with a live scrolling
preview, and 409s while a capture runs.

**Playing Simon (519):** verified on hardware — select it, set the clock
to ~50 kHz (the card's suggested rate) and click **Run**, then turn the
**External clock** toggle ON: the design's outputs immediately animate
(speaker PWM on `uo[4]`, digit multiplexing on `uo[5:6]`, the seven-seg
via `uio`). Press a color button to start a round, watch the LED sequence
on `uo_out` bits 0–3, repeat it with the same buttons, and read your
round number on the seven-segment display. (The internal-clock mode with
the toggle OFF does not oscillate on this chip — leave the toggle ON.)
