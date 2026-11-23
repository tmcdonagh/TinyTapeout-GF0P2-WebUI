# Tiny Tapeout GF0P2 Web Showcase

WiFi-connected controller firmware for the Tiny Tapeout TTGF0P2 business-card
chip. Two host firmwares share one web UI (`data/index.html`): the current
Raspberry Pi Pico 2 W port (`pico/`, branch `pico2W`) with PIO-driven VGA
capture, and the legacy DFRobot FireBeetle ESP32-E build (repo root).

## Archive

Documents live in `~/dev/ai-dev-archive/projects/tinytapeout-gf0p2-web-showcase/`,
not in this repo.

- Specs: `specs/<feature-slug>/spec.md` — read the relevant spec before
  changing design.
- Handoffs: `handoffs/` — **read the newest handoff at session start, before
  doing anything else.**
- Decisions: `decisions/` — check before revisiting a settled question.
- End every work session with `/handoff`.

## Build & test

- Build ESP32: `~/.venv-platformio/bin/pio run` (repo root)
- Build Pico 2 W: `~/.venv-platformio/bin/pio run -d pico`
- Flash Pico: see the newest handoff — COM9 at 1200 bps touch + UF2 copy;
  **never usbipd-attach this board to WSL** (it kills the device even in
  BOOTSEL mode).
- Serial console (Windows side): `C:\Users\tmcdo\wserial.ps1 -Command "<cmd>"`.
  Serial commands are documented in `docs/SERIAL.md` (Pico additions pending).

## Conventions

- Tom commits and pushes; agents never do. Stage work and suggest commit
  messages.
- The Pico port's SDD ledger snapshot lives at
  `docs/superpowers/sdd/2026-11-21-pico2w-capture-port/progress.md`; copy it
  to `.superpowers/sdd/<plan>/progress.md` before resuming that plan.
- Shared web UI (`data/index.html`) must keep working on both hosts — new
  state fields must degrade gracefully when absent (ESP32 build).
- Inside interrupt-masked windows use `busy_wait_*`, never sleep calls
  (`decisions/2026-11-22-masked-window-busy-wait.md`).
