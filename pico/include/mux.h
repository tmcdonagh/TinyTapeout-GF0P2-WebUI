#pragma once

#include <stdint.h>

namespace tt {

// Select a project on the TTGF0P2 mux by its mux address.
void select_project(uint16_t project);

// Return the currently selected project mux address.
uint16_t current_project();

}  // namespace tt
