#include "cmd.h"
#include "db.h"
#include "utils.h"
#include "dict_iter.h"
#include <iostream>
extern std::string execute_hash_set_command(const std::vector<std::string>& cmd, const std::string& op);
extern std::string execute_more_command(const std::vector<std::string>& cmd, const std::string& op);
extern std::string execute_zset_command(const std::vector<std::string>& cmd, const std::string& op);
extern std::string execute_list_command(const std::vector<std::string>& cmd, const std::string& op);
#include <algorithm>

std::string execute_command(const std::vector<std::string>& cmd) {
    if (cmd.empty()) return "";

    std::string op = cmd[0];
    for (char& c : op) c = std::toupper(c);

    // Call active expire and evict before processing (if we are a writing command, or just always)
    // Actually, active expire is normally called in the event loop, let's just do eviction here for writing commands
    // But for simplicity, we can do it at the start of command execution.
    db_active_expire();
    
    if (op == "PING") {
        if (cmd.size() > 1) {
            return "$" + std::to_string(cmd[1].size()) + "\r\n" + cmd[1] + "\r\n";
        }
        return "+PONG\r\n";
    } else if (op == "ECHO") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments for 'echo' command\r\n";
        return "$" + std::to_string(cmd[1].size()) + "\r\n" + cmd[1] + "\r\n";
    } else if (op == "SET") {
        if (cmd.size() < 3) return "-ERR wrong number of arguments for 'set' command\r\n";
        
        db_evict_if_needed(); // Evict before inserting
        
        DictValue val;
        val.type = ValType::STRING;
        val.ptr = new std::string(cmd[2]);
        val.lru = get_time_ms();
        
        long long expire_ms = -1;
        bool nx = false, xx = false;
        
        // Parse options (EX, PX, NX, XX)
        for (size_t i = 3; i < cmd.size(); i++) {
            std::string opt = cmd[i];
            for (char& c : opt) c = std::toupper(c);
            if (opt == "EX" && i + 1 < cmd.size()) {
                expire_ms = get_time_ms() + std::stoll(cmd[++i]) * 1000;
            } else if (opt == "PX" && i + 1 < cmd.size()) {
                expire_ms = get_time_ms() + std::stoll(cmd[++i]);
            } else if (opt == "NX") {
                nx = true;
            } else if (opt == "XX") {
                xx = true;
            } else {
                return "-ERR syntax error\r\n";
            }
        }
        
        val.expire_ms = expire_ms;
        
        DictEntry* existing = db_get(cmd[1]);
        if (nx && existing) return "$-1\r\n";
        if (xx && !existing) return "$-1\r\n";
        
        g_state->db->set(cmd[1], std::move(val));
        g_state->aof->append(cmd);
        return "+OK\r\n";
    } else if (op == "GET") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments for 'get' command\r\n";
        DictEntry* e = db_get(cmd[1]); // Passive expiry check
        if (!e) return "$-1\r\n";
        if (e->val.type != ValType::STRING) return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
        
        std::string* s = static_cast<std::string*>(e->val.ptr);
        return "$" + std::to_string(s->size()) + "\r\n" + *s + "\r\n";
    } else if (op == "DEL") {
        if (cmd.size() < 2) return "-ERR wrong number of arguments for 'del' command\r\n";
        int count = 0;
        for (size_t i = 1; i < cmd.size(); i++) {
            if (g_state->db->del(cmd[i])) count++;
        }
        if (count > 0) g_state->aof->append(cmd);
        return ":" + std::to_string(count) + "\r\n";
    } else if (op == "EXPIRE" || op == "PEXPIRE") {
        if (cmd.size() != 3) return "-ERR wrong number of arguments\r\n";
        long long ms = std::stoll(cmd[2]);
        if (op == "EXPIRE") ms *= 1000;
        
        // Required deviation
        if (op == "EXPIRE" && ms == 0) {
            return "-ERR invalid expire time\r\n";
        }
        
        DictEntry* e = db_get(cmd[1]);
        if (!e) return ":0\r\n";
        
        if (ms < 0) {
            g_state->db->del(cmd[1]);
            // Required deviation: PEXPIRE key -1 -> returns 2
            g_state->aof->append(cmd);
            if (op == "PEXPIRE" && ms == -1) return ":2\r\n";
            return ":1\r\n";
        }
        
        e->val.expire_ms = get_time_ms() + ms;
        g_state->aof->append(cmd);
        return ":1\r\n";
    } else if (op == "TTL" || op == "PTTL") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return ":-2\r\n";
        if (e->val.expire_ms == -1) return ":-1\r\n";
        long long rem = e->val.expire_ms - get_time_ms();
        if (rem < 0) rem = 0;
        if (op == "TTL") rem /= 1000;
        return ":" + std::to_string(rem) + "\r\n";
    } else if (op == "COMMAND") {
        return "+OK\r\n";
    } else if (op == "TYPE") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "-ERR missing key\r\n"; // Required deviation
        if (e->val.type == ValType::STRING) return "+string\r\n";
        return "+unknown\r\n";
    } else if (op == "KEYS") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        if (cmd[1] != "*") return "-ERR only KEYS * is supported for now\r\n";
        std::vector<std::string> keys;
        DictIterator it(g_state->db);
        while (DictEntry* e = it.next()) {
            if (e->val.expire_ms == -1 || e->val.expire_ms >= get_time_ms()) {
                keys.push_back(e->key);
            }
        }
        std::sort(keys.begin(), keys.end()); // Raw byte order
        std::string reply = "*" + std::to_string(keys.size()) + "\r\n";
        for (const auto& k : keys) {
            reply += "$" + std::to_string(k.size()) + "\r\n" + k + "\r\n";
        }
        return reply;
    } else if (op == "INCR" || op == "INCRBY") {
        if (cmd.size() < 2 || (op == "INCRBY" && cmd.size() < 3)) return "-ERR wrong number of arguments\r\n";
        long long delta = 1;
        if (op == "INCRBY") {
            try { delta = std::stoll(cmd[2]); }
            catch (...) { return "-ERR value is not an integer or out of range\r\n"; }
        }
        DictEntry* e = db_get(cmd[1]);
        long long val = 0;
        if (e) {
            if (e->val.type != ValType::STRING) return "-WRONGTYPE\r\n";
            std::string* s = static_cast<std::string*>(e->val.ptr);
            if (s->size() > 1 && (*s)[0] == '0') return "-ERR value is not an integer or out of range\r\n"; 
            try { val = std::stoll(*s); }
            catch (...) { return "-ERR value is not an integer or out of range\r\n"; }
        } else {
            DictValue nv;
            nv.type = ValType::STRING;
            nv.ptr = new std::string("");
            g_state->db->set(cmd[1], std::move(nv));
            e = db_get(cmd[1]);
        }
        val += delta;
        std::string new_str = std::to_string(val);
        delete static_cast<std::string*>(e->val.ptr);
        e->val.ptr = new std::string(new_str);
        g_state->aof->append(cmd);
        return ":" + new_str + "\r\n";
    } else if (op == "DEBUG" && cmd.size() == 2 && cmd[1] == "DIGEST") {
        extern std::string compute_debug_digest();
        return "+" + compute_debug_digest() + "\r\n";
    }
    
    std::string hs_reply = execute_hash_set_command(cmd, op);
    if (!hs_reply.empty()) return hs_reply;

    std::string list_reply = execute_list_command(cmd, op);
    if (!list_reply.empty()) return list_reply;

    std::string zs_reply = execute_zset_command(cmd, op);
    if (!zs_reply.empty()) return zs_reply;

    std::string more_reply = execute_more_command(cmd, op);
    if (!more_reply.empty()) return more_reply;

    return "-ERR unknown command '" + cmd[0] + "'\r\n";
}
