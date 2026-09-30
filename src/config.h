#pragma once
#include <string>
#include "aof.h"

enum class EvictPolicy {
    NOEVICTION,
    ALLKEYS_RANDOM,
    ALLKEYS_LRU,
    VOLATILE_LRU,
    VOLATILE_TTL
};

struct ServerConfig {
    uint64_t hash_seed = 0;
    int probe_cap = 16;
    int evict_sample = 5;
    std::string magic;
    
    size_t max_memory = 0; // 0 means unlimited
    EvictPolicy evict_policy = EvictPolicy::NOEVICTION;
    
    FsyncMode fsync_mode = FsyncMode::EVERYSEC;

    static ServerConfig load(const std::string& path);
};
