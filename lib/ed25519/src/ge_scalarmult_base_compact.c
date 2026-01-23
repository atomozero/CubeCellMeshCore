/*
 * Compact ge_scalarmult_base implementation without precomputed tables
 * This saves ~97KB of Flash but is slower
 *
 * Uses double-and-add algorithm instead of windowed multiplication
 */

#include "ge.h"
#include "fe.h"
#include <string.h>

/*
 * Ed25519 base point in compressed form (32 bytes)
 * This is the standard encoding of the base point G = (x, 4/5)
 * where x is the positive root.
 *
 * The point is decoded at runtime using ge_frombytes_negate_vartime
 * (with negation reversed since we need +x not -x)
 */
static const unsigned char ed25519_basepoint_compressed[32] = {
    0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
};

/* Internal function: point doubling P' = 2*P */
static void ge_p3_dbl_internal(ge_p3 *r, const ge_p3 *p) {
    ge_p1p1 t;
    ge_p2 q;
    ge_p3_to_p2(&q, p);
    ge_p2_dbl(&t, &q);
    ge_p1p1_to_p3(r, &t);
}

/* Internal function: point addition R = P + Q */
static void ge_p3_add(ge_p3 *r, const ge_p3 *p, const ge_p3 *q) {
    ge_cached q_cached;
    ge_p1p1 t;
    ge_p3_to_cached(&q_cached, q);
    ge_add(&t, p, &q_cached);
    ge_p1p1_to_p3(r, &t);
}

/*
 * ge_scalarmult_base - Compute h = a * B where B is the Ed25519 base point
 *
 * This is a compact (but slow) implementation using the double-and-add algorithm.
 * It avoids the 97KB precomputed table from precomp_data.h
 *
 * @param h: Output point (ge_p3 format)
 * @param a: 32-byte scalar
 */
void ge_scalarmult_base(ge_p3 *h, const unsigned char *a) {
    ge_p3 base;  /* Base point B */
    ge_p3 temp;
    int i, bit;

    /* Decode base point from compressed form */
    /* ge_frombytes_negate_vartime gives -B, so we need to negate X to get +B */
    ge_frombytes_negate_vartime(&base, ed25519_basepoint_compressed);
    /* Negate X to convert -B to +B */
    fe_neg(base.X, base.X);
    fe_neg(base.T, base.T);

    /* Initialize result to identity (0, 1, 1, 0) */
    ge_p3_0(h);

    /* Double-and-add algorithm: process scalar from MSB to LSB */
    for (i = 255; i >= 0; i--) {
        /* Double current result */
        if (i < 255) {  /* Skip first doubling (doubling identity is identity) */
            ge_p3_dbl_internal(&temp, h);
            memcpy(h, &temp, sizeof(ge_p3));
        }

        /* Add base point if bit is set */
        bit = (a[i / 8] >> (i & 7)) & 1;
        if (bit) {
            ge_p3_add(&temp, h, &base);
            memcpy(h, &temp, sizeof(ge_p3));
        }
    }
}
