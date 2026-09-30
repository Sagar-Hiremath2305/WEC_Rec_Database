#include "cmd.h"
#include "db.h"
#include "utils.h"
#include "dict_iter.h"
#include "sha256.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <iostream>

void write_uint32_le(std::string& out, uint32_t val) {
    out.push_back((char)(val & 0xFF));
    out.push_back((char)((val >> 8) & 0xFF));
    out.push_back((char)((val >> 16) & 0xFF));
    out.push_back((char)((val >> 24) & 0xFF));
}

std::string compute_debug_digest() {
    std::vector<std::pair<std::string, DictEntry*>> entries;
    DictIterator it(g_state->db);
    while (DictEntry* e = it.next()) {
        if (e->val.expire_ms == -1 || e->val.expire_ms >= get_time_ms()) {
            entries.push_back({e->key, e});
        }
    }
    
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    
    SHA256 sha;
    for (const auto& pair : entries) {
        std::string serialized;
        write_uint32_le(serialized, pair.first.size());
        serialized += pair.first;
        
        serialized.push_back(pair.second->val.expire_ms != -1 ? 1 : 0);
        
        if (pair.second->val.type == ValType::STRING) {
            std::string* s = static_cast<std::string*>(pair.second->val.ptr);
            write_uint32_le(serialized, s->size());
            serialized += *s;
        } else if (pair.second->val.type == ValType::LIST) {
            auto* list = static_cast<std::deque<std::string>*>(pair.second->val.ptr);
            for (const auto& item : *list) {
                write_uint32_le(serialized, item.size());
                serialized += item;
            }
        } else if (pair.second->val.type == ValType::HASH) {
            Dict* h = static_cast<Dict*>(pair.second->val.ptr);
            std::vector<std::pair<std::string, std::string>> h_entries;
            DictIterator hit(h);
            while (DictEntry* fe = hit.next()) {
                h_entries.push_back({fe->key, *static_cast<std::string*>(fe->val.ptr)});
            }
            std::sort(h_entries.begin(), h_entries.end());
            for (const auto& he : h_entries) {
                write_uint32_le(serialized, he.first.size());
                serialized += he.first;
                write_uint32_le(serialized, he.second.size());
                serialized += he.second;
            }
        } else if (pair.second->val.type == ValType::SET) {
            Dict* s = static_cast<Dict*>(pair.second->val.ptr);
            std::vector<std::string> s_entries;
            DictIterator sit(s);
            while (DictEntry* fe = sit.next()) {
                s_entries.push_back(fe->key);
            }
            std::sort(s_entries.begin(), s_entries.end());
            for (const auto& se : s_entries) {
                write_uint32_le(serialized, se.size());
                serialized += se;
            }
        }
        
        sha.update(serialized);
    }
    
    return sha.digest();
}
