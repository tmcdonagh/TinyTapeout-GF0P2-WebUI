#include "pins.h"
#include "mux.h"
#include <hardware/gpio.h>

namespace tt {

// read_uo_out() depends on uo_out[0..7] sitting on GP2..GP9 contiguously.
static_assert(PIN_UO_OUT[0] == 2 && PIN_UO_OUT[1] == 3 && PIN_UO_OUT[2] == 4 &&
                  PIN_UO_OUT[3] == 5 && PIN_UO_OUT[4] == 6 && PIN_UO_OUT[5] == 7 &&
                  PIN_UO_OUT[6] == 8 && PIN_UO_OUT[7] == 9,
              "uo_out register mapping stale");

namespace {

uint8_t g_ui_in_value = 0;

void set_output(int pin) {
    pinMode(pin, OUTPUT);
    // Strong drive so edges stay clean over dupont jumpers to the card.
    gpio_set_drive_strength(static_cast<uint>(pin), GPIO_DRIVE_STRENGTH_12MA);
}

void set_clock_output() {
    // The chip clock radiates hard over its jumper (square-wave harmonics
    // couple into the nearby USB link at MHz rates and destabilize it):
    // prefer the slowest legal slew and the weakest sufficient drive.
    pinMode(PIN_CLK, OUTPUT);
    gpio_set_drive_strength(static_cast<uint>(PIN_CLK), GPIO_DRIVE_STRENGTH_4MA);
    gpio_set_slew_rate(static_cast<uint>(PIN_CLK), GPIO_SLEW_RATE_SLOW);
}

}  // namespace

void init_pins() {
    // Control outputs: start inactive, maximum drive strength.
    set_output(PIN_CTRL_ENA);
    digitalWrite(PIN_CTRL_ENA, LOW);

    set_output(PIN_CTRL_SEL_INC);
    digitalWrite(PIN_CTRL_SEL_INC, LOW);

    set_output(PIN_CTRL_SEL_RST_N);
    digitalWrite(PIN_CTRL_SEL_RST_N, HIGH);  // active-low, so high = inactive

    set_clock_output();
    digitalWrite(PIN_CLK, LOW);

    set_output(PIN_U_RST_N);
    digitalWrite(PIN_U_RST_N, LOW);  // hold project in reset until released

    // uo_out inputs: no pull-up/pull-down, the TT card drives these.
    for (int i = 0; i < 8; ++i) {
        pinMode(PIN_UO_OUT[i], INPUT);
    }

    // ui_in outputs: default LOW (tile=0, solid_color=0, gamepad_latch=0).
    for (int i = 0; i < 8; ++i) {
        set_output(PIN_UI_IN[i]);
        digitalWrite(PIN_UI_IN[i], LOW);
    }
    g_ui_in_value = 0;
}

uint8_t read_uo_out() {
    // uo_out[0..7] = GP2..GP9, contiguous: one register snapshot.
    return static_cast<uint8_t>(gpio_get_all() >> PIN_UO_OUT_BASE);
}

void set_ctrl_ena(bool high) {
    digitalWrite(PIN_CTRL_ENA, high ? HIGH : LOW);
}

void set_ctrl_sel_inc(bool high) {
    digitalWrite(PIN_CTRL_SEL_INC, high ? HIGH : LOW);
}

void set_ctrl_sel_rst_n(bool high) {
    // `high` here means the *signal* is high. The wire is active-low, so
    // `true` de-asserts reset and `false` asserts it.
    digitalWrite(PIN_CTRL_SEL_RST_N, high ? HIGH : LOW);
}

void set_u_rst_n(bool high) {
    digitalWrite(PIN_U_RST_N, high ? HIGH : LOW);
}

void set_ui_in(uint8_t value) {
    // User-owned ui_in bits are 0-3 and 7; bits 4-6 belong to the project-33
    // virtual gamepad emulator (gamepad.cpp): user writes are masked AND
    // those pins are never touched here, so a toggle cannot clobber an
    // in-flight gamepad burst.
    value &= 0x8F;
    g_ui_in_value = value;
    for (int i = 0; i < 8; ++i) {
        if (i >= 4 && i <= 6) continue;  // gamepad emulator's pins
        digitalWrite(PIN_UI_IN[i], (value & (1u << i)) ? HIGH : LOW);
    }
}

uint8_t ui_in_value() {
    // The gamepad pins idle low between bursts (the design holds the
    // latched state internally), so the pin state IS the user bits.
    return g_ui_in_value;
}

void print_pin_diagnostics() {
    Serial.println("--- pin diagnostics ---");
    Serial.print("ctrl_ena=");   Serial.print(digitalRead(PIN_CTRL_ENA));
    Serial.print(" sel_inc=");   Serial.print(digitalRead(PIN_CTRL_SEL_INC));
    Serial.print(" sel_rst_n="); Serial.print(digitalRead(PIN_CTRL_SEL_RST_N));
    Serial.print(" clk=");       Serial.print(digitalRead(PIN_CLK));
    Serial.print(" u_rst_n=");   Serial.print(digitalRead(PIN_U_RST_N));
    Serial.print(" ui_in=0x");   Serial.print(g_ui_in_value, HEX);

    uint8_t uo = read_uo_out();
    Serial.print(" uo_out=0b");
    Serial.print(uo, BIN);
    Serial.print(" (0x");
    Serial.print(uo, HEX);
    Serial.println(")");
    Serial.println("----------------------");
}

}  // namespace tt
