#pragma once
#include <string>
#include <vector>
#include <memory>
#include "hash.h"

enum class ValType {
    STRING,
    HASH,
    LIST,
    SET
    , ZSET
    , DOUBLE

};

struct DictValue {
    ValType type = ValType::STRING;
    void* ptr = nullptr;
    long long expire_ms = -1; // -1 means no expiry
    uint64_t lru = 0;
    uint64_t version = 0;

    DictValue() = default;
    ~DictValue();

    // Disable copy
    DictValue(const DictValue&) = delete;
    DictValue& operator=(const DictValue&) = delete;

    // Move semantics
    DictValue(DictValue&& other) noexcept;
    DictValue& operator=(DictValue&& other) noexcept;
};

struct DictEntry {
    std::string key;
    DictValue val;
    bool occupied = false;
    bool deleted = false;
};

struct DictTable {
    std::vector<DictEntry> table;
    size_t size = 0;
    size_t used = 0;      // active elements
    size_t deleted = 0;   // tombstone elements

    void init(size_t sz) {
        table.clear();
        table.resize(sz);
        size = sz;
        used = 0;
        deleted = 0;
    }
};

class Dict {
public:
    Dict(uint64_t hash_seed, int probe_cap);
    ~Dict() = default;

    // Insert or update. Returns true if inserted, false if updated.
    bool set(const std::string& key, DictValue val);
    
    // Get entry. Returns nullptr if not found.
    DictEntry* get(const std::string& key);

    // Delete entry. Returns true if deleted, false if not found.
    bool del(const std::string& key);

    // Get a random entry (for sampling)
    DictEntry* get_random_entry();

    size_t size() const { return ht[0].used + ht[1].used; }
    int get_max_probes() const { return max_probes; }


    void perform_rehash_step();

private:
    friend class DictIterator;

    uint64_t seed;
    int max_probes;
    DictTable ht[2];
    long long rehashidx; // -1 if not rehashing

    bool is_rehashing() const { return rehashidx != -1; }
    void expand_if_needed();
    DictEntry* find_free_slot(DictTable& t, const std::string& key, uint64_t hash);
    DictEntry* find_entry(DictTable& t, const std::string& key, uint64_t hash);
};
