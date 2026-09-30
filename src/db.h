#pragma once
#include "dict.h"
#include "config.h"
#include "aof.h"

struct ServerState {
    ServerConfig config;
    Dict* db;
    AofManager* aof;
    uint64_t db_version = 1;


    ServerState(const ServerConfig& cfg) : config(cfg) {
        db = new Dict(cfg.hash_seed, cfg.probe_cap);
        aof = new AofManager("appendonly.aof", cfg.fsync_mode);
    }

    ~ServerState() {
        delete aof; // must be deleted before db so it can flush
        delete db;
    }
};

extern ServerState* g_state;

void init_server_state(const ServerConfig& cfg);
DictEntry* db_get(const std::string& key);
void db_active_expire();
void db_evict_if_needed();
