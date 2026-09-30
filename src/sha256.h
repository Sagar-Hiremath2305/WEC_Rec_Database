#pragma once
#include <cstdint>
#include <string>

class SHA256 {
public:
    SHA256();
    void update(const uint8_t * data, size_t length);
    void update(const std::string &data);
    std::string digest();

private:
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
    void transform();
};
