#pragma once

#include <Arduino.h>

namespace tt {

// Connect to the configured WiFi network in station mode.
// Blocks until connected or until a timeout occurs.
void wifi_init();

// Return the assigned IP address as a string, or "0.0.0.0" if not connected.
String wifi_ip();

// True if the station is connected to an AP.
bool wifi_connected();

}  // namespace tt
