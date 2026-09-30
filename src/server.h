#pragma once

#include <vector>
#include <string>
#include "resp.h"
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

enum class ConnState {
    READING,
    WRITING,
    DRAINING,
    CLOSING
};

struct Connection {
    int fd = -1;
    ConnState state = ConnState::READING;
    RespParser parser;
    std::string write_buffer;
    size_t write_offset = 0;
    bool in_multi = false;
    std::vector<std::vector<std::string>> multi_queue;
    std::vector<std::pair<std::string, uint64_t>> watched_keys;

    
    // Limits
    static constexpr size_t SOFT_LIMIT = 10 * 1024 * 1024; // 10MB
    static constexpr size_t HARD_LIMIT = 32 * 1024 * 1024; // 32MB
};

class Server {
public:
    Server(int port);
    ~Server();

    void run();

private:
    int port;
    int server_fd;
    int kq;
    std::vector<Connection> connections;

    void accept_connection();
    void handle_read(Connection& conn);
    void handle_write(Connection& conn);
    void process_command(Connection& conn, const std::vector<std::string>& cmd);
    void queue_reply(Connection& conn, const std::string& reply);
    void close_connection(int fd);
    void update_kqueue(int fd, int filter, int action);
};
