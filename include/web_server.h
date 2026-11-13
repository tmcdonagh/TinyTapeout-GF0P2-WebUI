#pragma once

namespace tt {

// Start the HTTP server and register API handlers.
void web_server_init();

// Call frequently (e.g., from loop()) to handle incoming HTTP requests.
void web_server_loop();

}  // namespace tt
