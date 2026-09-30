#include "zset.h"
#include <cstdlib>
#include <cmath>

ZSkipListNode* ZSkipListNode::create(int level, double score, const std::string& ele) {
    void* raw = malloc(sizeof(ZSkipListNode) + level * sizeof(ZSkipListLevel));
    ZSkipListNode* node = new (raw) ZSkipListNode();
    node->score = score;
    new (&node->ele) std::string(ele);
    return node;
}

void ZSkipListNode::destroy(ZSkipListNode* node) {
    node->ele.~basic_string();
    free(node);
}

ZSet::ZSet(uint64_t hash_seed, int probe_cap) {
    dict = new Dict(hash_seed, probe_cap);
    level = 1;
    length = 0;
    header = ZSkipListNode::create(ZSKIPLIST_MAXLEVEL, 0.0, "");
    for (int j = 0; j < ZSKIPLIST_MAXLEVEL; j++) {
        header->level[j].forward = nullptr;
        header->level[j].span = 0;
    }
    tail = nullptr;
}

ZSet::~ZSet() {
    ZSkipListNode* node = header->level[0].forward;
    while (node) {
        ZSkipListNode* next = node->level[0].forward;
        ZSkipListNode::destroy(node);
        node = next;
    }
    ZSkipListNode::destroy(header);
    delete dict;
}

int ZSet::randomLevel() {
    int lvl = 1;
    while ((rand() & 0xFFFF) < (ZSKIPLIST_P * 0xFFFF))
        lvl += 1;
    return (lvl < ZSKIPLIST_MAXLEVEL) ? lvl : ZSKIPLIST_MAXLEVEL;
}

int ZSet::zadd(double score, const std::string& ele, bool* is_new) {
    DictEntry* de = dict->get(ele);
    if (de && !de->deleted) {
        double* old_score = static_cast<double*>(de->val.ptr);
        if (*old_score == score) {
            if (is_new) *is_new = false;
            return 0; // nothing changed
        }
        // Remove old score from skiplist
        zrem(ele);
    }
    
    // Insert into dict
    DictValue dv;
    dv.type = ValType::STRING; // We repurpose ptr to hold a double
    dv.ptr = new double(score);
    dict->set(ele, std::move(dv));
    
    insertNode(score, ele);
    if (is_new) *is_new = true;
    return 1;
}

ZSkipListNode* ZSet::insertNode(double score, const std::string& ele) {
    ZSkipListNode* update[ZSKIPLIST_MAXLEVEL];
    unsigned long rank[ZSKIPLIST_MAXLEVEL];
    ZSkipListNode* x = header;
    
    for (int i = level - 1; i >= 0; i--) {
        rank[i] = i == (level - 1) ? 0 : rank[i+1];
        while (x->level[i].forward &&
               (x->level[i].forward->score < score ||
               (x->level[i].forward->score == score && x->level[i].forward->ele < ele))) {
            rank[i] += x->level[i].span;
            x = x->level[i].forward;
        }
        update[i] = x;
    }
    
    int lvl = randomLevel();
    if (lvl > level) {
        for (int i = level; i < lvl; i++) {
            rank[i] = 0;
            update[i] = header;
            update[i]->level[i].span = length;
        }
        level = lvl;
    }
    
    x = ZSkipListNode::create(lvl, score, ele);
    for (int i = 0; i < lvl; i++) {
        x->level[i].forward = update[i]->level[i].forward;
        update[i]->level[i].forward = x;
        
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[i]);
        update[i]->level[i].span = (rank[0] - rank[i]) + 1;
    }
    
    for (int i = lvl; i < level; i++) {
        update[i]->level[i].span++;
    }
    
    length++;
    return x;
}

void ZSet::deleteNode(ZSkipListNode* x, ZSkipListNode** update) {
    for (int i = 0; i < level; i++) {
        if (update[i]->level[i].forward == x) {
            update[i]->level[i].span += x->level[i].span - 1;
            update[i]->level[i].forward = x->level[i].forward;
        } else {
            update[i]->level[i].span -= 1;
        }
    }
    while (level > 1 && header->level[level-1].forward == nullptr)
        level--;
    length--;
}

bool ZSet::zrem(const std::string& ele) {
    DictEntry* de = dict->get(ele);
    if (!de || de->deleted) return false;
    double score = *static_cast<double*>(de->val.ptr);
    dict->del(ele);
    
    ZSkipListNode* update[ZSKIPLIST_MAXLEVEL];
    ZSkipListNode* x = header;
    for (int i = level - 1; i >= 0; i--) {
        while (x->level[i].forward &&
               (x->level[i].forward->score < score ||
               (x->level[i].forward->score == score && x->level[i].forward->ele < ele))) {
            x = x->level[i].forward;
        }
        update[i] = x;
    }
    x = x->level[0].forward;
    if (x && score == x->score && x->ele == ele) {
        deleteNode(x, update);
        ZSkipListNode::destroy(x);
        return true;
    }
    return false;
}

bool ZSet::zscore(const std::string& ele, double* score) {
    DictEntry* de = dict->get(ele);
    if (!de || de->deleted) return false;
    if (score) *score = *static_cast<double*>(de->val.ptr);
    return true;
}

long ZSet::zrank(const std::string& ele) {
    DictEntry* de = dict->get(ele);
    if (!de || de->deleted) return -1;
    double score = *static_cast<double*>(de->val.ptr);
    
    unsigned long rank = 0;
    ZSkipListNode* x = header;
    for (int i = level - 1; i >= 0; i--) {
        while (x->level[i].forward &&
               (x->level[i].forward->score < score ||
               (x->level[i].forward->score == score && x->level[i].forward->ele <= ele))) {
            rank += x->level[i].span;
            x = x->level[i].forward;
        }
    }
    if (x && x->ele == ele) return rank - 1;
    return -1;
}

std::vector<std::string> ZSet::zrange(long start, long stop) {
    std::vector<std::string> res;
    long llen = length;
    if (start < 0) start = llen + start;
    if (stop < 0) stop = llen + stop;
    if (start < 0) start = 0;
    
    if (start > stop || start >= llen) return res;
    if (stop >= llen) stop = llen - 1;
    
    unsigned long traversed = 0;
    ZSkipListNode* x = header;
    for (int i = level - 1; i >= 0; i--) {
        while (x->level[i].forward && (traversed + x->level[i].span) <= (unsigned long)(start)) {
            traversed += x->level[i].span;
            x = x->level[i].forward;
        }
    }
    x = x->level[0].forward;
    long count = stop - start + 1;
    while (x && count--) {
        res.push_back(x->ele);
        x = x->level[0].forward;
    }
    return res;
}

std::vector<std::string> ZSet::zrangebyscore(double min, double max) {
    std::vector<std::string> res;
    ZSkipListNode* x = header;
    for (int i = level - 1; i >= 0; i--) {
        while (x->level[i].forward && x->level[i].forward->score < min)
            x = x->level[i].forward;
    }
    x = x->level[0].forward;
    while (x && x->score <= max) {
        res.push_back(x->ele);
        x = x->level[0].forward;
    }
    return res;
}

std::vector<std::pair<std::string, double>> ZSet::zpopmin(int count) {
    std::vector<std::pair<std::string, double>> res;
    while (count-- > 0 && length > 0) {
        ZSkipListNode* x = header->level[0].forward;
        std::string ele = x->ele;
        double score = x->score;
        res.push_back({ele, score});
        zrem(ele); // This properly handles the skiplist and dictionary
    }
    return res;
}
