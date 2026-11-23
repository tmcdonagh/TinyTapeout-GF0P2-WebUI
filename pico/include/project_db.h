#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

namespace tt {

// The TTGF0P2 mux address space is SPARSE: the 52 designs live at the
// datasheet addresses (0, 1, 32, 33, 38, ... 615). Three curated
// descriptors carry metadata; every other datasheet slot gets a generic
// descriptor with its real title from the chip's project table.
constexpr uint16_t TT_SLOT_COUNT = 52;

// Project descriptor exposed by the firmware.
struct ProjectDescriptor {
    uint16_t address;
    const char* title;
    const char* macro;
    uint8_t uio_direction;
    const char* json_blob;  // nullptr for un-curated slots
};

// Initialize the project database.
bool project_db_init();

// Number of selectable projects (always TT_SLOT_COUNT).
size_t project_count();

// Look up a project by its mux address. Returns nullptr for addresses not
// in the datasheet table (those select empty slots: outputs stay low).
const ProjectDescriptor* project_by_address(uint16_t address);

// Look up by slot index (0 .. project_count()-1).
const ProjectDescriptor* project_by_index(size_t index);

// Convenience accessors.
uint16_t project_address(size_t index);
const char* project_title(size_t index);
const char* project_macro(size_t index);

// Generic descriptor JSON for un-curated slots, built with ArduinoJson.
// Consumed by the web server's /api/project handler.
String generic_project_json(uint16_t address);

}  // namespace tt
