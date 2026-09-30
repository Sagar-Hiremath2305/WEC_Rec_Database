#include "cmd.h"
#include "db.h"
#include "utils.h"
#include "dict_iter.h"
#include <string>
#include <vector>

// Helper to get or create a Hash (Dict)
Dict* get_or_create_hash(const std::string& key, bool& created, std::string& error) {
    DictEntry* e = db_get(key);
    created = false;
    error = "";
    if (e) {
        if (e->val.type != ValType::HASH) {
            error = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
            return nullptr;
        }
        return static_cast<Dict*>(e->val.ptr);
    }
    DictValue nv;
    nv.type = ValType::HASH;
    nv.ptr = new Dict(g_state->config.hash_seed, g_state->config.probe_cap);
    g_state->db->set(key, std::move(nv));
    created = true;
    return static_cast<Dict*>(db_get(key)->val.ptr);
}

// Helper to get or create a Set (Dict)
Dict* get_or_create_set(const std::string& key, bool& created, std::string& error) {
    DictEntry* e = db_get(key);
    created = false;
    error = "";
    if (e) {
        if (e->val.type != ValType::SET) {
            error = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
            return nullptr;
        }
        return static_cast<Dict*>(e->val.ptr);
    }
    DictValue nv;
    nv.type = ValType::SET;
    nv.ptr = new Dict(g_state->config.hash_seed, g_state->config.probe_cap);
    g_state->db->set(key, std::move(nv));
    created = true;
    return static_cast<Dict*>(db_get(key)->val.ptr);
}

std::string execute_hash_set_command(const std::vector<std::string>& cmd, const std::string& op) {
    // Hash Commands
    if (op == "HSET") {
        if (cmd.size() < 4 || cmd.size() % 2 != 0) return "-ERR wrong number of arguments\r\n";
        bool created; std::string err;
        Dict* h = get_or_create_hash(cmd[1], created, err);
        if (!h) return err;
        int count = 0;
        for (size_t i = 2; i < cmd.size(); i += 2) {
            DictValue fv;
            fv.type = ValType::STRING;
            fv.ptr = new std::string(cmd[i+1]);
            if (h->set(cmd[i], std::move(fv))) count++;
        }
        g_state->aof->append(cmd);
        return ":" + std::to_string(count) + "\r\n";
    } else if (op == "HGET") {
        if (cmd.size() != 4) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "$-1\r\n";
        if (e->val.type != ValType::HASH) return "-WRONGTYPE\r\n";
        Dict* h = static_cast<Dict*>(e->val.ptr);
        DictEntry* fe = h->get(cmd[2]);
        if (!fe || fe->deleted) return "$-1\r\n";
        std::string* s = static_cast<std::string*>(fe->val.ptr);
        return "$" + std::to_string(s->size()) + "\r\n" + *s + "\r\n";
    } else if (op == "HDEL") {
        if (cmd.size() < 3) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return ":0\r\n";
        if (e->val.type != ValType::HASH) return "-WRONGTYPE\r\n";
        Dict* h = static_cast<Dict*>(e->val.ptr);
        int count = 0;
        for (size_t i = 2; i < cmd.size(); i++) {
            if (h->del(cmd[i])) count++;
        }
        if (h->size() == 0) g_state->db->del(cmd[1]);
        if (count > 0) g_state->aof->append(cmd);
        return ":" + std::to_string(count) + "\r\n";
    } else if (op == "HGETALL") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "*0\r\n";
        if (e->val.type != ValType::HASH) return "-WRONGTYPE\r\n";
        Dict* h = static_cast<Dict*>(e->val.ptr);
        std::string reply = "*" + std::to_string(h->size() * 2) + "\r\n";
        DictIterator it(h);
        while (DictEntry* fe = it.next()) {
            reply += "$" + std::to_string(fe->key.size()) + "\r\n" + fe->key + "\r\n";
            std::string* s = static_cast<std::string*>(fe->val.ptr);
            reply += "$" + std::to_string(s->size()) + "\r\n" + *s + "\r\n";
        }
        return reply;
    } else if (op == "HINCRBY") {
        if (cmd.size() != 4) return "-ERR wrong number of arguments\r\n";
        bool created; std::string err;
        Dict* h = get_or_create_hash(cmd[1], created, err);
        if (!h) return err;
        long long delta;
        try { delta = std::stoll(cmd[3]); } catch (...) { return "-ERR value is not an integer\r\n"; }
        DictEntry* fe = h->get(cmd[2]);
        long long val = 0;
        if (fe && !fe->deleted) {
            std::string* s = static_cast<std::string*>(fe->val.ptr);
            try { val = std::stoll(*s); } catch (...) { return "-ERR hash value is not an integer\r\n"; }
        }
        val += delta;
        std::string new_str = std::to_string(val);
        DictValue fv; fv.type = ValType::STRING; fv.ptr = new std::string(new_str);
        h->set(cmd[2], std::move(fv));
        g_state->aof->append(cmd);
        return ":" + new_str + "\r\n";
    }
    
    // Set Commands
    else if (op == "SADD") {
        if (cmd.size() < 3) return "-ERR wrong number of arguments\r\n";
        bool created; std::string err;
        Dict* s = get_or_create_set(cmd[1], created, err);
        if (!s) return err;
        int count = 0;
        for (size_t i = 2; i < cmd.size(); i++) {
            DictValue fv; fv.type = ValType::STRING; // Dummy value
            if (s->set(cmd[i], std::move(fv))) count++;
        }
        g_state->aof->append(cmd);
        return ":" + std::to_string(count) + "\r\n";
    } else if (op == "SREM") {
        if (cmd.size() < 3) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return ":0\r\n";
        if (e->val.type != ValType::SET) return "-WRONGTYPE\r\n";
        Dict* s = static_cast<Dict*>(e->val.ptr);
        int count = 0;
        for (size_t i = 2; i < cmd.size(); i++) {
            if (s->del(cmd[i])) count++;
        }
        if (s->size() == 0) g_state->db->del(cmd[1]);
        if (count > 0) g_state->aof->append(cmd);
        return ":" + std::to_string(count) + "\r\n";
    } else if (op == "SMEMBERS") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return "*0\r\n";
        if (e->val.type != ValType::SET) return "-WRONGTYPE\r\n";
        Dict* s = static_cast<Dict*>(e->val.ptr);
        std::string reply = "*" + std::to_string(s->size()) + "\r\n";
        DictIterator it(s);
        while (DictEntry* fe = it.next()) {
            reply += "$" + std::to_string(fe->key.size()) + "\r\n" + fe->key + "\r\n";
        }
        return reply;
    } else if (op == "SISMEMBER") {
        if (cmd.size() != 3) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return ":0\r\n";
        if (e->val.type != ValType::SET) return "-WRONGTYPE\r\n";
        Dict* s = static_cast<Dict*>(e->val.ptr);
        DictEntry* fe = s->get(cmd[2]);
        if (fe && !fe->deleted) return ":1\r\n";
        return ":0\r\n";
    } else if (op == "SCARD") {
        if (cmd.size() != 2) return "-ERR wrong number of arguments\r\n";
        DictEntry* e = db_get(cmd[1]);
        if (!e) return ":0\r\n";
        if (e->val.type != ValType::SET) return "-WRONGTYPE\r\n";
        Dict* s = static_cast<Dict*>(e->val.ptr);
        return ":" + std::to_string(s->size()) + "\r\n";
    }

    return "";
}
