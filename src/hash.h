/* hash.h - MD5 / SHA-1 / SHA-256 streaming digests (pure C, no dependencies) */
#ifndef FM_HASH_H
#define FM_HASH_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[4];
    uint64_t total;
    unsigned char buf[64];
    size_t n;
} md5_ctx;

typedef struct {
    uint32_t h[5];
    uint64_t total;
    unsigned char buf[64];
    size_t n;
} sha1_ctx;

typedef struct {
    uint32_t h[8];
    uint64_t total;
    unsigned char buf[64];
    size_t n;
} sha256_ctx;

void md5_init(md5_ctx *c);
void md5_update(md5_ctx *c, const void *data, size_t len);
void md5_final(md5_ctx *c, unsigned char out[16]);

void sha1_init(sha1_ctx *c);
void sha1_update(sha1_ctx *c, const void *data, size_t len);
void sha1_final(sha1_ctx *c, unsigned char out[20]);

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *data, size_t len);
void sha256_final(sha256_ctx *c, unsigned char out[32]);

#endif /* FM_HASH_H */
