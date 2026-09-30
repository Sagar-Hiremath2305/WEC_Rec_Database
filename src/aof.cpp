#include "aof.h"
#include "db.h"
#include "utils.h"
#include "cmd.h"
#include "resp.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <iostream>
#include <cstring>

// Simple CRC32-like hash for checksum
uint32_t AofManager::checksum(const char* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum = (sum * 31) + (unsigned char)data[i];
    }
    return sum;
}

AofManager::AofManager(const std::string& path, FsyncMode mode) 
    : path(path), mode(mode), seq(0), last_fsync_ms(get_time_ms()), replay_mode(false) {
    
    fd = open(path.c_str(), O_CREAT | O_APPEND | O_RDWR, 0644);
    if (fd < 0) {
        perror("aof open");
        exit(1);
    }
}

AofManager::~AofManager() {
    if (fd >= 0) {
        flush_if_needed(true);
        sync();
        close(fd);
    }
}

void AofManager::append(const std::vector<std::string>& cmd) {
    if (replay_mode) return; // Don't append commands while replaying!

    std::string record = "*" + std::to_string(cmd.size()) + "\r\n";
    for (const auto& arg : cmd) {
        record += "$" + std::to_string(arg.size()) + "\r\n";
        record += arg + "\r\n";
    }
    
    seq++;
    uint32_t len = record.size();
    uint32_t chk = checksum(record.data(), len);
    
    // Header format: [8 bytes seq][4 bytes len][4 bytes chk] (Little Endian assumed for simplicity here, but should be explicit ideally)
    char header[16];
    memcpy(header, &seq, 8);
    memcpy(header + 8, &len, 4);
    memcpy(header + 12, &chk, 4);
    
    buffer.append(header, 16);
    buffer.append(record);
}

void AofManager::write_buffer() {
    if (buffer.empty()) return;
    
    size_t written = 0;
    while (written < buffer.size()) {
        ssize_t n = write(fd, buffer.data() + written, buffer.size() - written);
        if (n <= 0) {
            if (errno == EINTR) continue;
            perror("aof write");
            break;
        }
        written += n;
    }
    buffer.clear();
}

void AofManager::sync() {
    write_buffer();
    fsync(fd);
    last_fsync_ms = get_time_ms();
}

void AofManager::flush_if_needed(bool force_always) {
    write_buffer();
    if (mode == FsyncMode::ALWAYS || force_always) {
        sync();
    }
}

void AofManager::background_fsync() {
    if (mode == FsyncMode::EVERYSEC) {
        if (get_time_ms() - last_fsync_ms >= 1000) {
            sync();
        }
    }
}

void AofManager::replay() {
    // Read the file from the beginning
    lseek(fd, 0, SEEK_SET);
    set_replay_mode(true);

    while (true) {
        char header[16];
        ssize_t n = read(fd, header, 16);
        if (n == 0) {
            break; // Clean EOF
        }
        if (n < 16) {
            std::cerr << "AOF: Half-written header detected. Stopping replay.\n";
            // Truncate the file to discard the partial record
            off_t current_pos = lseek(fd, 0, SEEK_CUR);
            ftruncate(fd, current_pos - n);
            break;
        }

        uint64_t r_seq;
        uint32_t r_len;
        uint32_t r_chk;
        memcpy(&r_seq, header, 8);
        memcpy(&r_len, header + 8, 4);
        memcpy(&r_chk, header + 12, 4);

        if (r_len > 512 * 1024 * 1024) { // 512 MB sanity limit
            std::cerr << "AOF: Corrupt record length. Stopping replay.\n";
            off_t current_pos = lseek(fd, 0, SEEK_CUR);
            ftruncate(fd, current_pos - 16);
            break;
        }

        std::string payload(r_len, '\0');
        n = read(fd, &payload[0], r_len);
        if (n < r_len) {
            std::cerr << "AOF: Half-written payload detected. Stopping replay.\n";
            off_t current_pos = lseek(fd, 0, SEEK_CUR);
            ftruncate(fd, current_pos - 16 - n);
            break;
        }

        uint32_t actual_chk = checksum(payload.data(), r_len);
        if (actual_chk != r_chk) {
            std::cerr << "AOF: Checksum mismatch. Corrupt record. Stopping replay.\n";
            off_t current_pos = lseek(fd, 0, SEEK_CUR);
            ftruncate(fd, current_pos - 16 - r_len);
            break;
        }

        // Parse and execute
        RespParser parser;
        size_t consumed = 0;
        bool ready = parser.feed(payload.data(), r_len, consumed);
        if (ready && !parser.has_error()) {
            auto cmd = parser.get_command();
            execute_command(cmd); // execute_command doesn't append to AOF because replay_mode = true
            seq = r_seq; // update our seq to match
        } else {
            std::cerr << "AOF: RESP parse error in payload. Stopping replay.\n";
            off_t current_pos = lseek(fd, 0, SEEK_CUR);
            ftruncate(fd, current_pos - 16 - r_len);
            break;
        }
    }
    
    // Reset fd offset to end of file for future appends
    lseek(fd, 0, SEEK_END);
    set_replay_mode(false);
    std::cout << "AOF replay complete. Seq=" << seq << "\n";
}

#include "aof.h"
#include "db.h"
#include "dict_iter.h"
#include "utils.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <iostream>

void AofManager::rewrite() {
    std::string temp_path = path + ".tmp";
    int temp_fd = open(temp_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (temp_fd < 0) return;
    
    DictIterator it(g_state->db);
    uint64_t new_seq = seq; // Use the current sequence number for the start. In real Redis, new writes appended during rewrite need merging. For simplicity, we just do it synchronously here (stop the world).
    
    while (DictEntry* e = it.next()) {
        if (e->val.expire_ms != -1 && e->val.expire_ms < get_time_ms()) {
            continue; // Skip expired
        }
        
        std::vector<std::string> cmd;
        if (e->val.type == ValType::STRING) {
            cmd.push_back("SET");
            cmd.push_back(e->key);
            cmd.push_back(*static_cast<std::string*>(e->val.ptr));
            if (e->val.expire_ms != -1) {
                cmd.push_back("PX");
                cmd.push_back(std::to_string(e->val.expire_ms - get_time_ms()));
            }
        }
        // Other types (HASH, SET, LIST) would go here
        
        if (!cmd.empty()) {
            std::string record = "*" + std::to_string(cmd.size()) + "\r\n";
            for (const auto& arg : cmd) {
                record += "$" + std::to_string(arg.size()) + "\r\n";
                record += arg + "\r\n";
            }
            
            new_seq++;
            uint32_t len = record.size();
            uint32_t chk = checksum(record.data(), len);
            
            char header[16];
            memcpy(header, &new_seq, 8);
            memcpy(header + 8, &len, 4);
            memcpy(header + 12, &chk, 4);
            
            write(temp_fd, header, 16);
            write(temp_fd, record.data(), len);
        }
    }
    
    fsync(temp_fd);
    close(temp_fd);
    
    rename(temp_path.c_str(), path.c_str());
    
    int dir_fd = open(".", O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }
}
