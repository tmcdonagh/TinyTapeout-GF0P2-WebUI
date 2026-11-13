#pragma once

#include <Arduino.h>

namespace tt {

// Start the captive-portal DNS server on UDP port 53.
// Must be called after WiFi AP mode is initialized.
void dns_server_init();

// Process pending DNS packets. Call frequently from loop().
void dns_server_loop();

}  // namespace tt
