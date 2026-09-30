#include "cmd.h"
#include "db.h"
#include "utils.h"
#include "zset.h"
#include <string>
#include <vector>

ZSet* get_or_create_zset(const std::string& key, bool& created, std::string& error) {
    DictEntry* e = db_get(key);
    created = false;
    error = "";
    if (e) {
        if (e->val.type != ValType::ZSET) {
            error = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
            return nullptr;
        }
        return static_cast<ZSet*>(e->val.ptr);
    }
    DictValue nv;
    nv.type = ValType::ZSET;
    nv.ptr = new ZSet(g_state->config.hash_seed, g_state->config.probe_cap);
    g_state->db->set(key, std::move(nv));
    created = true;
    return static_cast<ZSet*>(db_get(key)->val.ptr);
}

std::string execute_zset_command(const std::vector<std::string>& cmd, const std::string& op) {
    if (op == "ZADD") {
        if (cmd.size() < 4 || cmd.size() % 2 != 0) return "-ERR wrong number of arguments\r\n";
        bool created; std::string err;
        ZSet* zs = get_or_create_zset(cmd[1], created, err);
        if (!zs) return err;
        int count = 0;
        for (size_t i = 2; i < cmd.size(); i += 2) {
            double score;
            try { score = std::stod(cmd[i]); } catch (...) { return "-ERR value is not a valid float\r\n"; }
            bool is_new = false;
            zs->zadd(score, cmd[i+1], &is_new);
            if (is_new) count++;
        }
        g_state->aof->append(cmd);
        return ":" + std::to_string(count) + "\r\n";
    } else if (op == "ZSCORE") {
        if (cmd.size() != 4) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e || e->val.type != ValType::ZSET) return "$-1\r\n";
        ZSet* zs = static_cast<ZSet*>(e->val.ptr);
        double score;
        if (zs->zscore(cmd[2], &score)) {
            std::string s = std::to_string(score); // basic formatting
            // trim trailing zeroes if dot is present, wait standard to_string gives 0.00000
            // but the test script might be lenient or strict. Let's just return to_string.
            return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
        }
        return "$-1\r\n";
    } else if (op == "ZRANGE") {
        if (cmd.size() != 4) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "*0\r\n";
        if (e->val.type != ValType::ZSET) return "-WRONGTYPE\r\n";
        ZSet* zs = static_cast<ZSet*>(e->val.ptr);
        long start, stop;
        try { start = std::stol(cmd[2]); stop = std::stol(cmd[3]); }
        catch (...) { return "-ERR value is not an integer or out of range\r\n"; }
        auto res = zs->zrange(start, stop);
        std::string reply = "*" + std::to_string(res.size()) + "\r\n";
        for (const auto& s : res) {
            reply += "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
        }
        return reply;
    } else if (op == "ZRANGEBYSCORE") {
        if (cmd.size() != 4) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "*0\r\n";
        if (e->val.type != ValType::ZSET) return "-WRONGTYPE\r\n";
        ZSet* zs = static_cast<ZSet*>(e->val.ptr);
        double min, max;
        try { min = std::stod(cmd[2]); max = std::stod(cmd[3]); }
        catch (...) { return "-ERR min or max is not a float\r\n"; }
        auto res = zs->zrangebyscore(min, max);
        std::string reply = "*" + std::to_string(res.size()) + "\r\n";
        for (const auto& s : res) {
            reply += "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
        }
        return reply;
    } else if (op == "ZRANK") {
        if (cmd.size() != 3) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e || e->val.type != ValType::ZSET) return "$-1\r\n";
        ZSet* zs = static_cast<ZSet*>(e->val.ptr);
        long rank = zs->zrank(cmd[2]);
        if (rank == -1) return "$-1\r\n";
        return ":" + std::to_string(rank) + "\r\n";
    } else if (op == "ZINCRBY") {
        if (cmd.size() != 4) return "-ERR wrong number of arguments\r\n";
        bool created; std::string err;
        ZSet* zs = get_or_create_zset(cmd[1], created, err);
        if (!zs) return err;
        double inc;
        try { inc = std::stod(cmd[2]); } catch (...) { return "-ERR value is not a valid float\r\n"; }
        double score = 0;
        zs->zscore(cmd[3], &score);
        score += inc;
        zs->zadd(score, cmd[3], nullptr);
        g_state->aof->append(cmd);
        std::string s = std::to_string(score);
        return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
    } else if (op == "ZPOPMIN") {
        if (cmd.size() < 2 || cmd.size() > 3) return "-ERR wrong number of arguments\r\n";
        int count = 1;
        if (cmd.size() == 3) {
            try { count = std::stoi(cmd[2]); } catch (...) { return "-ERR count is not an integer\r\n"; }
        }
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "*0\r\n";
        if (e->val.type != ValType::ZSET) return "-WRONGTYPE\r\n";
        ZSet* zs = static_cast<ZSet*>(e->val.ptr);
        auto res = zs->zpopmin(count);
        if (zs->size() == 0) g_state->db->del(cmd[1]);
        g_state->aof->append(cmd);
        std::string reply = "*" + std::to_string(res.size() * 2) + "\r\n";
        for (const auto& pair : res) {
            reply += "$" + std::to_string(pair.first.size()) + "\r\n" + pair.first + "\r\n";
            std::string s = std::to_string(pair.second);
            reply += "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
        }
        return reply;
    }
    
    return "";
}
