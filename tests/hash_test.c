/* hash_test.c - self test for MD5 / SHA-1 / SHA-256 against known vectors */
#include "../src/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void hex(const unsigned char *b, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[i * 2] = d[b[i] >> 4];
        out[i * 2 + 1] = d[b[i] & 15];
    }
    out[n * 2] = 0;
}

static void check(const char *label, const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL %-28s got %s want %s\n", label, got, want);
        failures++;
    } else {
        printf("ok   %-28s %s\n", label, got);
    }
}

static void run_all(const char *label, const void *data, size_t len, int chunk,
                    const char *md5v, const char *sha1v, const char *sha256v)
{
    md5_ctx m;
    sha1_ctx s1;
    sha256_ctx s2;
    unsigned char d16[16], d20[20], d32[32];
    char hx[65];
    size_t off = 0;
    char tag[128];

    md5_init(&m);
    sha1_init(&s1);
    sha256_init(&s2);
    while (off < len) {
        size_t n = (size_t)chunk < len - off ? (size_t)chunk : len - off;
        md5_update(&m, (const unsigned char *)data + off, n);
        sha1_update(&s1, (const unsigned char *)data + off, n);
        sha256_update(&s2, (const unsigned char *)data + off, n);
        off += n;
    }
    md5_final(&m, d16);
    sha1_final(&s1, d20);
    sha256_final(&s2, d32);

    snprintf(tag, sizeof(tag), "%s md5", label);
    hex(d16, 16, hx);
    if (*md5v) {
        check(tag, hx, md5v);
    } else {
        printf("info %-28s %s\n", tag, hx);
    }
    snprintf(tag, sizeof(tag), "%s sha1", label);
    hex(d20, 20, hx);
    if (*sha1v) {
        check(tag, hx, sha1v);
    } else {
        printf("info %-28s %s\n", tag, hx);
    }
    snprintf(tag, sizeof(tag), "%s sha256", label);
    hex(d32, 32, hx);
    if (*sha256v) {
        check(tag, hx, sha256v);
    } else {
        printf("info %-28s %s\n", tag, hx);
    }
}

int main(void)
{
    static unsigned char big[10000];
    int i;

    for (i = 0; i < (int)sizeof(big); i++) {
        big[i] = (unsigned char)(i * 37 + 11);
    }

    run_all("empty", "", 0, 0,
            "d41d8cd98f00b204e9800998ecf8427e",
            "da39a3ee5e6b4b0d3255bfef95601890afd80709",
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    run_all("abc", "abc", 3, 1,
            "900150983cd24fb0d6963f7d28e17f72",
            "a9993e364706816aba3e25717850c26c9cd0d89d",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    run_all("fox", "The quick brown fox jumps over the lazy dog", 43, 7,
            "9e107d9d372bb6826bd81d3542a419d6",
            "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12",
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
    /* million 'a' - classic vector */
    {
        char *ma = (char *)malloc(1000000);
        memset(ma, 'a', 1000000);
        run_all("million-a", ma, 1000000, 4096,
                "7707d6ae4e027c70eea2a935c2296f21",
                "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
                "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        free(ma);
    }
    run_all("10k-binary", big, sizeof(big), 333, "", "", "");

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
