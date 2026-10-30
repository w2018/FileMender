/* hash.c - MD5 (RFC 1321), SHA-1 / SHA-256 (FIPS 180-4), streaming, pure C. */
#include "hash.h"
#include <string.h>

#define ROL32(x, c) (((x) << (c)) | ((x) >> (32 - (c))))
#define ROR32(x, c) (((x) >> (c)) | ((x) << (32 - (c))))

static void be32_put(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void le32_put(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t be32_get(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t le32_get(const unsigned char *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

/* ---------------------------------------------------------------- MD5 --- */

static const uint32_t MD5_K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

static const unsigned char MD5_S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static void md5_block(md5_ctx *c, const unsigned char *p)
{
    uint32_t m[16], a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3];
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = le32_get(p + i * 4);
    }
    for (i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & d) | (~b & e);
            g = i;
        } else if (i < 32) {
            f = (e & b) | (~e & d);
            g = (5 * i + 1) & 15;
        } else if (i < 48) {
            f = b ^ d ^ e;
            g = (3 * i + 5) & 15;
        } else {
            f = d ^ (b | ~e);
            g = (7 * i) & 15;
        }
        f = f + a + MD5_K[i] + m[g];
        a = e;
        e = d;
        d = b;
        b = b + ROL32(f, MD5_S[i]);
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += d;
    c->h[3] += e;
}

void md5_init(md5_ctx *c)
{
    c->h[0] = 0x67452301u;
    c->h[1] = 0xefcdab89u;
    c->h[2] = 0x98badcfeu;
    c->h[3] = 0x10325476u;
    c->total = 0;
    c->n = 0;
}

void md5_update(md5_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    c->total += len;
    if (c->n) {
        size_t need = 64 - c->n;
        if (len < need) {
            memcpy(c->buf + c->n, p, len);
            c->n += len;
            return;
        }
        memcpy(c->buf + c->n, p, need);
        md5_block(c, c->buf);
        p += need;
        len -= need;
        c->n = 0;
    }
    while (len >= 64) {
        md5_block(c, p);
        p += 64;
        len -= 64;
    }
    if (len) {
        memcpy(c->buf, p, len);
        c->n = len;
    }
}

void md5_final(md5_ctx *c, unsigned char out[16])
{
    uint64_t bits = c->total * 8;
    unsigned char pad[72];
    size_t padlen;
    int i;

    padlen = (c->n < 56) ? (56 - c->n) : (120 - c->n);
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (i = 0; i < 8; i++) {
        pad[padlen + i] = (unsigned char)(bits >> (8 * i));
    }
    md5_update(c, pad, padlen + 8);
    for (i = 0; i < 4; i++) {
        le32_put(out + i * 4, c->h[i]);
    }
}

/* -------------------------------------------------------------- SHA-1 --- */

static void sha1_block(sha1_ctx *c, const unsigned char *p)
{
    uint32_t w[80], a, b, d, e, f, k, t;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = be32_get(p + i * 4);
    }
    for (i = 16; i < 80; i++) {
        w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    a = c->h[0];
    b = c->h[1];
    d = c->h[2];
    e = c->h[3];
    f = c->h[4];
    for (i = 0; i < 80; i++) {
        uint32_t tmp;
        if (i < 20) {
            t = (b & d) | (~b & e);
            k = 0x5a827999u;
        } else if (i < 40) {
            t = b ^ d ^ e;
            k = 0x6ed9eba1u;
        } else if (i < 60) {
            t = (b & d) | (b & e) | (d & e);
            k = 0x8f1bbcdcu;
        } else {
            t = b ^ d ^ e;
            k = 0xca62c1d6u;
        }
        tmp = ROL32(a, 5) + t + f + k + w[i];
        f = e;
        e = d;
        d = ROL32(b, 30);
        b = a;
        a = tmp;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += d;
    c->h[3] += e;
    c->h[4] += f;
}

void sha1_init(sha1_ctx *c)
{
    c->h[0] = 0x67452301u;
    c->h[1] = 0xefcdab89u;
    c->h[2] = 0x98badcfeu;
    c->h[3] = 0x10325476u;
    c->h[4] = 0xc3d2e1f0u;
    c->total = 0;
    c->n = 0;
}

void sha1_update(sha1_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    c->total += len;
    if (c->n) {
        size_t need = 64 - c->n;
        if (len < need) {
            memcpy(c->buf + c->n, p, len);
            c->n += len;
            return;
        }
        memcpy(c->buf + c->n, p, need);
        sha1_block(c, c->buf);
        p += need;
        len -= need;
        c->n = 0;
    }
    while (len >= 64) {
        sha1_block(c, p);
        p += 64;
        len -= 64;
    }
    if (len) {
        memcpy(c->buf, p, len);
        c->n = len;
    }
}

void sha1_final(sha1_ctx *c, unsigned char out[20])
{
    uint64_t bits = c->total * 8;
    unsigned char pad[72];
    size_t padlen;
    int i;

    padlen = (c->n < 56) ? (56 - c->n) : (120 - c->n);
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (i = 0; i < 8; i++) {
        pad[padlen + i] = (unsigned char)(bits >> (56 - 8 * i));
    }
    sha1_update(c, pad, padlen + 8);
    for (i = 0; i < 5; i++) {
        be32_put(out + i * 4, c->h[i]);
    }
}

/* ------------------------------------------------------------ SHA-256 --- */

static const uint32_t SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static void sha256_block(sha256_ctx *c, const unsigned char *p)
{
    uint32_t w[64], t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = be32_get(p + i * 4);
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    {
        uint32_t A = c->h[0], B = c->h[1], C = c->h[2], D = c->h[3];
        uint32_t E = c->h[4], F = c->h[5], G = c->h[6], H = c->h[7];
        for (i = 0; i < 64; i++) {
            uint32_t S1 = ROR32(E, 6) ^ ROR32(E, 11) ^ ROR32(E, 25);
            uint32_t ch = (E & F) ^ (~E & G);
            uint32_t S0 = ROR32(A, 2) ^ ROR32(A, 13) ^ ROR32(A, 22);
            uint32_t mj = (A & B) ^ (A & C) ^ (B & C);
            t1 = H + S1 + ch + SHA256_K[i] + w[i];
            t2 = S0 + mj;
            H = G;
            G = F;
            F = E;
            E = D + t1;
            D = C;
            C = B;
            B = A;
            A = t1 + t2;
        }
        c->h[0] += A;
        c->h[1] += B;
        c->h[2] += C;
        c->h[3] += D;
        c->h[4] += E;
        c->h[5] += F;
        c->h[6] += G;
        c->h[7] += H;
    }
}

void sha256_init(sha256_ctx *c)
{
    c->h[0] = 0x6a09e667u;
    c->h[1] = 0xbb67ae85u;
    c->h[2] = 0x3c6ef372u;
    c->h[3] = 0xa54ff53au;
    c->h[4] = 0x510e527fu;
    c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu;
    c->h[7] = 0x5be0cd19u;
    c->total = 0;
    c->n = 0;
}

void sha256_update(sha256_ctx *c, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    c->total += len;
    if (c->n) {
        size_t need = 64 - c->n;
        if (len < need) {
            memcpy(c->buf + c->n, p, len);
            c->n += len;
            return;
        }
        memcpy(c->buf + c->n, p, need);
        sha256_block(c, c->buf);
        p += need;
        len -= need;
        c->n = 0;
    }
    while (len >= 64) {
        sha256_block(c, p);
        p += 64;
        len -= 64;
    }
    if (len) {
        memcpy(c->buf, p, len);
        c->n = len;
    }
}

void sha256_final(sha256_ctx *c, unsigned char out[32])
{
    uint64_t bits = c->total * 8;
    unsigned char pad[72];
    size_t padlen;
    int i;

    padlen = (c->n < 56) ? (56 - c->n) : (120 - c->n);
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    for (i = 0; i < 8; i++) {
        pad[padlen + i] = (unsigned char)(bits >> (56 - 8 * i));
    }
    sha256_update(c, pad, padlen + 8);
    for (i = 0; i < 8; i++) {
        be32_put(out + i * 4, c->h[i]);
    }
}
