#include "db.h"
#include "utils.h"
#include "memory.h"
#include <iostream>

ServerState* g_state = nullptr;

void init_server_state(const ServerConfig& cfg) {
    g_state = new ServerState(cfg);
}

DictEntry* db_get(const std::string& key) {
    DictEntry* e = g_state->db->get(key);
    if (!e || e->deleted) return nullptr;
    
    if (e->val.expire_ms != -1) {
        if (get_time_ms() > e->val.expire_ms) {
            g_state->db->del(key);
            return nullptr;
        }
    }
    
    e->val.lru = get_time_ms();
    return e;
}

void db_active_expire() {
    if (g_state->db->size() == 0) return;
    long long start = get_time_ms();
    
    while (true) {
        int expired_count = 0;
        int sampled = 0;
        for (int i = 0; i < 20; i++) {
            DictEntry* e = g_state->db->get_random_entry();
            if (!e) break;
            if (e->val.expire_ms != -1) {
                sampled++;
                if (get_time_ms() > e->val.expire_ms) {
                    g_state->db->del(e->key);
                    expired_count++;
                }
            }
        }
        if (sampled == 0 || (expired_count * 100 / sampled) < 25) break;
        if (get_time_ms() - start > 1) break;
    }
}

void db_evict_if_needed() {
    if (g_state->config.max_memory == 0) return;
    if (g_state->config.evict_policy == EvictPolicy::NOEVICTION) return;

    while (get_used_memory() > g_state->config.max_memory) {
        std::string best_key;
        uint64_t best_val = (uint64_t)-1;
        bool any_found = false;

        int sample_size = g_state->config.evict_sample;
        int max_attempts = sample_size * 10;
        
        for (int i = 0; i < max_attempts && sample_size > 0; i++) {
            DictEntry* e = g_state->db->get_random_entry();
            if (!e) break;

            if (g_state->config.evict_policy == EvictPolicy::VOLATILE_LRU || 
                g_state->config.evict_policy == EvictPolicy::VOLATILE_TTL) {
                if (e->val.expire_ms == -1) continue;
            }
            sample_size--;

            if (!any_found) {
                best_key = e->key;
                if (g_state->config.evict_policy == EvictPolicy::ALLKEYS_LRU || g_state->config.evict_policy == EvictPolicy::VOLATILE_LRU) {
                    best_val = e->val.lru;
                } else if (g_state->config.evict_policy == EvictPolicy::VOLATILE_TTL) {
                    best_val = e->val.expire_ms;
                } else if (g_state->config.evict_policy == EvictPolicy::ALLKEYS_RANDOM) {
                    any_found = true;
                    break;
                }
                any_found = true;
                continue;
            }

            if (g_state->config.evict_policy == EvictPolicy::ALLKEYS_LRU || g_state->config.evict_policy == EvictPolicy::VOLATILE_LRU) {
                if (e->val.lru < best_val) {
                    best_val = e->val.lru;
                    best_key = e->key;
                }
            } else if (g_state->config.evict_policy == EvictPolicy::VOLATILE_TTL) {
                if ((uint64_t)e->val.expire_ms < best_val) {
                    best_val = e->val.expire_ms;
                    best_key = e->key;
                }
            }
        }

        if (any_found) {
            g_state->db->del(best_key);
        } else {
            break;
        }
    }
}
