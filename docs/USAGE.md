# Using the web UI

1. On your phone or laptop, connect to the WiFi network:
   - **SSID:** `TT-GF0P2-Showcase`
   - **Password:** `tinytapeout`
2. The phone should show a "Sign in to network" / captive portal notification within a few seconds. Tap it — the web UI opens automatically.
   - If the portal notification does not appear, open a browser and go to `http://192.168.4.1/`.
   - If the page still does not load, make sure mobile data is off for this test; the captive portal normally routes the request over WiFi, but some phones need a moment to switch.
3. Enter a project number (0–51) and click **Select**.
4. Use the clock presets or click the pencil to enter a custom frequency.
5. Click **Run** to start the clock, **Stop** to halt it, or **Step** to advance one cycle.
6. Watch the `uo_out` LEDs update as the TT chip runs.

## Live frame capture

The page can also show the selected project's video output as an image. The frame is captured by a background task, and the page streams rows while it runs, so the image fills in top-to-bottom on a live canvas. The initial sync is a stepping seek that samples every second line into a rolling preview window (`preview_row`), so the canvas shows the wallpaper scrolling while it aligns, then snaps to the aligned fill. While a capture is in progress, the `clock`, `step`, `select`, `reset`, and `frame.bmp` endpoints return HTTP 409. The `frame_raw` endpoint serves the partial rows for the live view.
