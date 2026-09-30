#include "../src/resp.h"
#include <iostream>
#include <cassert>

void test_feed_char() {
    RespParser p;
    std::string cmd = "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nalice\r\n";
    bool ready = false;
    for (char c : cmd) {
        ready = p.feed(c);
    }
    assert(ready);
    auto res = p.get_command();
    assert(res.size() == 3);
    assert(res[0] == "SET");
    assert(res[1] == "name");
    assert(res[2] == "alice");
    std::cout << "test_feed_char passed\n";
}

void test_inline() {
    RespParser p;
    std::string cmd = "SET name alice\r\n";
    bool ready = false;
    for (char c : cmd) {
        ready = p.feed(c);
    }
    assert(ready);
    auto res = p.get_command();
    assert(res.size() == 3);
    assert(res[0] == "SET");
    assert(res[1] == "name");
    assert(res[2] == "alice");
    std::cout << "test_inline passed\n";
}

int main() {
    test_feed_char();
    test_inline();
    return 0;
}
