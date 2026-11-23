#pragma once

namespace tt {

// Register all HTTP routes and start the server.
void web_server_init();

// Service one pending HTTP client, if any. Call from the main loop.
void web_server_loop();

}  // namespace tt
