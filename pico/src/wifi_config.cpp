#include "wifi_config.h"
#include <WiFi.h>
#include <hardware/watchdog.h>
#include <EEPROM.h>

namespace tt {

namespace {
    constexpr char AP_SSID[] = "TT-GF0P2-Showcase";
    constexpr char AP_PSK[] = "tinytapeout";
    constexpr int AP_CHANNEL = 6;
    constexpr int AP_MAX_CLIENTS = 4;

    // Cached station credentials + state for the reconnect service.
    String g_sta_ssid;
    String g_sta_pass;
    bool g_sta_mode = false;

    // EEPROM layout: magic, ssid_len, ssid bytes, pass_len, pass bytes.
    constexpr size_t EE_ADDR = 0;
    constexpr uint8_t EE_MAGIC = 0x57;  // 'W'

    bool load_sta_config(String& ssid, String& pass) {
        EEPROM.begin(4096);
        uint8_t magic = EEPROM.read(EE_ADDR);
        if (magic != EE_MAGIC) return false;
        size_t off = EE_ADDR + 1;
        uint8_t ssid_len = EEPROM.read(off++);
        if (ssid_len == 0 || ssid_len > 32) return false;
        char buf[33];
        for (uint8_t i = 0; i < ssid_len; ++i) buf[i] = EEPROM.read(off + i);
        buf[ssid_len] = 0;
        off += ssid_len;
        ssid = buf;
        uint8_t pass_len = EEPROM.read(off++);
        if (pass_len > 64) return false;
        char pbuf[65];
        for (uint8_t i = 0; i < pass_len; ++i) pbuf[i] = EEPROM.read(off + i);
        pbuf[pass_len] = 0;
        pass = pbuf;
        return ssid.length() > 0;
    }

    bool start_ap() {
        WiFi.mode(WIFI_AP);
        // Pin the AP address explicitly: every hardcoded IP, the UI's
        // redirect logic, and the docs assume 192.168.4.1 - do not rely
        // on the core's default happening to match.
        WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                          IPAddress(255, 255, 255, 0));
        bool ok = WiFi.softAP(AP_SSID, AP_PSK, AP_CHANNEL, false, AP_MAX_CLIENTS);
        if (ok) {
            Serial.print("WiFi AP started: ");
            Serial.println(AP_SSID);
            Serial.print("IP address: ");
            Serial.println(WiFi.softAPIP());
        } else {
            Serial.println("ERROR: WiFi AP start failed");
        }
        return ok;
    }
}

void wifi_init() {
    String ssid, pass;
    if (load_sta_config(ssid, pass)) {
        g_sta_ssid = ssid;
        g_sta_pass = pass;
        g_sta_mode = true;
        Serial.print("Joining home network: ");
        Serial.println(ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        for (int i = 0; i < 80; ++i) {  // up to 8 s
            if (WiFi.status() == WL_CONNECTED) break;
            delay(100);
            watchdog_update();  // the join can outlast the watchdog budget
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("Station IP address: ");
            Serial.println(WiFi.localIP());
            return;
        }
        Serial.println("Home network unreachable - starting demo AP instead");
        // Fall back to AP-only. g_sta_mode MUST clear: the reconnect
        // service would otherwise keep issuing WiFi.disconnect()/begin()
        // every 15 s, tearing at the very AP that is the recovery path.
        g_sta_mode = false;
    }
    start_ap();
}

String wifi_ip() {
    if (WiFi.getMode() & WIFI_STA) return WiFi.localIP().toString();
    return WiFi.softAPIP().toString();
}

bool wifi_connected() {
    if (WiFi.getMode() & WIFI_STA) return WiFi.status() == WL_CONNECTED;
    return WiFi.softAPgetStationNum() > 0;
}

void wifi_set_station(const String& ssid, const String& pass) {
    if (ssid.length() > 32 || pass.length() > 64) {
        Serial.println("ssid must be <= 32 chars, password <= 64");
        return;
    }
    EEPROM.begin(4096);
    size_t off = EE_ADDR;
    EEPROM.write(off++, EE_MAGIC);
    EEPROM.write(off++, static_cast<uint8_t>(ssid.length()));
    for (size_t i = 0; i < ssid.length(); ++i) EEPROM.write(off++, ssid[i]);
    EEPROM.write(off++, static_cast<uint8_t>(pass.length()));
    for (size_t i = 0; i < pass.length(); ++i) EEPROM.write(off++, pass[i]);
    EEPROM.commit();
    Serial.println("WiFi saved; rebooting to join");
    delay(100);
    rp2040.reboot();
}

void wifi_clear_station() {
    EEPROM.begin(4096);
    EEPROM.write(EE_ADDR, 0);
    EEPROM.commit();
    Serial.println("WiFi config cleared; demo AP on next boot");
}

void wifi_print_status() {    Serial.print("mode=");
    if (WiFi.getMode() & WIFI_STA) {
        Serial.print("station connected=");
        Serial.print(WiFi.status() == WL_CONNECTED);
        Serial.print(" ip=");
        Serial.println(WiFi.localIP());
    } else {
        Serial.print("ap stations=");
        Serial.print(WiFi.softAPgetStationNum());
        Serial.print(" ip=");
        Serial.println(WiFi.softAPIP());
    }
}

void wifi_service() {
    static unsigned long last_connected_ms = 0;
    static unsigned long last_try_ms = 0;
    if (!g_sta_mode) return;  // AP mode: stations come and go on their own

    if (WiFi.status() == WL_CONNECTED) {
        last_connected_ms = millis();
        return;
    }
    if (millis() - last_connected_ms < 3000) return;  // brief glitch: ignore
    if (millis() - last_try_ms < 15000) return;      // one attempt per 15 s
    last_try_ms = millis();
    Serial.println("[wifi] connection lost; rejoining");
    WiFi.disconnect();
    WiFi.begin(g_sta_ssid.c_str(), g_sta_pass.c_str());
}

}  // namespace tt
