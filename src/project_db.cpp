#include "project_db.h"
#include <Arduino.h>
#include <ArduinoJson.h>

namespace tt {

namespace {

// Real TTGF0P2 mux addresses from the chip datasheet
// (tinytapeout.com/chips/ttgf0p2, "All projects" table). The mux address
// space is SPARSE: the 52 designs live at these exact addresses - anything
// else selects an empty slot and the outputs stay low.

const char JSON_33[] PROGMEM =
    "{"
    "\"address\":33,"
    "\"macro\":\"tt_um_waferspace_vga_screensaver\","
    "\"title\":\"Wafer.space Logo VGA Screensaver\","
    "\"author\":\"Uri Shaked\","
    "\"description\":\"VGA 320x240 animated screensaver\","
    "\"clock_hz\":100000,"
    "\"pinout\":{\"ui\":[\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"],"
    "\"uo\":[\"R0\",\"G0\",\"B0\",\"vsync\",\"R1\",\"G1\",\"B1\",\"hsync\"],"
    "\"uio\":[\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"]},"
    "\"uio_direction\":\"0x00\","
    "\"inputs\":[{\"id\":\"tile\",\"type\":\"toggle\",\"bit\":0,\"label\":\"Tile logos\"},{\"id\":\"solid_color\",\"type\":\"toggle\",\"bit\":1,\"label\":\"Solid color\"}],"
    "\"outputs\":[{\"type\":\"vga_canvas\"}]"
    "}";

const char JSON_519[] PROGMEM =
    "{"
    "\"address\":519,"
    "\"macro\":\"tt_um_urish_simon\","
    "\"title\":\"Simon Says memory game\","
    "\"author\":\"Uri Shaked\","
    "\"description\":\"Repeat the sequence of colors and sounds to win\","
    "\"clock_hz\":50000,"
    "\"pinout\":{\"ui\":[\"btn1\",\"btn2\",\"btn3\",\"btn4\",\"seginv\",\"\",\"\",\"clk_sel\"],"
    "\"uo\":[\"led1\",\"led2\",\"led3\",\"led4\",\"speaker\",\"dig1\",\"dig2\",\"clk_internal\"],"
    "\"uio\":[\"seg_a\",\"seg_b\",\"seg_c\",\"seg_d\",\"seg_e\",\"seg_f\",\"seg_g\",\"\"]},"
    "\"uio_direction\":\"0x7F\","
    "\"inputs\":[{\"id\":\"buttons\",\"type\":\"buttons\",\"bits\":[0,1,2,3],\"labels\":[\"Red\",\"Green\",\"Blue\",\"Yellow\"]},{\"id\":\"seginv\",\"type\":\"toggle\",\"bit\":4,\"label\":\"Invert segments\"},{\"id\":\"clk_sel\",\"type\":\"toggle\",\"bit\":7,\"label\":\"External clock\"}],"
    "\"outputs\":[{\"type\":\"leds\",\"bits\":[0,1,2,3],\"labels\":[\"Red\",\"Green\",\"Blue\",\"Yellow\"]},{\"type\":\"seven_seg\",\"segments\":{\"a\":\"uio.0\",\"b\":\"uio.1\",\"c\":\"uio.2\",\"d\":\"uio.3\",\"e\":\"uio.4\",\"f\":\"uio.5\",\"g\":\"uio.6\"},\"digits\":[{\"select\":\"uo.5\"},{\"select\":\"uo.6\"}]}]"
    "}";

const char JSON_384[] PROGMEM =
    "{"
    "\"address\":384,"
    "\"macro\":\"tt_um_kercrafter_leds_racer\","
    "\"title\":\"LEDs Racer\","
    "\"author\":\"KerCrafter\","
    "\"description\":\"Funny electronic game with WS2812B LEDs\","
    "\"clock_hz\":50000000,"
    "\"pinout\":{\"ui\":[\"BLUE_BTN\",\"RED_BTN\",\"GREEN_BTN\",\"YELLOW_BTN\",\"\",\"\",\"\",\"\"],"
    "\"uo\":[\"LEDS_LINE\",\"TP_SCREEN_0\",\"TP_SCREEN_1\",\"TP_BLUE_READY\",\"TP_RED_READY\",\"TP_GREEN_READY\",\"TP_YELLOW_READY\",\"TP_UPDATE_FRAME\"],"
    "\"uio\":[\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"]},"
    "\"uio_direction\":\"0x00\","
    "\"inputs\":[{\"id\":\"buttons\",\"type\":\"buttons\",\"bits\":[0,1,2,3],\"labels\":[\"Blue\",\"Red\",\"Green\",\"Yellow\"]}],"
    "\"outputs\":[{\"type\":\"ws2812_strip\",\"data\":\"uo.0\",\"led_count\":64}]"
    "}";

// All 52 datasheet entries, in address order. Curated entries carry their
// detailed JSON above; the rest get generic cards until more are curated.
struct Slot {
    uint16_t address;
    const char* title;
    const char* macro;
};

constexpr Slot SLOTS[] = {
    {0,   "Chip ROM", "tt_um_chip_rom"},
    {1,   "Tiny Tapeout Factory Test", "tt_um_factory_test"},
    {32,  "Zedulo TestChip1", "tt_um_zedtc1_top"},
    {33,  "Wafer.space Logo VGA Screensaver", "tt_um_waferspace_vga_screensaver"},
    {38,  "Wildcat RISC-V", "tt_um_schoeberl_wildcat"},
    {39,  "TinyQV Risc-V SoC", "tt_um_MichaelBell_tinyQV"},
    {101, "SCuM-BLE-RX", "tt_um_BLE_RX"},
    {102, "Silly-Faust", "tt_um_gf0p2_faust_top"},
    {103, "Simon's Caterpillar", "tt_um_htfab_caterpillar"},
    {128, "VGA clock", "tt_um_vga_clock"},
    {129, "2x2 MAC Systolic array with DFT", "tt_um_essen"},
    {130, "USB CDC (Serial) Device", "tt_um_urish_usb_cdc"},
    {134, "VGA Drop (audio/visual demo)", "tt_um_rejunity_vga_test01"},
    {135, "raybox-zero TTGF0p2 edition", "tt_um_algofoogle_raybox_zero"},
    {194, "2048 sliding tile puzzle game (VGA)", "tt_um_2048_vga_game"},
    {196, "Quickscope", "tt_um_thezoq2_quickscope"},
    {197, "LISA 8-Bit Microcontroller", "tt_um_lisa"},
    {198, "easy PAL", "tt_um_MATTHIAS_M_PAL_TOP_WRAPPER"},
    {199, "VGA Nyan Cat", "tt_um_a1k0n_nyancat"},
    {288, "Notre Dame - Lockpick Game TT Example", "tt_um_mmorri22_lockpick_game"},
    {295, "KianV uLinux SoC", "tt_um_kianV_rv32ima_uLinux_SoC"},
    {358, "Notre Dame - CSE 30342 - Advanced FSM Example", "tt_um_mmorri22_cse_30342"},
    {359, "Simple RISC-V", "tt_um_simple_riscv"},
    {384, "LEDs Racer", "tt_um_kercrafter_leds_racer"},
    {385, "Frequency Counter SSD1306 OLED", "tt_um_frequency_counter"},
    {391, "megabytebeat", "tt_um_proppy_megabytebeat"},
    {451, "Asicle v2", "tt_um_htfab_asicle2"},
    {454, "CAN Controller for Rocket", "tt_um_noritsuna_CAN_CTRL"},
    {455, "Zilog Z80", "tt_um_rejunity_z80"},
    {481, "ROTFPGA v2", "tt_um_htfab_rotfpga2"},
    {483, "7-Segment Digital Desk Clock", "tt_um_digital_clock_example"},
    {485, "MarcoPolo", "tt_um_javibajocero_top"},
    {487, "Linear Timecode (LTC) generator", "tt_um_flummer_ltc"},
    {513, "Programmable Sound Generator SN76489", "tt_um_rejunity_sn76489"},
    {515, "VGA Tiny Logo", "tt_um_rejunity_vga_logo"},
    {517, "Ring Oscillator (5 inverter)", "tt_um_dlmiles_ringosc_5inv"},
    {519, "Simon Says memory game", "tt_um_urish_simon"},
    {545, "Flame demo", "tt_um_kbeckmann_flame"},
    {547, "WokwiPWM", "tt_um_wokwi_445338187869298689"},
    {549, "Dog Battle Game", "tt_um_SophusAndreassen_dogbattle"},
    {551, "PVTMonitorSuite", "tt_um_zacky1972_PVTMonitorSuite"},
    {577, "PILIPINAS_IC", "tt_um_wokwi_392873974467527681"},
    {579, "PRISM 8 with TinySnake", "tt_um_wokwi_442081253563458561"},
    {581, "Register bank via SPI and I2C", "tt_um_calonso88_spi_i2c_reg_bank"},
    {583, "Cell mux", "tt_um_htfab_cells"},
    {609, "Super-Simple-SPI-CPU", "tt_um_spi_cpu_top"},
    {610, "Example of Bad Synchronizer", "tt_um_dlmiles_bad_synchronizer"},
    {611, "Video mode tester", "tt_um_htfab_vga_tester"},
    {612, "One Bit PUF", "tt_um_multi_bit_puf_wrapper"},
    {613, "DDR throughput test", "tt_um_dlmiles_ddr_throughput_test"},
    {614, "Ring osc on VGA", "tt_um_algofoogle_vgaringosc"},
    {615, "GF180MCU loopback tile", "tt_um_dlmiles_loopback"},
};
constexpr size_t SLOT_COUNT = sizeof(SLOTS) / sizeof(SLOTS[0]);

// The three curated descriptors, keyed by their true datasheet addresses.
constexpr ProjectDescriptor CURATED[] = {
    {33,  "Wafer.space Logo VGA Screensaver", "tt_um_waferspace_vga_screensaver", 0x00, JSON_33},
    {519, "Simon Says memory game", "tt_um_urish_simon", 0x7F, JSON_519},
    {384, "LEDs Racer", "tt_um_kercrafter_leds_racer", 0x00, JSON_384},
};
constexpr size_t CURATED_COUNT = sizeof(CURATED) / sizeof(CURATED[0]);

// Generic descriptor handed out for un-curated slots. One shared instance,
// refilled per lookup - safe in the single-loop firmware.
ProjectDescriptor g_generic = {};

const char* curated_blob_for(uint16_t address) {
    for (size_t i = 0; i < CURATED_COUNT; ++i) {
        if (CURATED[i].address == address) return CURATED[i].json_blob;
    }
    return nullptr;
}

}  // namespace

bool project_db_init() {
    return true;
}

size_t project_count() {
    return SLOT_COUNT;
}

const ProjectDescriptor* project_by_address(uint16_t address) {
    // ROM address 0 also selects; everything else must be a datasheet slot.
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (SLOTS[i].address != address) continue;
        for (size_t c = 0; c < CURATED_COUNT; ++c) {
            if (CURATED[c].address == address) return &CURATED[c];
        }
        g_generic.address = address;
        g_generic.title = SLOTS[i].title;
        g_generic.macro = SLOTS[i].macro;
        g_generic.uio_direction = 0x00;
        g_generic.json_blob = nullptr;
        return &g_generic;
    }
    return nullptr;
}

const ProjectDescriptor* project_by_index(size_t index) {
    if (index >= SLOT_COUNT) return nullptr;
    return project_by_address(SLOTS[index].address);
}

uint16_t project_address(size_t index) {
    if (index >= SLOT_COUNT) return 0xFFFF;
    return SLOTS[index].address;
}

const char* project_title(size_t index) {
    const ProjectDescriptor* desc = project_by_index(index);
    return desc != nullptr ? desc->title : "";
}

const char* project_macro(size_t index) {
    const ProjectDescriptor* desc = project_by_index(index);
    return (desc != nullptr) ? desc->macro : "";
}

String generic_project_json(uint16_t address) {
    JsonDocument doc;
    doc["address"] = address;
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        if (SLOTS[i].address == address) {
            doc["title"] = SLOTS[i].title;
            doc["macro"] = SLOTS[i].macro;
            break;
        }
    }
    doc["author"] = "";
    doc["description"] = "Generic controls - see the project's datasheet page";
    doc["clock_hz"] = 10000;

    JsonArray ui = doc["pinout"]["ui"].to<JsonArray>();
    JsonArray uo = doc["pinout"]["uo"].to<JsonArray>();
    JsonArray uio = doc["pinout"]["uio"].to<JsonArray>();
    for (int i = 0; i < 8; ++i) {
        ui.add("ui." + String(i));
        uo.add("uo." + String(i));
        uio.add("uio." + String(i));
    }

    doc["uio_direction"] = "0x00";

    JsonArray inputs = doc["inputs"].to<JsonArray>();
    for (int i = 0; i < 8; ++i) {
        JsonObject toggle = inputs.add<JsonObject>();
        toggle["id"] = "ui_bit_" + String(i);
        toggle["type"] = "toggle";
        toggle["bit"] = i;
        toggle["label"] = "ui." + String(i);
    }

    JsonArray outputs = doc["outputs"].to<JsonArray>();
    JsonObject leds = outputs.add<JsonObject>();
    leds["type"] = "leds";
    JsonArray bits = leds["bits"].to<JsonArray>();
    JsonArray labels = leds["labels"].to<JsonArray>();
    for (int i = 0; i < 8; ++i) {
        bits.add(i);
        labels.add("uo." + String(i));
    }

    String out;
    serializeJson(doc, out);
    return out;
}

}  // namespace tt
