#include "../src/dict.h"
#include <iostream>
#include <cassert>

int main() {
    Dict d(0x12345678, 16);
    
    // Insert
    for (int i = 0; i < 1000; i++) {
        DictValue v;
        v.type = ValType::STRING;
        v.ptr = new std::string("val" + std::to_string(i));
        d.set("key" + std::to_string(i), std::move(v));
    }
    
    assert(d.size() == 1000);
    
    // Retrieve
    for (int i = 0; i < 1000; i++) {
        DictEntry* e = d.get("key" + std::to_string(i));
        assert(e != nullptr);
        assert(*static_cast<std::string*>(e->val.ptr) == "val" + std::to_string(i));
    }
    
    // Delete
    for (int i = 0; i < 500; i++) {
        assert(d.del("key" + std::to_string(i)));
    }
    
    assert(d.size() == 500);
    
    // Retrieve again
    for (int i = 0; i < 500; i++) {
        DictEntry* e = d.get("key" + std::to_string(i));
        assert(e == nullptr);
    }
    for (int i = 500; i < 1000; i++) {
        DictEntry* e = d.get("key" + std::to_string(i));
        assert(e != nullptr);
    }
    
    std::cout << "test_dict passed\n";
    return 0;
}
