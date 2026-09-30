#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>

ServerConfig ServerConfig::load(const std::string& path) {
    ServerConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Warning: could not open " << path << ", using defaults\n";
        return cfg;
    }
    std::string line;
    while (std::getline(f, line)) {
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            if (key == "HASH_SEED") cfg.hash_seed = std::stoull(val);
            else if (key == "PROBE_CAP") cfg.probe_cap = std::stoi(val);
            else if (key == "EVICT_SAMPLE") cfg.evict_sample = std::stoi(val);
            else if (key == "MAGIC") cfg.magic = val;
            else if (key == "maxmemory") cfg.max_memory = std::stoull(val);
            else if (key == "maxmemory-policy") {
                if (val == "noeviction") cfg.evict_policy = EvictPolicy::NOEVICTION;
                else if (val == "allkeys-random") cfg.evict_policy = EvictPolicy::ALLKEYS_RANDOM;
                else if (val == "allkeys-lru") cfg.evict_policy = EvictPolicy::ALLKEYS_LRU;
                else if (val == "volatile-lru") cfg.evict_policy = EvictPolicy::VOLATILE_LRU;
                else if (val == "volatile-ttl") cfg.evict_policy = EvictPolicy::VOLATILE_TTL;
            }
            else if (key == "appendfsync") {
                if (val == "always") cfg.fsync_mode = FsyncMode::ALWAYS;
                else if (val == "everysec") cfg.fsync_mode = FsyncMode::EVERYSEC;
                else if (val == "no") cfg.fsync_mode = FsyncMode::NO;
            }
        }
    }
    return cfg;
}
