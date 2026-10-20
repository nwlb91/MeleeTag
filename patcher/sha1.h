#ifndef MELEETAG_SHA1_H
#define MELEETAG_SHA1_H

#include <stddef.h>
#include <stdint.h>

typedef struct Sha1 {
    uint32_t h[5];
    uint64_t len;
    uint8_t buf[64];
    size_t fill;
} Sha1;

void sha1_init(Sha1* s);
void sha1_update(Sha1* s, const void* data, size_t n);
void sha1_final(Sha1* s, uint8_t out[20]);
void sha1_hex(const uint8_t digest[20], char out[41]);

#endif
