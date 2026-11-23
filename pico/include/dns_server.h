#pragma once

namespace tt {

// Start the wildcard DNS responder (captive portal).
void dns_server_init();

// Service one pending DNS packet, if any. Call from the main loop.
void dns_server_loop();

}  // namespace tt
