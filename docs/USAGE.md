# Using the web UI

1. On your phone or laptop, connect to the WiFi network:
   - **SSID:** `TT-GF0P2-Showcase`
   - **Password:** `tinytapeout`
2. The phone should show a "Sign in to network" / captive portal notification within a few seconds. Tap it — the web UI opens automatically.
   - If the portal notification does not appear, open a browser and go to `http://192.168.4.1/`.
   - If the page still does not load, make sure mobile data is off for this test; the captive portal normally routes the request over WiFi, but some phones need a moment to switch.
3. Choose a project from the **Project** dropdown. Phase 1 showcases are:
   - **Wafer.space Logo VGA Screensaver** (33)
   - **Simon Says memory game** (519)
   - **LEDs Racer** (384)
4. Use the clock presets or click the pencil to enter a custom frequency.
5. Click **Run** to start the clock, **Stop** to halt it, or **Step** to advance one cycle.
6. The page renders project-specific input and output widgets automatically.
   - If the optional MCP23017 IO expander is connected, all `ui_in[0:7]` and `uio[0:7]` controls are active.
   - Without the expander, only the native `ui_in` bits (0, 1, 4) are usable; missing controls are shown disabled.

## Live frame capture

The page can also show the selected project's video output as an image. The frame is captured by a background task, and the page streams rows while it runs, so the image fills in top-to-bottom on a live canvas. The initial sync is a stepping seek that samples every second line into a rolling preview window (`preview_row`), so the canvas shows the wallpaper scrolling while it aligns, then snaps to the aligned fill. While a capture is in progress, the `clock`, `step`, `select`, `reset`, and `frame.bmp` endpoints return HTTP 409. The `frame_raw` endpoint serves the partial rows for the live view.
