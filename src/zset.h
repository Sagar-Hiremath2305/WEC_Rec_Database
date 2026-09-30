#pragma once
#include "dict.h"
#include <string>
#include <vector>

#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

struct ZSkipListNode {
    std::string ele;
    double score;
    struct ZSkipListLevel {
        ZSkipListNode* forward;
        unsigned long span;
    } level[];
    
    // Dynamic allocation helper
    static ZSkipListNode* create(int level, double score, const std::string& ele);
    static void destroy(ZSkipListNode* node);
};

class ZSet {
public:
    ZSet(uint64_t hash_seed, int probe_cap);
    ~ZSet();

    int zadd(double score, const std::string& ele, bool* is_new);
    bool zscore(const std::string& ele, double* score);
    bool zrem(const std::string& ele);
    
    std::vector<std::string> zrange(long start, long stop);
    std::vector<std::string> zrangebyscore(double min, double max);
    long zrank(const std::string& ele);
    std::vector<std::pair<std::string, double>> zpopmin(int count);
    
    size_t size() const { return length; }

private:
    Dict* dict;
    
    ZSkipListNode* header;
    ZSkipListNode* tail;
    unsigned long length;
    int level;
    
    int randomLevel();
    ZSkipListNode* insertNode(double score, const std::string& ele);
    void deleteNode(ZSkipListNode* x, ZSkipListNode** update);
};
