#include "dns_server.h"
#include "wifi_config.h"
#include <WiFi.h>
#include <WiFiUdp.h>

namespace tt {

namespace {
    constexpr uint16_t DNS_PORT = 53;
    constexpr size_t DNS_MAX_PACKET = 512;

    WiFiUDP g_dns_udp;
    uint8_t g_packet[DNS_MAX_PACKET];

    // DNS header offsets.
    constexpr size_t OFF_ID = 0;
    constexpr size_t OFF_FLAGS = 2;
    constexpr size_t OFF_QDCOUNT = 4;
    constexpr size_t OFF_ANCOUNT = 6;
    constexpr size_t OFF_NSCOUNT = 8;
    constexpr size_t OFF_ARCOUNT = 10;
    constexpr size_t DNS_HEADER_SIZE = 12;

    uint16_t read_u16(const uint8_t* buf, size_t offset) {
        return (static_cast<uint16_t>(buf[offset]) << 8) | buf[offset + 1];
    }

    void write_u16(uint8_t* buf, size_t offset, uint16_t value) {
        buf[offset] = static_cast<uint8_t>(value >> 8);
        buf[offset + 1] = static_cast<uint8_t>(value & 0xFF);
    }

    // Find the end of the question section. Returns the offset just past the
    // last question, or 0 if the packet is malformed.
    size_t skip_questions(const uint8_t* buf, size_t len, uint16_t qdcount) {
        size_t pos = DNS_HEADER_SIZE;
        for (uint16_t q = 0; q < qdcount; ++q) {
            // Skip the QNAME (length-prefixed labels, ending with 0).
            while (pos < len) {
                uint8_t label_len = buf[pos];
                if (label_len == 0) {
                    ++pos;
                    break;
                }
                if ((label_len & 0xC0) == 0xC0) {
                    // Compression pointer: skip 2 bytes and we're done with this name.
                    pos += 2;
                    break;
                }
                pos += 1 + label_len;
            }
            if (pos > len) {
                return 0;
            }
            // Skip QTYPE and QCLASS.
            pos += 4;
            if (pos > len) {
                return 0;
            }
        }
        return pos;
    }
}

void dns_server_init() {
    if (g_dns_udp.begin(DNS_PORT)) {
        Serial.println("DNS server started on port 53");
    } else {
        Serial.println("ERROR: DNS server failed to start");
    }
}

void dns_server_loop() {
    // The wildcard DNS only makes sense for the captive-portal AP; on the
    // home network answering every query with our own IP breaks name
    // resolution for any client that uses the Pico's DHCP-told resolver.
    if (!(WiFi.getMode() & WIFI_AP)) return;
    int len = g_dns_udp.parsePacket();
    if (len <= 0 || static_cast<size_t>(len) > DNS_MAX_PACKET) {
        return;
    }

    int read = g_dns_udp.read(g_packet, DNS_MAX_PACKET);
    if (read < static_cast<int>(DNS_HEADER_SIZE)) {
        return;
    }

    uint16_t id = read_u16(g_packet, OFF_ID);
    uint16_t flags = read_u16(g_packet, OFF_FLAGS);
    uint16_t qdcount = read_u16(g_packet, OFF_QDCOUNT);

    // Only answer standard queries.
    if ((flags & 0x8000) != 0 || qdcount == 0 || qdcount > 8) {
        return;
    }

    size_t question_end = skip_questions(g_packet, read, qdcount);
    if (question_end == 0 || question_end > static_cast<size_t>(read)) {
        return;
    }

    // Build the response in place. The question section stays where it is;
    // answers are appended after it.
    size_t pos = question_end;

    for (uint16_t i = 0; i < qdcount && pos + 16 <= DNS_MAX_PACKET; ++i) {
        // Pointer to the first question name at offset 12 in the header.
        write_u16(g_packet, pos, 0xC00C);
        pos += 2;
        // Type A.
        write_u16(g_packet, pos, 0x0001);
        pos += 2;
        // Class IN.
        write_u16(g_packet, pos, 0x0001);
        pos += 2;
        // TTL: 300 seconds (5 minutes), big-endian.
        g_packet[pos++] = 0;
        g_packet[pos++] = 0;
        g_packet[pos++] = 0x01;
        g_packet[pos++] = 0x2C;
        // RDLENGTH: 4 bytes for IPv4.
        write_u16(g_packet, pos, 4);
        pos += 2;
        // RDATA: AP IP address.
        IPAddress ip = WiFi.softAPIP();
        g_packet[pos++] = ip[0];
        g_packet[pos++] = ip[1];
        g_packet[pos++] = ip[2];
        g_packet[pos++] = ip[3];
    }

    // Update header for response.
    write_u16(g_packet, OFF_ID, id);
    write_u16(g_packet, OFF_FLAGS, 0x8180);  // standard response, no error
    write_u16(g_packet, OFF_ANCOUNT, qdcount);
    write_u16(g_packet, OFF_NSCOUNT, 0);
    write_u16(g_packet, OFF_ARCOUNT, 0);
    // QDCOUNT is left unchanged.

    g_dns_udp.beginPacket(g_dns_udp.remoteIP(), g_dns_udp.remotePort());
    g_dns_udp.write(g_packet, pos);
    g_dns_udp.endPacket();
}

}  // namespace tt
