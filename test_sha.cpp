#include "src/sha256.h"
#include <iostream>

int main() {
    SHA256 sha;
    sha.update("hello world");
    std::cout << sha.digest() << std::endl;
    return 0;
}
