#pragma once

#include <Arduino.h>

namespace tt {

// Boot WiFi policy: join the saved home network (serial 'W' command,
// stored in EEPROM) and fall back to the demo SoftAP when there is no
// saved network or the router is unreachable.
void wifi_init();

// Current AP IP as a string ("192.168.4.1"), or the station IP when
// associated to the home network.
String wifi_ip();

// True when at least one station is associated (AP mode).
bool wifi_connected();

// Save home-network credentials and reboot to apply.
void wifi_set_station(const String& ssid, const String& pass);

// Forget the saved network (next boot uses the demo AP).
void wifi_clear_station();

// Network status for the 'N' serial command.
void wifi_print_status();

// Call from the main loop: rejoin the home network after a drop (the
// router's lease renewals / steering kicks are outside our control).
void wifi_service();

}  // namespace tt
