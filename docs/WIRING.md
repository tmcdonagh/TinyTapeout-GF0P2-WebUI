# Hardware wiring

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
