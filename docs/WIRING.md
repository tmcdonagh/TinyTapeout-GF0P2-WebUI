# Hardware wiring

There are two host wirings documented here: the **Pico 2 W** (current, fast
VGA capture) and the original **FireBeetle ESP32-E** (legacy). Only one host
may ever drive the card — disconnect the other before powering either.

## Pico 2 W host (current)

USB at the top, physical pin 1 top-left; right-side pins count upward from
the bottom. All connections are dupont jumpers onto a soldered 40-pin
header. The Pico's `3V3(OUT)` (pin 36) powers the TT card's VDD (both VDD
pad types, including the barred one) and the MCP23017 board.

### Pico 2 W ↔ TTGF0P2 direct connections

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
| VDD (both VDD pad types, incl. barred) | 3V3(OUT) | pin 36 | Power |
| GND | any GND (pins 3, 8, 13, 18, 23, 28, 33, 38) | — | Ground |

`uo_out` sits contiguously on GP2–GP9 so a single PIO instruction latches
the whole byte, and GP10 carries the chip clock from a second PIO state
machine during capture (PWM the rest of the time). GP0/GP1 (debug UART)
and GP28 stay unwired as spares.

### Pico 2 W ↔ Waveshare MCP23017 board (X00332)

| MCP23017 board pin | Connect to |
|---|---|
| VCC | Pico 3V3(OUT) — pin 36 |
| GND | Pico GND |
| SDA | GP26 (pin 31, I2C1 SDA) |
| SCL | GP27 (pin 32, I2C1 SCL) |
| PB0–PB7 | `uio[0:7]` on the TT card |
| PA0–PA7 | leave unconnected (future expansion) |
| INTA, INTB | leave unconnected |
| A0/A1/A2 solder jumpers | leave open (answers at 0x27; firmware probes 0x27 then 0x20) |

The Waveshare board has onboard 10 kΩ pull-ups on SDA/SCL and RESET
(RESET is not even exposed), so no external pull-ups are needed. Power it
from **3.3 V**, never 5 V. Unlike the ESP32 wiring, `ui_in` is fully direct
on the Pico, so the expander only carries `uio`.

**Connect both VDD pin types (including one with bar) on the Tiny Tapeout board to 3.3 V.**

## FireBeetle ESP32-E host (legacy)

## ESP32 to TTGF0P2 direct connections

| TTGF0P2 signal | FireBeetle pin | ESP32 GPIO | Direction |
|---|---|---|---|
| `ctrl_ena` | SCK | GPIO18 | ESP32 → TT |
| `ctrl_sel_inc` | MOSI | GPIO23 | ESP32 → TT |
| `ctrl_sel_rst_n` | MISO | GPIO19 | ESP32 → TT |
| `clk` | D5 | GPIO0 | ESP32 → TT |
| `u_rst_n` | A4 | GPIO15 | ESP32 → TT |
| `uo_out[0]` | A3 | GPIO35 | TT → ESP32 |
| `uo_out[1]` | A2 | GPIO34 | TT → ESP32 |
| `uo_out[2]` | A1 | GPIO39 | TT → ESP32 |
| `uo_out[3]` | A0 | GPIO36 | TT → ESP32 |
| `uo_out[4]` | D7 | GPIO13 | TT → ESP32 |
| `uo_out[5]` | D6 | GPIO14 | TT → ESP32 |
| `uo_out[6]` | D3 | GPIO26 | TT → ESP32 |
| `uo_out[7]` | D2 | GPIO25 | TT → ESP32 |
| `ui_in[0]` | D12 | GPIO4 | ESP32 → TT |
| `ui_in[1]` | D11 | GPIO16 | ESP32 → TT |
| `ui_in[4]` | D10 | GPIO17 | ESP32 → TT |
| VDD | 3V3 | — | Power |
| GND | GND | — | Ground |

**Important:** GPIO0 (D5) is an ESP32 strapping pin. Add a **10 kΩ pull-up resistor from D5 to 3.3 V** so the board boots normally.

## MCP23017 IO expander (optional, enables full `ui_in[0:7]` and `uio[0:7]`)

| MCP23017 pin | Connect to |
|---|---|
| VCC | 3.3 V |
| GND | GND |
| SDA | FireBeetle SDA (GPIO21) |
| SCL | FireBeetle SCL (GPIO22) |
| A0 | GND |
| A1 | GND |
| A2 | GND |
| RESET | 3.3 V |
| INTA, INTB | leave unconnected |
| GPIOA[0:7] | `ui_in[0:7]` on TTGF0P2 |
| GPIOB[0:7] | `uio[0:7]` on TTGF0P2 |

If the MCP23017 is not connected, the firmware falls back to the three native `ui_in` bits (0, 1, 4) and `uio` is unavailable.

**Connect both VDD pin types (including one with bar) on the Tiny Tapeout board to 3.3 V.**
