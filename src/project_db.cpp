#include "project_db.h"

namespace tt {

namespace {

const char JSON_33[] =
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
    "\"inputs\":[],"
    "\"outputs\":[{\"type\":\"vga_canvas\"}]"
    "}";

const char JSON_519[] =
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

const char JSON_384[] =
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

constexpr ProjectDescriptor DESCRIPTORS[] = {
    {33, "Wafer.space Logo VGA Screensaver", "tt_um_waferspace_vga_screensaver", 0x00, JSON_33},
    {519, "Simon Says memory game", "tt_um_urish_simon", 0x7F, JSON_519},
    {384, "LEDs Racer", "tt_um_kercrafter_leds_racer", 0x00, JSON_384},
};

constexpr size_t DESCRIPTOR_COUNT = sizeof(DESCRIPTORS) / sizeof(DESCRIPTORS[0]);

}  // namespace

bool project_db_init() {
    return true;
}

size_t project_count() {
    return DESCRIPTOR_COUNT;
}

const ProjectDescriptor* project_by_address(uint16_t address) {
    for (size_t i = 0; i < DESCRIPTOR_COUNT; ++i) {
        if (DESCRIPTORS[i].address == address) {
            return &DESCRIPTORS[i];
        }
    }
    return nullptr;
}

const ProjectDescriptor* project_by_index(size_t index) {
    if (index >= DESCRIPTOR_COUNT) return nullptr;
    return &DESCRIPTORS[index];
}

uint16_t project_address(size_t index) {
    if (index >= DESCRIPTOR_COUNT) return 0xFFFF;
    return DESCRIPTORS[index].address;
}

const char* project_title(size_t index) {
    if (index >= DESCRIPTOR_COUNT) return "";
    return DESCRIPTORS[index].title;
}

const char* project_macro(size_t index) {
    if (index >= DESCRIPTOR_COUNT) return "";
    return DESCRIPTORS[index].macro;
}

}  // namespace tt
