#include "wifi_config.h"
#include <WiFi.h>

namespace tt {

namespace {
    constexpr char AP_SSID[] = "TT-GF0P2-Showcase";
    constexpr char AP_PSK[] = "tinytapeout";
    constexpr int AP_CHANNEL = 6;
    constexpr int AP_MAX_CLIENTS = 4;
}

void wifi_init() {
    WiFi.mode(WIFI_AP);
    bool ok = WiFi.softAP(AP_SSID, AP_PSK, AP_CHANNEL, false, AP_MAX_CLIENTS);

    if (ok) {
        Serial.print("WiFi AP started: ");
        Serial.println(AP_SSID);
        Serial.print("IP address: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("ERROR: WiFi AP start failed");
    }
}

String wifi_ip() {
    return WiFi.softAPIP().toString();
}

bool wifi_connected() {
    return WiFi.softAPgetStationNum() > 0;
}

}  // namespace tt
