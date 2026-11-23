# Tiny Tapeout GF0P2 Web Showcase Firmware

![Demo animation](docs/demo.gif)

## What

Raspberry Pi Pico 2 W based web interface to the Tiny Tapeout TTGF0P2
business card chip, with hardware-driven VGA frame capture.

## Why

I volunteered to be a TA at a Tiny Tapeout workshop at Hackaday Supercon 2026.
As part of that, all attendees were given a Tiny Tapeout chip on a business
card sized PCB. Typically, Tiny Tapeout chips are mounted on devkits to be used
with PMODs. Since I don't have any PMODs and even if I did I would have to solder
a connector to this board, I wondered how else I could test it.

Since all inputs and outputs are digital, it seemed like a natural next step to
read and write to those pins with a microcontroller. In fact, the guide instructs
you to use a microcontroller to set the project, so why stop there? I enjoy the
flexibility and accessibility that the Wokwi simulator brings, so my goal is to
replicate that in functionality as close as possible, with some extras.

**Tiny Tapeout provided documentation:** https://tinytapeout.com/guides/get-started-had-gf0p2/

## Features

- Select any of the 52 real TTGF0P2 designs from a web page — the
  datasheet's sparse mux addresses (0, 1, 32, ... 615), with curated
  widgets for the showcase projects and generic controls for the rest.
- Adjustable project clock: run, stop, set frequency (10 Hz–25 MHz), single-step.
- Live display of `uo_out[0..7]` as LEDs; all 8 `ui_in` bits direct GPIO,
  `uio[0:7]` via the MCP23017 IO expander.
- **Continuously animating VGA frame capture for project 33
  (Wafer.space screensaver)** — a PIO state machine clocks the chip while
  a second samples all 8 pins into DMA, with each capture CPU-aligned to
  the VSync edge; ~6 fps at the 4 MHz default (the serial `k` command
  raises it — clean up to 25.175 MHz on this wiring), instead of the
  ESP32 build's ~5 seconds per frame.
- No app required. Captive portal opens the web UI automatically when you
  connect to the AP.

## AI Usage

This project uses AI heavily. I've been experimenting with the open weight models through
API access recently like Kimi K2.7 Code and GLM-5.3-Flash. I've found great success in
giving the AI access to the USB port of the ESP32 so it can flash and test over serial.

## Documentation

| Topic | Document |
|---|---|
| Hardware wiring and pinout | [`docs/WIRING.md`](docs/WIRING.md) |
| Flash a pre-built release binary | [`docs/FLASHING.md`](docs/FLASHING.md) |
| Build and flash from source | [`docs/DEVELOPER.md`](docs/DEVELOPER.md) |
| Using the web UI | [`docs/USAGE.md`](docs/USAGE.md) |
| Serial debug commands | [`docs/SERIAL.md`](docs/SERIAL.md) |
