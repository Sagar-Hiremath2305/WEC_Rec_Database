#pragma once
#include "dict.h"

class DictIterator {
public:
    DictIterator(Dict* dict) : d(dict), table_idx(0), slot_idx(-1) {}
    
    DictEntry* next() {
        while (true) {
            slot_idx++;
            if (slot_idx >= (long long)d->ht[table_idx].size) {
                if (table_idx == 0 && d->is_rehashing()) {
                    table_idx = 1;
                    slot_idx = 0;
                    if (d->ht[1].size == 0) return nullptr;
                } else {
                    return nullptr;
                }
            }
            DictEntry* e = &d->ht[table_idx].table[slot_idx];
            if (e->occupied && !e->deleted) {
                return e;
            }
        }
    }

private:
    Dict* d;
    int table_idx;
    long long slot_idx;
};
