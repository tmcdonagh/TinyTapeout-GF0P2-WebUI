#include "pins.h"
#include "mux.h"
#include "vga_capture.h"
#include "io_expander.h"
#include <driver/gpio.h>
#include <soc/gpio_struct.h>

namespace tt {

static_assert(PIN_UO_OUT[0] == 35 && PIN_UO_OUT[1] == 34 && PIN_UO_OUT[2] == 39 && PIN_UO_OUT[3] == 36 && PIN_UO_OUT[4] == 13 && PIN_UO_OUT[5] == 14 && PIN_UO_OUT[6] == 26 && PIN_UO_OUT[7] == 25, "uo_out register mapping stale");

namespace {
    void set_output(int pin) {
        pinMode(pin, OUTPUT);
        gpio_set_drive_capability(static_cast<gpio_num_t>(pin), GPIO_DRIVE_CAP_3);
    }
}

void init_pins() {
    // Control outputs: start inactive, with maximum drive strength so the
    // signals reach the TT card cleanly even if the breakout loads them.
    set_output(PIN_CTRL_ENA);
    digitalWrite(PIN_CTRL_ENA, LOW);

    set_output(PIN_CTRL_SEL_INC);
    digitalWrite(PIN_CTRL_SEL_INC, LOW);

    set_output(PIN_CTRL_SEL_RST_N);
    digitalWrite(PIN_CTRL_SEL_RST_N, HIGH);  // active-low, so high = inactive

    set_output(PIN_CLK);
    digitalWrite(PIN_CLK, LOW);

    set_output(PIN_U_RST_N);
    digitalWrite(PIN_U_RST_N, LOW);  // hold project in reset until released

    // uo_out inputs: no pull-up/pull-down, the TT card drives these.
    for (int i = 0; i < 8; ++i) {
        pinMode(PIN_UO_OUT[i], INPUT);
    }

    // One-time boot cross-check: register-derived value vs digitalRead loop.
    // Diagnostic only; never return early on mismatch.
    {
        uint8_t reg = read_uo_out();
        uint8_t dig = 0;
        for (int i = 0; i < 8; ++i) {
            if (digitalRead(PIN_UO_OUT[i]) == HIGH) {
                dig |= (1u << i);
            }
        }
        if (reg == dig) {
            Serial.println("uo_out register read cross-check: OK");
        } else {
            uint8_t diff = reg ^ dig;
            Serial.print("uo_out register read cross-check: MISMATCH, bits:");
            for (int i = 0; i < 8; ++i) {
                if (diff & (1u << i)) {
                    Serial.print(' ');
                    Serial.print(i);
                }
            }
            Serial.print(" (reg=0x");
            Serial.print(reg, HEX);
            Serial.print(" digitalRead=0x");
            Serial.print(dig, HEX);
            Serial.println(")");
        }
    }

    // ui_in outputs: default LOW (tile=0, solid_color=0, gamepad_latch=0).
    for (int i = 0; i < 8; ++i) {
        if (PIN_UI_IN[i] >= 0) {
            set_output(PIN_UI_IN[i]);
            digitalWrite(PIN_UI_IN[i], LOW);
        }
    }
}

namespace {
    uint8_t g_ui_in_value = 0;
}

uint8_t read_uo_out() {
    uint32_t lo = GPIO.in;          // GPIOs 0-31 (direct field in this IDF)
    uint32_t hi = GPIO.in1.val;     // GPIOs 32-39
    return ((lo >> 13) & 1) << 4 | ((lo >> 14) & 1) << 5 |
           ((lo >> 26) & 1) << 6 | ((lo >> 25) & 1) << 7 |
           ((hi >> 3) & 1) << 0 | ((hi >> 2) & 1) << 1 |
           ((hi >> 7) & 1) << 2 | ((hi >> 4) & 1) << 3;
}

void set_ctrl_ena(bool high) {
    digitalWrite(PIN_CTRL_ENA, high ? HIGH : LOW);
}

void set_ctrl_sel_inc(bool high) {
    digitalWrite(PIN_CTRL_SEL_INC, high ? HIGH : LOW);
}

void set_ctrl_sel_rst_n(bool high) {
    // `high` here means the *signal* is high. Since the wire is active-low,
    // passing `true` de-asserts reset and `false` asserts it.
    digitalWrite(PIN_CTRL_SEL_RST_N, high ? HIGH : LOW);
}

void set_u_rst_n(bool high) {
    digitalWrite(PIN_U_RST_N, high ? HIGH : LOW);
}

void set_ui_in(uint8_t value) {
    g_ui_in_value = value;
    for (int i = 0; i < 8; ++i) {
        if (PIN_UI_IN[i] >= 0) {
            digitalWrite(PIN_UI_IN[i], (value & (1u << i)) ? HIGH : LOW);
        }
    }
    // MCP23017 (if present) drives all ui_in bits, including the ones
    // also wired natively.
    ui_in_write(value);
    if (current_project() == 33) {
        vga_cache_request();
    }
}

uint8_t ui_in_value() {
    return g_ui_in_value;
}

namespace {
    uint8_t g_uio_value = 0;
    uint8_t g_uio_dir = 0;
}

void set_uio_direction(uint8_t output_mask) {
    g_uio_dir = output_mask;
    uio_set_direction(output_mask);
}

uint8_t uio_direction() {
    return g_uio_dir;
}

void set_uio_value(uint8_t value) {
    g_uio_value = value;
    uio_write(value);
}

uint8_t uio_value() {
    g_uio_value = uio_read();
    return g_uio_value;
}

void print_pin_diagnostics() {
    Serial.println("--- pin diagnostics ---");
    Serial.print("ctrl_ena=");      Serial.print(digitalRead(PIN_CTRL_ENA));
    Serial.print(" sel_inc=");      Serial.print(digitalRead(PIN_CTRL_SEL_INC));
    Serial.print(" sel_rst_n=");    Serial.print(digitalRead(PIN_CTRL_SEL_RST_N));
    Serial.print(" clk=");          Serial.print(digitalRead(PIN_CLK));
    Serial.print(" u_rst_n=");      Serial.print(digitalRead(PIN_U_RST_N));
    Serial.print(" ui_in=0x");      Serial.print(g_ui_in_value, HEX);
    Serial.print(" uio_dir=0x");    Serial.print(g_uio_dir, HEX);
    Serial.print(" uio_val=0x");    Serial.println(g_uio_value, HEX);

    uint8_t uo = read_uo_out();
    Serial.print("uo_out=0b");      Serial.print(uo, BIN);
    Serial.print(" (0x");           Serial.print(uo, HEX);
    Serial.println(")");
    Serial.println("----------------------");
}

}  // namespace tt
