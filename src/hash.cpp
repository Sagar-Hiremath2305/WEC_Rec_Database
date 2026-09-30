#include "hash.h"

// Simple SipHash-2-4 implementation
#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define U32TO8_LE(p, v) \
    (p)[0] = (uint8_t)((v)      ); (p)[1] = (uint8_t)((v) >>  8); \
    (p)[2] = (uint8_t)((v) >> 16); (p)[3] = (uint8_t)((v) >> 24);

#define U64TO8_LE(p, v) \
    U32TO8_LE((p),     (uint32_t)((v)      )); \
    U32TO8_LE((p) + 4, (uint32_t)((v) >> 32));

#define U8TO64_LE(p) \
    (((uint64_t)((p)[0])      ) | \
     ((uint64_t)((p)[1]) <<  8) | \
     ((uint64_t)((p)[2]) << 16) | \
     ((uint64_t)((p)[3]) << 24) | \
     ((uint64_t)((p)[4]) << 32) | \
     ((uint64_t)((p)[5]) << 40) | \
     ((uint64_t)((p)[6]) << 48) | \
     ((uint64_t)((p)[7]) << 56))

#define SIPROUND \
    do { \
        v0 += v1; v1 = ROTL(v1, 13); v1 ^= v0; v0 = ROTL(v0, 32); \
        v2 += v3; v3 = ROTL(v3, 16); v3 ^= v2; \
        v0 += v3; v3 = ROTL(v3, 21); v3 ^= v0; \
        v2 += v1; v1 = ROTL(v1, 17); v1 ^= v2; v2 = ROTL(v2, 32); \
    } while (0)

uint64_t siphash(const uint8_t *in, const size_t inlen, uint64_t seed) {
    uint64_t k0 = seed;
    uint64_t k1 = seed ^ 0x0123456789ABCDEF; // Just some mixing if we only have 1 64-bit seed
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;
    uint64_t b = ((uint64_t)inlen) << 56;
    const uint8_t *end = in + inlen - (inlen % 8);
    const int left = inlen & 7;
    for (; in != end; in += 8) {
        uint64_t m = U8TO64_LE(in);
        v3 ^= m;
        SIPROUND; SIPROUND;
        v0 ^= m;
    }
    switch (left) {
    case 7: b |= ((uint64_t)in[6]) << 48; // fallthrough
    case 6: b |= ((uint64_t)in[5]) << 40; // fallthrough
    case 5: b |= ((uint64_t)in[4]) << 32; // fallthrough
    case 4: b |= ((uint64_t)in[3]) << 24; // fallthrough
    case 3: b |= ((uint64_t)in[2]) << 16; // fallthrough
    case 2: b |= ((uint64_t)in[1]) << 8;  // fallthrough
    case 1: b |= ((uint64_t)in[0]); break;
    case 0: break;
    }
    v3 ^= b;
    SIPROUND; SIPROUND;
    v0 ^= b;
    v2 ^= 0xff;
    SIPROUND; SIPROUND; SIPROUND; SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

uint64_t dict_hash(const std::string& key, uint64_t seed) {
    return siphash((const uint8_t*)key.data(), key.size(), seed);
}
