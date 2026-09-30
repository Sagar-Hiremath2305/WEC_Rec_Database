#pragma once
#include <cstdint>
#include <string>
#include <cstddef>

uint64_t siphash(const uint8_t *in, const size_t inlen, uint64_t seed);
uint64_t dict_hash(const std::string& key, uint64_t seed);
