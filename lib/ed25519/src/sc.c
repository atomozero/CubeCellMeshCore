/*
 * Compact scalar arithmetic modulo the Ed25519 group order
 *   l = 2^252 + 27742317777372353535851937790883648493
 *
 * Based on the public-domain TweetNaCl modL() routine. Replaces orlp's fully
 * unrolled sc.c (~13.5KB Flash on Cortex-M0+) with a loop-based version
 * (<1KB). Same API and results (fully reduced, little-endian 32-byte scalars).
 */
#include "fixedint.h"
#include "sc.h"

static const int64_t L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10
};

/* r = x mod l, where x is a 64-limb (base 2^8, signed) integer. Clobbers x. */
static void modL(unsigned char *r, int64_t x[64]) {
    int64_t carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; ++j) x[j] -= carry * L[j];
    for (i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = (unsigned char)(x[i] & 255);
    }
}

/* s (64 bytes in) -> s mod l (32 bytes out, in place) */
void sc_reduce(unsigned char *s) {
    int64_t x[64];
    int i;
    for (i = 0; i < 64; ++i) x[i] = (int64_t)s[i];
    modL(s, x);
}

/* s = (a * b + c) mod l */
void sc_muladd(unsigned char *s, const unsigned char *a, const unsigned char *b, const unsigned char *c) {
    int64_t x[64];
    int i, j;
    for (i = 0; i < 64; ++i) x[i] = (i < 32) ? (int64_t)c[i] : 0;
    for (i = 0; i < 32; ++i)
        for (j = 0; j < 32; ++j)
            x[i + j] += (int64_t)a[i] * b[j];
    modL(s, x);
}
