#include "server.h"
#include "db.h"

int main() {
    ServerConfig cfg = ServerConfig::load("params.txt");
    init_server_state(cfg);
    g_state->aof->replay();
    Server server(6379);
    server.run();
    return 0;
}
