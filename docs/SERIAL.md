# Serial commands

While the Serial monitor is open, you can type:

- `s<N>` — select project N
- `c<N>` — start clock at N Hz (`c0` to stop)
- `t` — single-step clock
- `r` — read `uo_out` byte
- `d` — pin diagnostics
- `i<XX>` — set `ui_in` hex value (e.g. `i01`, `i12`); bit 0 = tile, bit 1 = solid_color
- `u<N>` — set `u_rst_n` (0 = assert/low, 1 = release/high)
- `v` — capture one VGA frame (project 33)
- `b` — dump last captured 320×240 indexed frame as hex
- `a` — auto-cycle projects 0–10 (for mux test)
- `p` — slow pulse `ctrl_sel_inc` for multimeter test

To open the Serial monitor from a PlatformIO build environment:

```bash
pio device monitor --port /dev/ttyUSB0 --baud 115200
```
