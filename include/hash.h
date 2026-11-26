#ifndef HASH_H
#define HASH_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Multiply and Mix: Near-perfect bit avalanche
static inline uint32_t _wymum32(uint32_t A, uint32_t B) 
{
    uint64_t r = (uint64_t)A * B;
    return (uint32_t)(r ^ (r >> 32));
}
// Ultra-fast 32-bit Wyhash algorithm
static inline uint32_t wyhash32(const char *str) 
{
    uint32_t seed = 0xa0761d64u;
    uint32_t a = seed, b = seed ^ 0xe7037ed1u;
    size_t len = strlen(str);
    const uint8_t *p = (const uint8_t *)str;
    size_t i = len;
    // Process 8-byte chunks (blazing fast!)
    for (; i >= 8; i -= 8, p += 8) 
    {
        uint32_t v1 = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        uint32_t v2 = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
        a = _wymum32(a ^ v1, 0x9e3779b9u);
        b = _wymum32(b ^ v2, 0x85ebca6bu);
    }
    // Process remaining 4 bytes
    if (i >= 4) 
    {
        uint32_t v1 = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        a = _wymum32(a ^ v1, 0x9e3779b9u);
        p += 4; i -= 4;
    }
    // Process trailing 1-3 bytes
    if (i > 0) 
    {
        uint32_t v = 0;
        for (size_t k = 0; k < i; k++) v |= ((uint32_t)p[k]) << (k * 8);
        b = _wymum32(b ^ v, 0x85ebca6bu);
    }
    return _wymum32(a ^ b, (uint32_t)len ^ 0x27d4eb2fu);
}

static inline uint32_t hash_u32(uint32_t x)
{
    return x * 0x9e3779b9u;
}

#endif