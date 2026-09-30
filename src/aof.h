#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum class FsyncMode {
    ALWAYS,
    EVERYSEC,
    NO
};

class AofManager {
public:
    AofManager(const std::string& path, FsyncMode mode);
    ~AofManager();

    // Append a command to the log.
    void append(const std::vector<std::string>& cmd);

    // Call this before acknowledging to client (e.g., at end of event loop tick)
    void flush_if_needed(bool force_always = false);
    
    // Background fsync loop or check called periodically
    void background_fsync();

    // Replay log on startup
    void replay();

    // Rewrite log (compaction)
    void rewrite();

    // Temporarily pause appending during replay
    void set_replay_mode(bool on) { replay_mode = on; }

private:
    std::string path;
    FsyncMode mode;
    int fd;
    uint64_t seq;
    
    std::string buffer; // Pending data to write to disk
    long long last_fsync_ms;
    bool replay_mode;
    
    uint32_t checksum(const char* data, size_t len);
    void write_buffer();
    void sync();
};
