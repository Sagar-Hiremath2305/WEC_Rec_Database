#include "cmd.h"
#include "db.h"
#include "utils.h"
#include <deque>
#include <string>
#include <iostream>

// Helper to get or create a list
std::deque<std::string>* get_or_create_list(const std::string& key, bool& created, std::string& error) {
    DictEntry* e = db_get(key);
    created = false;
    error = "";
    
    if (e) {
        if (e->val.type != ValType::LIST) {
            error = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
            return nullptr;
        }
        return static_cast<std::deque<std::string>*>(e->val.ptr);
    }
    
    // Create new list
    DictValue nv;
    nv.type = ValType::LIST;
    auto* list = new std::deque<std::string>();
    nv.ptr = list;
    g_state->db->set(key, std::move(nv));
    created = true;
    
    return static_cast<std::deque<std::string>*>(db_get(key)->val.ptr);
}

std::string execute_list_command(const std::vector<std::string>& cmd, const std::string& op) {
    if (op == "LPUSH" || op == "RPUSH") {
        if (cmd.size() < 3) return "-ERR wrong number of arguments\r\n";
        bool created;
        std::string err;
        auto* list = get_or_create_list(cmd[1], created, err);
        if (!list) return err;
        
        
        for (size_t i = 2; i < cmd.size(); i++) {
            if (op == "LPUSH") {
                list->push_front(cmd[i]);
            } else {
                list->push_back(cmd[i]);
            }
            
        }
        g_state->aof->append(cmd);
        return ":" + std::to_string(list->size()) + "\r\n";
        
    } else if (op == "LPOP" || op == "RPOP") {
        if (cmd.size() < 2 || cmd.size() > 3) return "-ERR wrong number of arguments\r\n";
        int count = 1;
        if (cmd.size() == 3) {
            try { count = std::stoi(cmd[2]); }
            catch (...) { return "-ERR value is not an integer or out of range\r\n"; }
            if (count < 0) return "-ERR value is out of range, must be positive\r\n";
        }
        
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "$-1\r\n";
        if (e->val.type != ValType::LIST) return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
        
        auto* list = static_cast<std::deque<std::string>*>(e->val.ptr);
        if (list->empty()) return "$-1\r\n"; // Should not happen as empty lists are deleted
        
        if (cmd.size() == 2) { // Single pop returns a bulk string
            std::string val;
            if (op == "LPOP") {
                val = list->front();
                list->pop_front();
            } else {
                val = list->back();
                list->pop_back();
            }
            if (list->empty()) g_state->db->del(cmd[1]);
            g_state->aof->append(cmd);
            return "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
        } else { // Count pop returns an array
            std::string reply = "";
            int actual_count = std::min(count, (int)list->size());
            reply += "*" + std::to_string(actual_count) + "\r\n";
            for (int i = 0; i < actual_count; i++) {
                std::string val;
                if (op == "LPOP") {
                    val = list->front();
                    list->pop_front();
                } else {
                    val = list->back();
                    list->pop_back();
                }
                reply += "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
            }
            if (list->empty()) g_state->db->del(cmd[1]);
            g_state->aof->append(cmd);
            return reply;
        }
        
    } else if (op == "LLEN") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return ":0\r\n";
        if (e->val.type != ValType::LIST) return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
        auto* list = static_cast<std::deque<std::string>*>(e->val.ptr);
        return ":" + std::to_string(list->size()) + "\r\n";
        
    } else if (op == "LRANGE") {
        if (cmd.size() != 4) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "*0\r\n";
        if (e->val.type != ValType::LIST) return "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
        
        long long start, stop;
        try {
            start = std::stoll(cmd[2]);
            stop = std::stoll(cmd[3]);
        } catch (...) {
            return "-ERR value is not an integer or out of range\r\n";
        }
        
        auto* list = static_cast<std::deque<std::string>*>(e->val.ptr);
        long long len = list->size();
        
        if (start < 0) start += len;
        if (stop < 0) stop += len;
        if (start < 0) start = 0;
        if (stop < 0) stop = 0;
        if (stop >= len) stop = len - 1;
        
        if (start > stop || start >= len) {
            return "*0\r\n";
        }
        
        long long count = stop - start + 1;
        std::string reply = "*" + std::to_string(count) + "\r\n";
        for (long long i = start; i <= stop; i++) {
            const std::string& val = (*list)[i];
            reply += "$" + std::to_string(val.size()) + "\r\n" + val + "\r\n";
        }
        return reply;
    }
    
    return "";
}
