#include "cmd.h"
#include "db.h"
#include "utils.h"
#include "dict_iter.h"
#include <string>
#include <vector>

std::string execute_more_command(const std::vector<std::string>& cmd, const std::string& op) {
    if (op == "GETDEL") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "$-1\r\n";
        if (e->val.type != ValType::STRING) return "-WRONGTYPE\r\n";
        std::string* s = static_cast<std::string*>(e->val.ptr);
        std::string reply = "$" + std::to_string(s->size()) + "\r\n" + *s + "\r\n";
        g_state->db->del(cmd[1]);
        g_state->aof->append(cmd);
        return reply;
    } else if (op == "APPEND") {
        if (cmd.size() != 3) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) {
            DictValue nv; nv.type = ValType::STRING; nv.ptr = new std::string(cmd[2]);
            g_state->db->set(cmd[1], std::move(nv));
            g_state->aof->append(cmd);
            return ":" + std::to_string(cmd[2].size()) + "\r\n";
        }
        if (e->val.type != ValType::STRING) return "-WRONGTYPE\r\n";
        std::string* s = static_cast<std::string*>(e->val.ptr);
        s->append(cmd[2]);
        g_state->aof->append(cmd);
        return ":" + std::to_string(s->size()) + "\r\n";
    } else if (op == "STRLEN") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return ":0\r\n";
        if (e->val.type != ValType::STRING) return "-WRONGTYPE\r\n";
        std::string* s = static_cast<std::string*>(e->val.ptr);
        return ":" + std::to_string(s->size()) + "\r\n";
    } else if (op == "MSET") {
        if (cmd.size() < 3 || cmd.size() % 2 == 0) return "-ERR wrong number of arguments\r\n";
        for (size_t i = 1; i < cmd.size(); i += 2) {
            DictValue nv; nv.type = ValType::STRING; nv.ptr = new std::string(cmd[i+1]);
            g_state->db->set(cmd[i], std::move(nv));
        }
        g_state->aof->append(cmd);
        return "+OK\r\n";
    } else if (op == "MGET") {
        if (cmd.size() < 2) return "-ERR wrong number of arguments\r\n";
        std::string reply = "*" + std::to_string(cmd.size() - 1) + "\r\n";
        for (size_t i = 1; i < cmd.size(); i++) {
            DictEntry* e = db_get(cmd[i]);
            if (!e || e->val.type != ValType::STRING) {
                reply += "$-1\r\n";
            } else {
                std::string* s = static_cast<std::string*>(e->val.ptr);
                reply += "$" + std::to_string(s->size()) + "\r\n" + *s + "\r\n";
            }
        }
        return reply;
    } else if (op == "EXISTS") {
        if (cmd.size() < 2) return "-ERR wrong number of arguments\r\n";
        int count = 0;
        for (size_t i = 1; i < cmd.size(); i++) {
            if (db_get(cmd[i])) count++;
        }
        return ":" + std::to_string(count) + "\r\n";
    } else if (op == "PERSIST") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e || e->val.expire_ms == -1) return ":0\r\n";
        e->val.expire_ms = -1;
        g_state->aof->append(cmd);
        return ":1\r\n";
    } else if (op == "DBSIZE") {
        if (cmd.size() != 1) return "-ERR wrong number of arguments\r\n";
        return ":" + std::to_string(g_state->db->size()) + "\r\n";
    } else if (op == "FLUSHDB") {
        if (cmd.size() > 2) return "-ERR syntax error\r\n";
        if (cmd.size() == 2 && cmd[1] == "ASYNC") {
            return "-ERR FLUSHDB ASYNC unsupported\r\n"; // Required deviation
        }
        // Need to delete all keys
        DictIterator it(g_state->db);
        std::vector<std::string> keys;
        while (DictEntry* e = it.next()) keys.push_back(e->key);
        for (const auto& k : keys) g_state->db->del(k);
        g_state->aof->append(cmd);
        return "+OK\r\n";
    } else if (op == "RENAME") {
        if (cmd.size() != 3) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "-ERR no such key\r\n";
        // To be safe, steal the value, delete, insert
        // DictValue copied = e->val; // wait, if I do this I will double delete. 
        // Need to take ownership properly. But wait, `DictValue` copies `ptr`, so double delete if both keys have it!
        // `Dict::del` deletes the value. So I must NOT just copy it.
        // Actually, I can construct a new string/deque/hash, but that's O(N).
        return "-ERR rename not fully implemented yet\r\n";
    } else if (op == "SCAN") {
        return "-ERR scan not implemented yet\r\n";
    } else if (op == "INFO") {
        return "$0\r\n\r\n"; // Dummy for now
    } else if (op == "CONFIG") {
        return "+OK\r\n"; // Dummy
    } else if (op == "DEBUG" && cmd.size() == 2 && cmd[1] == "PROBESTAT") {
        return ":" + std::to_string(g_state->db->get_max_probes()) + "\r\n";
    }
    
    return "";
}
