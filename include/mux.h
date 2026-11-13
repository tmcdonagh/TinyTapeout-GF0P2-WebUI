#pragma once

#include <stdint.h>

namespace tt {

// Select a project on the TTGF0P2 mux.
// project must be 0..51.
void select_project(uint8_t project);

// Return the currently selected project number.
uint8_t current_project();

}  // namespace tt
