#include "dict.h"
#include <iostream>
#include "zset.h"

#include <deque>

DictValue::~DictValue() {
    if (ptr) {
        if (type == ValType::STRING) {
            delete static_cast<std::string*>(ptr);
        } else if (type == ValType::LIST) {
            delete static_cast<std::deque<std::string>*>(ptr);
        } else if (type == ValType::HASH || type == ValType::SET) {
            delete static_cast<Dict*>(ptr);
        } else if (type == ValType::ZSET) {
            delete static_cast<ZSet*>(ptr);
        } else if (type == ValType::DOUBLE) {
            delete static_cast<double*>(ptr);


        }
    }
}

DictValue::DictValue(DictValue&& other) noexcept 
    : type(other.type), ptr(other.ptr), expire_ms(other.expire_ms), lru(other.lru), version(other.version) {
    other.ptr = nullptr;
}

DictValue& DictValue::operator=(DictValue&& other) noexcept {
    if (this != &other) {
        std::swap(type, other.type);
        std::swap(ptr, other.ptr);
        std::swap(expire_ms, other.expire_ms);
        std::swap(lru, other.lru);
        std::swap(version, other.version);
    }
    return *this;
}

Dict::Dict(uint64_t hash_seed, int probe_cap) : seed(hash_seed), max_probes(probe_cap), rehashidx(-1) {
    ht[0].init(16); // Initial small size
    ht[1].init(0);
}

void Dict::perform_rehash_step() {
    if (!is_rehashing()) return;

    int steps = 10; // Migrate up to 10 buckets per step
    while (steps-- > 0) {
        if (ht[0].used == 0) {
            ht[0] = std::move(ht[1]);
            ht[1].init(0);
            rehashidx = -1;
            return;
        }

        while (rehashidx < (long long)ht[0].size && !ht[0].table[rehashidx].occupied) {
            rehashidx++;
        }

        if (rehashidx >= (long long)ht[0].size) {
            ht[0] = std::move(ht[1]);
            ht[1].init(0);
            rehashidx = -1;
            return;
        }

        // Migrate ht[0].table[rehashidx]
        DictEntry& old_entry = ht[0].table[rehashidx];
        if (!old_entry.deleted) {
            uint64_t h = dict_hash(old_entry.key, seed);
            DictEntry* new_slot = find_free_slot(ht[1], old_entry.key, h);
            if (new_slot) {
                new_slot->key = std::move(old_entry.key);
                new_slot->val = std::move(old_entry.val);
                new_slot->occupied = true;
                new_slot->deleted = false;
                ht[1].used++;
            }
        }
        
        old_entry.occupied = false;
        old_entry.deleted = false;
        ht[0].used--;
        rehashidx++;
    }
}

void Dict::expand_if_needed() {
    if (is_rehashing()) return;
    
    // If load factor > 0.75, expand
    if (ht[0].used + ht[0].deleted > ht[0].size * 3 / 4) {
        ht[1].init(ht[0].size * 2);
        rehashidx = 0;
    }
}

DictEntry* Dict::find_free_slot(DictTable& t, const std::string& key, uint64_t hash) {
    size_t idx = hash & (t.size - 1);
    size_t first_deleted = (size_t)-1;

    for (int i = 0; i < max_probes; i++) {
        if (!t.table[idx].occupied) {
            if (first_deleted != (size_t)-1) {
                return &t.table[first_deleted];
            }
            return &t.table[idx];
        } else if (t.table[idx].deleted) {
            if (first_deleted == (size_t)-1) {
                first_deleted = idx;
            }
        } else if (t.table[idx].key == key) {
            return &t.table[idx];
        }
        idx = (idx + 1) & (t.size - 1);
    }
    
    // If we exceed max_probes, we must resize.
    if (!is_rehashing()) {
        ht[1].init(t.size * 2);
        rehashidx = 0;
    }
    return nullptr; // Caller should retry
}

DictEntry* Dict::find_entry(DictTable& t, const std::string& key, uint64_t hash) {
    if (t.size == 0) return nullptr;
    size_t idx = hash & (t.size - 1);
    
    for (int i = 0; i < max_probes; i++) {
        if (!t.table[idx].occupied) {
            return nullptr;
        }
        if (!t.table[idx].deleted && t.table[idx].key == key) {
            return &t.table[idx];
        }
        idx = (idx + 1) & (t.size - 1);
    }
    return nullptr;
}

bool Dict::set(const std::string& key, DictValue val) {
    if (is_rehashing()) perform_rehash_step();
    expand_if_needed();

    uint64_t h = dict_hash(key, seed);
    
    DictTable* t = is_rehashing() ? &ht[1] : &ht[0];
    
    DictEntry* entry = nullptr;
    while (!entry) {
        entry = find_free_slot(*t, key, h);
        if (!entry && !is_rehashing()) {
            expand_if_needed();
            t = &ht[1]; // rebind pointer instead of copying
        } else if (!entry) {
            // we are rehashing and ht[1] is also full? That shouldn't happen.
            break;
        }
    }
    
    if (entry) {
        bool is_new = !entry->occupied || entry->deleted;
        entry->key = key;
        entry->val = std::move(val);
        entry->occupied = true;
        entry->deleted = false;
        if (is_new) {
            t->used++;
            return true;
        }
        return false;
    }
    return false;
}

DictEntry* Dict::get(const std::string& key) {
    if (is_rehashing()) perform_rehash_step();
    
    uint64_t h = dict_hash(key, seed);
    DictEntry* e = find_entry(ht[0], key, h);
    if (!e && is_rehashing()) {
        e = find_entry(ht[1], key, h);
    }
    return e;
}

bool Dict::del(const std::string& key) {
    if (is_rehashing()) perform_rehash_step();
    
    uint64_t h = dict_hash(key, seed);
    DictEntry* e = find_entry(ht[0], key, h);
    if (e) {
        e->deleted = true;
        ht[0].used--;
        ht[0].deleted++;
        return true;
    }
    if (is_rehashing()) {
        e = find_entry(ht[1], key, h);
        if (e) {
            e->deleted = true;
            ht[1].used--;
            ht[1].deleted++;
            return true;
        }
    }
    return false;
}

DictEntry* Dict::get_random_entry() {
    if (size() == 0) return nullptr;
    if (is_rehashing()) perform_rehash_step();

    // Loop to find an occupied entry
    while (true) {
        int table_idx = 0;
        if (is_rehashing()) {
            table_idx = (rand() % (ht[0].size + ht[1].size)) < ht[0].size ? 0 : 1;
        }
        DictTable& t = ht[table_idx];
        if (t.size == 0) continue;
        
        size_t idx = rand() & (t.size - 1);
        if (t.table[idx].occupied && !t.table[idx].deleted) {
            return &t.table[idx];
        }
    }
}
