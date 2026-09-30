#include "server.h"
#include "db.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

Server::Server(int port) : port(port), server_fd(-1), kq(-1) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Set non-blocking
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, 10000) < 0) {
        perror("listen");
        exit(1);
    }

    kq = kqueue();
    if (kq < 0) {
        perror("kqueue");
        exit(1);
    }

    update_kqueue(server_fd, EVFILT_READ, EV_ADD | EV_ENABLE);
    connections.resize(65536); // Support up to 64k fds
}

Server::~Server() {
    if (server_fd >= 0) close(server_fd);
    if (kq >= 0) close(kq);
}

void Server::update_kqueue(int fd, int filter, int action) {
    struct kevent ev;
    EV_SET(&ev, fd, filter, action, 0, 0, nullptr);
    kevent(kq, &ev, 1, nullptr, 0, nullptr);
}

void Server::accept_connection() {
    while (true) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EMFILE) {
                // Too many open files. Stop accepting for now.
                break;
            } else {
                perror("accept");
                break;
            }
        }

        fcntl(client_fd, F_SETFL, O_NONBLOCK);
        
        if ((size_t)client_fd >= connections.size()) {
            close(client_fd);
            continue;
        }

        Connection& conn = connections[client_fd];
        conn.fd = client_fd;
        conn.state = ConnState::READING;
        conn.parser.reset();
        conn.write_buffer.clear();
        conn.write_offset = 0;

        update_kqueue(client_fd, EVFILT_READ, EV_ADD | EV_ENABLE);
    }
}

void Server::close_connection(int fd) {
    if (fd < 0 || (size_t)fd >= connections.size()) return;
    Connection& conn = connections[fd];
    if (conn.fd == -1) return;
    
    update_kqueue(fd, EVFILT_READ, EV_DELETE);
    update_kqueue(fd, EVFILT_WRITE, EV_DELETE);
    close(fd);
    
    conn.fd = -1;
    conn.write_buffer.clear();
    conn.parser.reset();
}

void Server::queue_reply(Connection& conn, const std::string& reply) {
    conn.write_buffer += reply;
    if (conn.write_buffer.size() > Connection::HARD_LIMIT) {
        conn.state = ConnState::CLOSING;
    } else if (conn.state == ConnState::READING) {
        conn.state = ConnState::WRITING;
        update_kqueue(conn.fd, EVFILT_WRITE, EV_ADD | EV_ENABLE);
    }
}

#include "cmd.h"

void Server::process_command(Connection& conn, const std::vector<std::string>& cmd) {
    if (cmd.empty()) return;
    
    std::string op = cmd[0];
    for (char& c : op) c = std::toupper(c);
    
    if (op == "MULTI") {
        if (conn.in_multi) queue_reply(conn, "-ERR MULTI calls can not be nested\r\n");
        else {
            conn.in_multi = true;
            queue_reply(conn, "+OK\r\n");
        }
        return;
    } else if (op == "EXEC") {
        if (!conn.in_multi) {
            queue_reply(conn, "-ERR EXEC without MULTI\r\n");
            return;
        }
        
        bool abort = false;
        for (const auto& wk : conn.watched_keys) {
            DictEntry* e = db_get(wk.first);
            uint64_t current_version = e ? e->val.version : 0;
            if (current_version != wk.second) {
                abort = true;
                break;
            }
        }
        
        if (abort) {
            conn.in_multi = false;
            conn.multi_queue.clear();
            conn.watched_keys.clear();
            queue_reply(conn, "*-1\r\n");
            return;
        }
        
        queue_reply(conn, "*" + std::to_string(conn.multi_queue.size()) + "\r\n");
        for (const auto& qcmd : conn.multi_queue) {
            std::string reply = execute_command(qcmd);
            queue_reply(conn, reply);
        }
        
        conn.in_multi = false;
        conn.multi_queue.clear();
        conn.watched_keys.clear();
        return;
    } else if (op == "DISCARD") {
        if (!conn.in_multi) {
            queue_reply(conn, "-ERR DISCARD without MULTI\r\n");
            return;
        }
        conn.in_multi = false;
        conn.multi_queue.clear();
        conn.watched_keys.clear();
        queue_reply(conn, "+OK\r\n");
        return;
    } else if (op == "WATCH") {
        if (conn.in_multi) {
            queue_reply(conn, "-ERR WATCH inside MULTI is not allowed\r\n");
            return;
        }
        for (size_t i = 1; i < cmd.size(); i++) {
            DictEntry* e = db_get(cmd[i]);
            uint64_t v = e ? e->val.version : 0;
            conn.watched_keys.push_back({cmd[i], v});
        }
        queue_reply(conn, "+OK\r\n");
        return;
    } else if (op == "UNWATCH") {
        conn.watched_keys.clear();
        queue_reply(conn, "+OK\r\n");
        return;
    }
    
    if (conn.in_multi) {
        conn.multi_queue.push_back(cmd);
        queue_reply(conn, "+QUEUED\r\n");
        return;
    }

    std::string reply = execute_command(cmd);
    queue_reply(conn, reply);
}

void Server::handle_read(Connection& conn) {
    char buf[4096];
    while (true) {
        ssize_t n = read(conn.fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else {
                conn.state = ConnState::CLOSING;
                break;
            }
        } else if (n == 0) {
            // EOF
            conn.state = ConnState::CLOSING;
            break;
        }

        size_t consumed = 0;
        while (consumed < (size_t)n) {
            bool ready = conn.parser.feed(buf + consumed, n - consumed, consumed);
            if (conn.parser.has_error()) {
                queue_reply(conn, "-ERR Protocol error\r\n");
                conn.state = ConnState::DRAINING;
                break;
            }
            if (ready) {
                auto cmd = conn.parser.get_command();
                process_command(conn, cmd);
                if (conn.state == ConnState::CLOSING || conn.state == ConnState::DRAINING) {
                    break;
                }
            }
        }
    }
    
    // Flush AOF if we processed any commands (and if fsync mode requires it)
    if (g_state && g_state->aof) {
        g_state->aof->flush_if_needed();
    }
}

void Server::handle_write(Connection& conn) {
    while (conn.write_offset < conn.write_buffer.size()) {
        ssize_t n = write(conn.fd, conn.write_buffer.data() + conn.write_offset, conn.write_buffer.size() - conn.write_offset);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else {
                conn.state = ConnState::CLOSING;
                break;
            }
        }
        conn.write_offset += n;
    }

    if (conn.write_offset == conn.write_buffer.size()) {
        conn.write_buffer.clear();
        conn.write_offset = 0;
        if (conn.state == ConnState::WRITING) {
            conn.state = ConnState::READING;
            update_kqueue(conn.fd, EVFILT_WRITE, EV_DELETE);
        } else if (conn.state == ConnState::DRAINING) {
            conn.state = ConnState::CLOSING;
        }
    }
}

void Server::run() {
    struct kevent events[64];
    std::cout << "Starting server on port " << port << std::endl;
    
    while (true) {
        int nev = kevent(kq, nullptr, 0, events, 64, nullptr);
        if (nev < 0) {
            if (errno == EINTR) continue;
            perror("kevent");
            break;
        }

        for (int i = 0; i < nev; i++) {
            int fd = events[i].ident;
            if (fd == server_fd) {
                accept_connection();
            } else {
                if ((size_t)fd >= connections.size()) continue;
                Connection& conn = connections[fd];
                if (conn.fd == -1) continue;
                
                if (events[i].flags & EV_EOF) {
                    conn.state = ConnState::CLOSING;
                } else {
                    if (events[i].filter == EVFILT_READ && (conn.state == ConnState::READING || conn.state == ConnState::WRITING)) {
                        handle_read(conn);
                    }
                    if (events[i].filter == EVFILT_WRITE && (conn.state == ConnState::WRITING || conn.state == ConnState::DRAINING)) {
                        handle_write(conn);
                    }
                }

                if (conn.state == ConnState::CLOSING) {
                    close_connection(fd);
                }
            }
        }
    }
}
