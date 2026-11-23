#pragma once

#include <stdint.h>
#include <stddef.h>

namespace tt {

// Project descriptor exposed by the firmware.
// The JSON blob is a pointer to a PROGMEM string containing the full
// descriptor object for this one project.
struct ProjectDescriptor {
    uint16_t address;
    const char* title;
    const char* macro;
    uint8_t uio_direction;
    const char* json_blob;
};

// Initialize the project database.
bool project_db_init();

// Number of projects in the database.
size_t project_count();

// Look up a project by its mux address. Returns nullptr if not found.
const ProjectDescriptor* project_by_address(uint16_t address);

// Look up a project by its index in the database (0 .. project_count()-1).
const ProjectDescriptor* project_by_index(size_t index);

// Convenience accessors.
uint16_t project_address(size_t index);
const char* project_title(size_t index);
const char* project_macro(size_t index);

}  // namespace tt
