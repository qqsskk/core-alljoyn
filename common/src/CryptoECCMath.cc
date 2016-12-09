/**
 * @file CryptoECCMath.cc
 *
 * Class for ECC Crypto Math.
 */

/******************************************************************************
 * Copyright (c) 2016 Open Connectivity Foundation (OCF) and AllJoyn Open
 *    Source Project (AJOSP) Contributors and others.
 *
 *    SPDX-License-Identifier: Apache-2.0
 *
 *    All rights reserved. This program and the accompanying materials are
 *    made available under the terms of the Apache License, Version 2.0
 *    which accompanies this distribution, and is available at
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Copyright 2016 Open Connectivity Foundation and Contributors to
 *    AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for
 *    any purpose with or without fee is hereby granted, provided that the
 *    above copyright notice and this permission notice appear in all
 *    copies.
 *
 *     THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *     WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *     WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *     AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *     DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 *     PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *     TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *     PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

#include <qcc/platform.h>
#include <qcc/Util.h>
#include <qcc/String.h>
#include <qcc/StringUtil.h>

#include <qcc/Debug.h>
#include <qcc/CryptoECC.h>
#include <qcc/CryptoECCOldEncoding.h>
#include <qcc/Crypto.h>

#include <qcc/CryptoECCMath.h>
#include <qcc/CryptoECCfp.h>
#include <qcc/CryptoECCp256.h>

#include <Status.h>

using namespace std;

namespace qcc {

#define QCC_MODULE "CRYPTO"

/* ------------------------------------------------------------------------------------------------------
 * This file contains the mathematics routines for Ellitpic Curve Cryptography.
 * It is used in the generic case, or when CNG is present (on windows) it will only used by the
 * Crypto_ECC_Old_Encoding class for legacy key exchange that uses the whole of the agreed point (in ECDH)
 * for shared key derivation.  (Which is not currently supported by CNG.)
 *
 * All this code was copied from CryptoECC.cc to remerge after the files were split
 * to wrap CNG or use the original method from the generic file.
 *
 */

/* P256 is tested directly with known answer tests from example in
   ANSI X9.62 Annex L.4.2.  (See item in pt_mpy_testcases below.)
   Mathematica code, written in a non-curve-specific way, was also
   tested on the ANSI example, then used to generate both P192 and
   P256 test cases. */

/*
 * This file exports the functions ECDH_generate, ECDH_derive, and
 * optionally, ECDSA_sign and ECDSA_verify.
 */

/*
 * References:
 *
 * [KnuthV2] is D.E. Knuth, The Art of Computer Programming, Volume 2:
 * Seminumerical Algorithms, 1969.
 *
 * [HMV] is D. Hankerson, A. Menezes, and S. Vanstone, Guide to
 * Elliptic Curve Cryptography, 2004.
 *
 * [Wallace] is C.S. Wallace, "A suggestion for a Fast Multiplier",
 * IEEE Transactions on Electronic Computers, EC-13 no. 1, pp 14-17,
 * 1964.
 *
 * [ANSIX9.62] is ANSI X9.62-2005, "Public Key Cryptography for the Financial
 * Services Industry The Elliptic Curve Digital Signature Algorithm
 * (ECDSA)".
 */

/*
 * The vast majority of cycles in programs like this are spent in
 * modular multiplication.  The usual approach is Montgomery
 * multiplication, which effectively does two multiplications in place
 * of one multiplication and one reduction. However, this program is
 * dedicated to the NIST standard curves P256 and P192.  Most of the
 * NIST curves have the property that they can be expressed as a_i *
 * 2^(32*i), where a_i is -1, 0, or +1.  For example P192 is 2^(6*32)
 * - 2^(2*32) - 2^(0*32).  This allows easy word-oriented reduction
 * (32 bit words): The word at position 6 can just be subtracted from
 * word 6 (i.e. word 6 zeroed), and added to words 2 and 0.  This is
 * faster than Montgomery multiplication.
 *
 * Two problems with the naive implementation suggested above are carry
 * propagation and getting the reduction precise.
 *
 * Every time you do an add or subtract you have to propagate carries.
 * The result might come out between the modulus and 2^192 or 2^256,
 * in which case you subtract the modulus.  Most carry propagation is avoided
 * by using 64 bit words during computation, even though the radix is only
 * 2^32.  A carry propagation is done once in the multiplication
 * and once again after the reduction step.  (This idea comes from the carry
 * save adder used in hardware designs.)
 *
 * Exact reduction is required for only a few operations: comparisons,
 * and halving.  The multiplier for point multiplication must also be
 * exactly reduced.  So we do away with the requirement for exact
 * reduction in most operations.  Thus, any reduced value, X, can may
 * represented by X + k * modulus, for any integer k, as long as the
 * result is representable in the data structure.  Typically k is
 * between -1 and 1.  (A bigval_t has one more 32 bit word than is
 * required to hold the modulus, and is interpreted as 2's complement
 * binary, little endian by word, native endian within words.)
 *
 * An exact reduction function is supplied, and must be called as necessary.
 */

/*
 * CONFIGURATION STUFF
 *
 * All these values are undefined.  It seems better to set the
 * preprocessor variables in the makefile, and thus avoid
 * generating many different versions of the code.
 * This may not be practical with ECC_P192 and ECC_P256, but at
 * least that is only in the ecc.h file.
 */

/* define ECDSA to include ECDSA functions */
#define ECDSA
/* define ECC_TEST to rename the the exported symbols to avoid name collisions
   with openSSL, and a few other things necessary for linking with the
   test program ecctest.c */
/* define ARM7_ASM to use assembly code specially for the ARM7 processor */
// #define ARM7_ASM
/* define SMALL_CODE to skip unrolling loops */
// #define SMALL_CODE
/* define SPECIAL_SQUARE to generate a special case for squaring.
   Special squaring should just about halve the number of multiplies,
   but on Windows machines and if loops are unrolled (SMALL_CODE not
   defined) actually causes slight slowing. */
#define SPECIAL_SQUARE
/* define MPY2BITS to consume the multiplier two bits at a time. */
#define MPY2BITS


#ifdef ECC_TEST
/* rename to avoid conflicts with OpenSSL in ecctest.c code. */
#define ECDSA_sign TEST_ECDSA_sign
#define ECDSA_verify TEST_ECDSA_verify
#define COND_STATIC

#else /* ECC_TEST not defined */

#define COND_STATIC static

#endif /* ECC_TEST not defined */



typedef struct {
    int64_t data[2 * BIGLEN];
} dblbigval_t;



static void big_adjustP(bigval_t* tgt, bigval_t const* a, int64_t k);
static void big_1wd_mpy(bigval_t* tgt, bigval_t const* a, int32_t k);
static void big_sub(bigval_t* tgt, bigval_t const* a, bigval_t const* b);

/*
 * Does approximate reduction.  Subtracts most significant word times
 * modulus from src.  The double cast is important to get sign
 * extension right.
 */
#define big_approx_reduceP(tgt, src)                            \
    big_adjustP(tgt, src, -(int64_t)(int32_t)(src)->data[MSW])

/* if tgt is a modular value, it must be precisely reduced */
#define big_is_odd(tgt) ((tgt)->data[0] & 1)

// Squares, always modulo the modulus
#define big_sqrP(tgt, a) big_mpyP(tgt, a, a, MOD_MODULUS)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define OVERFLOWCHECK(sum, a, b) ((((a) > 0) && ((b) > 0) && ((sum) <= 0)) || \
                                  (((a) < 0) && ((b) < 0) && ((sum) >= 0)))

static affine_point_t const affine_infinity = { { { 0, 0, 0, 0, 0, 0, 0 } },
                                                { { 0, 0, 0, 0, 0, 0, 0 } },
                                                B_TRUE };
static jacobian_point_t const jacobian_infinity = { { { 1, 0, 0, 0, 0, 0, 0 } },
                                                    { { 1, 0, 0, 0, 0, 0, 0 } },
                                                    { { 0, 0, 0, 0, 0, 0, 0 } } };

static bigval_t const b_P256 =
{ { 0x27d2604b, 0x3bce3c3e, 0xcc53b0f6, 0x651d06b0,
    0x769886bc, 0xb3ebbd55, 0xaa3a93e7, 0x5ac635d8, 0x00000000 } };
#ifdef ECDSA
static dblbigval_t const orderDBL256 =
{ { 0xfc632551LL - 0x100000000LL,
    0xf3b9cac2LL - 0x100000000LL + 1LL,
    0xa7179e84LL - 0x100000000LL + 1LL,
    0xbce6faadLL - 0x100000000LL + 1LL,
    0xffffffffLL - 0x100000000LL + 1LL,
    0xffffffffLL - 0x100000000LL + 1LL,
    0x00000000LL + 0x1LL,
    0xffffffffLL - 0x100000000LL,
    0x00000000LL + 1LL } };
#endif /* ECDSA */


#define orderDBL orderDBL256
#define curve_b b_P256



#ifdef ARM7_ASM
#define MULACC(a, b)                            \
    __asm                                        \
    {                                            \
        UMULL tmpr0, tmpr1, a, b;                   \
        ADDS sum0, sum0, tmpr0;                   \
        ADCS sum1, sum1, tmpr1;                   \
        ADC cum_carry, cum_carry, 0x0;           \
    }
// cumcarry: 32-bit word that accumulates carries
// sum0: lower half 32-bit word of sum
// sum1: higher half 32-bit word of sum
// a   : 32-bit operand to be multiplied
// b   : 32-bit operand to be multiplied
// tmpr0, tmpr1: two temporary words
// sum = sum + A*B where cout may contain carry info from previous operations

#define MULACC_DOUBLE(a, b)                     \
    __asm                                        \
    {                                            \
        UMULL tmpr0, tmpr1, a, b;                   \
        ADDS sum0, sum0, tmpr0;                   \
        ADCS sum1, sum1, tmpr1;                   \
        ADC cum_carry, cum_carry, 0x0;           \
        ADDS sum0, sum0, tmpr0;                   \
        ADCS sum1, sum1, tmpr1;                   \
        ADC cum_carry, cum_carry, 0x0;           \
    }

#define ACCUM(ap, bp) MULACC(*(ap), *(bp))
#define ACCUMDBL(ap, bp) MULACC_DOUBLE(*(ap), *(bp))

#else /* ARM7_ASM, below is platform independent */

/* (sum, carry) += a * b */
static void mpy_accum(int* cumcarry, uint64_t* sum, uint32_t a, uint32_t b)
{
    uint64_t product = (uint64_t)a * (uint64_t)b;
    uint64_t lsum = *sum;

    lsum += product;
    if (lsum < product) {
        *cumcarry += 1;
    }
    *sum = lsum;
}

#ifdef SPECIAL_SQUARE

/* (sum, carry += 2 * a * b.  Attempts to reduce writes to memory and
   branches caused slowdown on windows machines. */
static void mpy_accum_dbl(int* cumcarry, uint64_t* sum, uint32_t a, uint32_t b)
{
    uint64_t product = (uint64_t)a * (uint64_t)b;
    uint64_t lsum = *sum;
    lsum += product;
    if (lsum < product) {
        *cumcarry += 1;
    }
    lsum += product;
    if (lsum < product) {
        *cumcarry += 1;
    }
    *sum = lsum;
}

#endif /* SPECIAL_SQUARE */

/* ap and bp are pointers to the words to be multiplied and accumulated */
#define ACCUM(ap, bp) mpy_accum(&cum_carry, &u_accum, *(ap),  *(bp))
#define ACCUMDBL(ap, bp) mpy_accum_dbl(&cum_carry, &u_accum, *(ap),  *(bp))

#endif /* !ARM7_ASM, ie platform independent */


/*
 * The big_mpyP algorithm first multiplies the two arguments, with the
 * outer loop indexing over output words, and the inner "loop"
 * (unrolled unless SMALL_CODE is defined), collecting all the terms
 * that contribute to that output word.
 *
 * The impementation is inspired by the Wallace Tree often used in
 * hardware [Wallace], where (0, 1) terms of the same weight are
 * collected together into a sequence values each of which can be on
 * the order of the number of bits in a word, and then the sequence is
 * turned into a binary number with a carry save adder.  This is
 * generized from base 2 to base 2^32.
 *
 * The first part of the algorithm sums together products of equal
 * weight.  The outer loop does carry propagation and makes each value
 * at most 32 bits.
 *
 * Then corrections are applied for negative arguments.  (The first
 * part essentially does unsigned multiplication.)
 *
 * The reduction proceeds in 2 steps.  The first treats the 32 bit
 * values (in 64 bit words) from above as though they were
 * polynomials, and reduces by the paper and pencil method.  Carries
 * are propagated and the result collapsed to a sequence of 32 bit
 * words (in the target).  The second step subtracts MSW * modulus
 * from the result.  This usually (but not always) results in the MSW
 * being zero.  (And that makes subsequent mutliplications faster.)
 *
 * The modselect parameter chooses whether reduction is mod the modulus
 * or the order of the curve.  If ECDSA is not defined, this parameter
 * is ignored, and the curve modulus is used.
 */

/*
 * Computes a * b, approximately reduced mod modulusP or orderP,
 * depending on the modselect flag.
 */
void big_mpyP(bigval_t* tgt, bigval_t const* a, bigval_t const* b,
              modulus_val_t modselect)
{
    int64_t w[2 * BIGLEN];
    int64_t s_accum; /* signed */
    int i, minj, maxj, a_words, b_words, cum_carry;
#ifdef SMALL_CODE
    int j;
#else
    uint32_t const*ap, *bp;
#endif

#ifdef ARM7_ASM
    uint32_t tmpr0, tmpr1, sum0, sum1;
#else
    uint64_t u_accum;
#endif

#ifdef ECDSA
#define MODSELECT modselect
#else
#define MODSELECT MOD_MODULUS
#endif

    if (big_is_zero(a) || big_is_zero(b)) {
        /* a or b is zero, so a*b is zero */
        memset(tgt, 0, sizeof(bigval_t));
        return;
    }

    a_words = BIGLEN;
    while (a_words > 1 && a->data[a_words - 1] == 0) {
        --a_words;
    }

    /*
     * i is target index.  The j (in comments only) indexes
     * through the multiplier.
     */
#ifdef ARM7_ASM
    sum0 = 0;
    sum1 = 0;
    cum_carry = 0;
#else
    u_accum = 0;
    cum_carry = 0;
#endif

#ifndef SPECIAL_SQUARE
#define NO_SPECIAL_SQUARE 1
#else
#define NO_SPECIAL_SQUARE 0
#endif


    if (NO_SPECIAL_SQUARE || a != b) {

        /* normal multiply */

        /* compute length of b */
        b_words = BIGLEN;
        while (b_words > 1 && b->data[b_words - 1] == 0) {
            --b_words;
        }

        /* iterate over words of output */
        for (i = 0; i < a_words + b_words - 1; ++i) {
            /* Run j over all possible values such that
               0 <= j < b_words && 0 <= i-j < a_words.
               Hence
               j >= 0 and j > i - a_words and
               j < b_words and j <= i

               (j exists only in the mind of the reader.)
             */
            maxj = MIN(b_words - 1, i);
            minj = MAX(0, i - a_words + 1);

            /* ACCUM accumlates into <cum_carry, u_accum>. */
#ifdef SMALL_CODE
            for (j = minj; j <= maxj; ++j) {
                ACCUM(a->data + i - j, b->data + j);
            }
#else /* SMALL_CODE not defined */
      /*
       * The inner loop (over j, running from minj to maxj) is
       * unrolled.  Sequentially increasing case values in the code
       * are intended to coax the compiler into emitting a jump
       * table. Here j runs from maxj to minj, but addition is
       * commutative, so it doesn't matter.
       */

            ap = &a->data[i - minj];
            bp = &b->data[minj];

            /* the order is opposite the loop, but addition is commutative */
            switch (8 - (maxj - minj)) {
            case 0: ACCUM(ap - 8, bp + 8); /* j = 8 */

            case 1: ACCUM(ap - 7, bp + 7);

            case 2: ACCUM(ap - 6, bp + 6);

            case 3: ACCUM(ap - 5, bp + 5);

            case 4: ACCUM(ap - 4, bp + 4);

            case 5: ACCUM(ap - 3, bp + 3);

            case 6: ACCUM(ap - 2, bp + 2);

            case 7: ACCUM(ap - 1, bp + 1);

            case 8: ACCUM(ap - 0, bp + 0); /* j = 0 */
            }
#endif /* SMALL_CODE not defined */


            /* The total value is
               w + u_accum << (32 *i) + cum_carry << (32 * i + 64).
               The steps from here to the end of the i-loop (not counting
               squaring branch) and the increment of i by the loop
               maintain the invariant that the value is constant.
               (Assume w had been initialized to zero, even though we
               really didn't.) */

#ifdef ARM7_ASM
            w[i] = sum0;
            sum0 = sum1;
            sum1 = cum_carry;
            cum_carry = 0;
#else
            w[i] = u_accum & 0xffffffffULL;
            u_accum = (u_accum >> 32) + ((uint64_t)cum_carry << 32);
            cum_carry = 0;
#endif
        }
    } else {
        /* squaring */

#ifdef SPECIAL_SQUARE

        /* a[i] * a[j] + a[j] * a[i] == 2 * (a[i] * a[j]), so
           we can cut the number of multiplies nearly in half. */
        for (i = 0; i < 2 * a_words - 1; ++i) {

            /* Run j over all possible values such that
               0 <= j < a_words && 0 <= i-j < a_words && j < i-j
               Hence
               j >= 0 and j > i - a_words and
               j < a_words and 2*j < i
             */
            maxj = MIN(a_words - 1, i);
            /* Only go half way.  Must use (i-1)>> 1, not (i-1)/ 2 */
            maxj = MIN(maxj, (i - 1) >> 1);
            minj = MAX(0, i - a_words + 1);
#ifdef SMALL_CODE
            for (j = minj; j <= maxj; ++j) {
                ACCUMDBL(a->data + i - j, a->data + j);
            }
            /* j live */
            if ((i & 1) == 0) {
                ACCUM(a->data + j, a->data + j);
            }
#else /* SMALL_CODE not defined */
            ap = &a->data[i - minj];
            bp = &a->data[minj];
            switch (8 - (maxj - minj)) {
            case 0: ACCUMDBL(ap - 8, bp + 8); /* j = 8 */

            case 1: ACCUMDBL(ap - 7, bp + 7);

            case 2: ACCUMDBL(ap - 6, bp + 6);

            case 3: ACCUMDBL(ap - 5, bp + 5);

            case 4: ACCUMDBL(ap - 4, bp + 4);

            case 5: ACCUMDBL(ap - 3, bp + 3);

            case 6: ACCUMDBL(ap - 2, bp + 2);

            case 7: ACCUMDBL(ap - 1, bp + 1);

            case 8: ACCUMDBL(ap - 0, bp + 0); /* j = 0 */
            }

            /* Even numbered columns (zero based) have a middle element. */
            if ((i & 1) == 0) {
                ACCUM(a->data + maxj + 1, a->data + maxj + 1);
            }
#endif /* SMALL_CODE not defined */

            /* The total value is
               w + u_accum << (32 *i) + cum_carry << (32 * i + 64).
               The steps from here to the end of i-loop and
               the increment of i by the loop maintain the invariant
               that the total value is unchanged.
               (Assume w had been initialized to zero, even though we
               really didn't.) */
#ifdef ARM7_ASM
            w[i] = sum0;
            sum0 = sum1;
            sum1 = cum_carry;
            cum_carry = 0;
#else /* ARM7_ASM not defined */
            w[i] = u_accum & 0xffffffffULL;
            u_accum = (u_accum >> 32) + ((uint64_t)cum_carry << 32);
            cum_carry = 0;
#endif /* ARM7_ASM not defined */
        }
#endif /* SPECIAL_SQUARE */
    } /* false branch of NO_SPECIAL_SQUARE || (a != b)  */

    /* The total value as indicated above is maintained invariant
       down to the approximate reduction code below. */

    /* propagate any residual to next to end of array */
    for (; i < 2 * BIGLEN - 1; ++i) {
#ifdef ARM7_ASM
        w[i] = sum0;
        sum0 = sum1;
        sum1 = 0;
#else
        w[i] = u_accum & 0xffffffffULL;
        u_accum >>= 32;
#endif
    }
    /* i is still live */
    /* from here on, think of w as containing signed values */

    /* Last value of the array, still using i.  We store the entire 64
       bits.  There are two reasons for this.  The pedantic one is that
       this clearly maintains our invariant that the value has not
       changed.  The other one is that this makes w[BIGNUM-1] negative
       if the result was negative, and reduction depends on this. */

#ifdef ARM7_ASM
    w[i] = ((uint64_t)sum1 << 32) | sum0;
    /* sum1 = sum0 = 0;  maintain invariant */
#else
    w[i] = u_accum;
    /* u_accum = 0; maintain invariant */
#endif

    /*
     * Apply correction if a or b are negative.  It would be nice to
     * put this inside the i-loop to reduce memory bandwidth.  Later...
     *
     * signvedval(a) = unsignedval(a) - 2^(32*BIGLEN)*isneg(a).
     *
     * so signval(a) * signedval(b) = unsignedval(a) * unsignedval[b] -
     *  isneg(a) * unsignedval(b) * 2^(32*BIGLEN) -
     *  isneg(b) * unsingedval(a) * 2^ (32*BIGLEN) +
     *  isneg(a) * isneg(b) * 2 ^(2 * 32 * BIGLEN)
     *
     * If one arg is zero and the other is negative, obviously no
     * correction is needed, but we do not make a special case, since
     * the "correction" only adds in zero.
     */

    if (big_is_negative(a)) {
        for (i = 0; i < BIGLEN; ++i) {
            w[i + BIGLEN] -= b->data[i];
        }
    }
    if (big_is_negative(b)) {
        for (i = 0; i < BIGLEN; ++i) {
            w[i + BIGLEN] -= a->data[i];
        }
        if (big_is_negative(a)) {
            /* both negative */
            w[2 * BIGLEN - 1] += 1ULL << 32;
        }
    }

    /*
     * The code from here to the end of the function maintains w mod
     * modulusP constant, even though it changes the value of w.
     */

    /* reduce (approximate) */
    if (MODSELECT == MOD_MODULUS) {
        for (i = 2 * BIGLEN - 1; i >= MSW; --i) {
            int64_t v;
            v = w[i];
            if (v != 0) {
                w[i] = 0;
                w[i - 1] += v;
                w[i - 2] -= v;
                w[i - 5] -= v;
                w[i - 8] += v;
            }
        }
    } else {
        /* modulo order.  Not performance critical */
#ifdef ECDSA

        int64_t carry;

        /* convert to 32 bit values, except for most signifiant word */
        carry = 0;
        for (i = 0; i < 2 * BIGLEN - 1; ++i) {
            w[i] += carry;
            carry =  w[i] >> 32;
            w[i] -= carry << 32;
        }
        /* i is live */
        w[i] += carry;

        /* each iteration knocks off word i */
        for (i = 2 * BIGLEN - 1; i >= MSW; --i) { /* most to least significant */
            int64_t v;
            int64_t tmp;
            int64_t tmp2;
            int j;
            int k;

            for (k = 0; w[i] != 0 && k < 3; ++k) {
                v = w[i];
                carry = 0;
                for (j = i - MSW; j < 2 * BIGLEN; ++j) {
                    if (j <= i) {
                        tmp2 = -(v * orderDBL.data[j - i + MSW]);
                        tmp = w[j] + tmp2 + carry;
                    } else {
                        tmp = w[j] + carry;
                    }
                    if (j < 2 * BIGLEN - 1) {
                        carry = tmp >> 32;
                        tmp -= carry << 32;
                    } else {
                        carry = 0;
                    }
                    w[j] = tmp;
                }
            }
        }
#endif /* ECDSA */
    }

    /* propagate carries and copy out to tgt in 32 bit chunks. */
    s_accum = 0;
    for (i = 0; i < BIGLEN; ++i) {
        s_accum += w[i];
        tgt->data[i] = (uint32_t)s_accum;
        s_accum >>= 32; /* signed, so sign bit propagates */
    }

    /* final approximate reduction */

    if (MODSELECT == MOD_MODULUS) {
        big_approx_reduceP(tgt, tgt);
    } else {
#ifdef ECDSA
        if (tgt->data[MSW]) {
            /* Keep it simple! At one time all this was done in place,
               and was totally unobvious. */
            bigval_t tmp;
            /* The most significant word is signed, even though the
               whole array has declared uint32_t.  */
            big_1wd_mpy(&tmp, &orderP, (int32_t)tgt->data[MSW]);
            big_sub(tgt, tgt, &tmp);
        }
#endif /* ECDSA */
    }
}

/*
 * Adds k * modulusP to a and stores into target.  -2^62 <= k <= 2^62 .
 * (This is conservative.)
 */
static void big_adjustP(bigval_t* tgt, bigval_t const* a, int64_t k)
{


#define RDCSTEP(i, adj)                         \
    w += a->data[i];                             \
    w += (adj);                                  \
    tgt->data[i] = (uint32_t)(int32_t)w;         \
    w >>= 32;

    /* add k * modulus */

    if (k != 0) {
        int64_t w = 0;
        RDCSTEP(0, -k);
        RDCSTEP(1, 0);
        RDCSTEP(2, 0);
        RDCSTEP(3, k);
        RDCSTEP(4, 0);
        RDCSTEP(5, 0);
        RDCSTEP(6, k);
        RDCSTEP(7, -k);
        RDCSTEP(8, k);
    } else if (tgt != a) {
        *tgt = *a;
    }
}

/*
 * Computes k * a and stores into target.  Conditions: product must
 * be representable in bigval_t.
 */
static void big_1wd_mpy(bigval_t* tgt, bigval_t const* a, int32_t k)
{
    int64_t w = 0;
    int64_t tmp;
    int64_t prod;
    int j;

    for (j = 0; j <= MSW; ++j) {
        prod = (int64_t)k * (int64_t)a->data[j];
        tmp = w + prod;
        w = tmp;
        tgt->data[j] = (uint32_t)w;
        w -= tgt->data[j];
        w >>= 32;
    }
}


/*
 * Adds a to b as signed (2's complement) numbers.  Ok to use for
 * modular values if you don't let the sum overflow.
 */
void big_add(bigval_t* tgt, bigval_t const* a, bigval_t const* b)
{
    uint64_t v;
    int i;

    v = 0;
    for (i = 0; i < BIGLEN; ++i) {
        v += a->data[i];
        v += b->data[i];
        tgt->data[i] = (uint32_t)v;
        v >>= 32;
    }
}

/*
 * modulo modulusP addition with approximate reduction.
 */
static void big_addP(bigval_t* tgt, bigval_t const* a, bigval_t const* b)
{
    big_add(tgt, a, b);
    big_approx_reduceP(tgt, tgt);
}


/* 2's complement subtraction */
static void big_sub(bigval_t* tgt, bigval_t const* a, bigval_t const* b)
{
    uint64_t v;
    int i;
    /* negation is equivalent to 1's complement and increment */

    v = 1; /* increment */
    for (i = 0; i < BIGLEN; ++i) {
        v += a->data[i];
        v += (uint64_t)((uint32_t)(~b->data[i])); /* 1's complement */
        tgt->data[i] = (uint32_t)v;
        v >>= 32;
    }
}


/*
 * modulo modulusP subtraction with approximate reduction.
 */
static void big_subP(bigval_t* tgt, bigval_t const* a, bigval_t const* b)
{
    big_sub(tgt, a, b);
    big_approx_reduceP(tgt, tgt);
}


/* returns 1 if a > b, -1 if a < b, and 0 if a == b.
   a and b are 2's complement.  When applied to modular values,
   args must be precisely reduced. */
int big_cmp(bigval_t const* a, bigval_t const* b)
{
    int i;

    /* most significant word is treated as 2's complement */
    if ((int32_t)a->data[MSW] > (int32_t)b->data[MSW]) {
        return (1);
    } else if ((int32_t)a->data[MSW] < (int32_t)b->data[MSW]) {
        return (-1);
    }
    /* remainder treated as unsigned */
    for (i = MSW - 1; i >= 0; --i) {
        if (a->data[i] > b->data[i]) {
            return (1);
        } else if (a->data[i] < b->data[i]) {
            return (-1);
        }
    }
    return (0);
}


/*
 * Computes tgt = a mod modulus.  Only works with modluii slightly
 * less than 2**(32*(BIGLEN-1)).  Both modulusP and orderP qualify.
 */
void big_precise_reduce(bigval_t* tgt, bigval_t const* a, bigval_t const* modulus)
{
    /*
     * src is a trick to avoid an extra copy of a to arg a to a
     * temporary.  Every statement uses src as the src and tgt as the
     * destination, and it executes src = tgt, so all subsequent
     * operations affect the modified data, not the original.  There is
     * a case to handle the situation of no modifications having been
     * made.
     */
    bigval_t const* src = a;

    /* If tgt < 0, a positive value gets added in, so eventually tgt
       will be >= 0.  If tgt > 0 and the MSW is non-zero, a non-zero
       value smaller than tgt gets subtracted, so eventually target
       becomes < 1 * 2**(32*MSW), but not negative, i.e. tgt->data[MSW]
       == 0, and thus loop termination is guaranteed. */


    while ((int32_t)src->data[MSW] != 0) {
        if (modulus != &modulusP) {
            /* General case.  Keep it simple! */
            bigval_t tmp;

            /* The most significant word is signed, even though the
               whole array has been declared uint32_t.  */
            big_1wd_mpy(&tmp, modulus, (int32_t)src->data[MSW]);
            big_sub(tgt, src, &tmp);
        } else {
            /* just an optimization.  The other branch would work, but slower. */
            big_adjustP(tgt, src, -(int64_t)(int32_t)src->data[MSW]);
        }
        src = tgt;
    }

    while (big_cmp(src, modulus) >= 0) {
        big_sub(tgt, src, modulus);
        src = tgt;
    }
    while ((int32_t)src->data[MSW] < 0) {
        big_add(tgt, src, modulus);
        src = tgt;
    }

    /* copy src to tgt if not already done */

    if (src != tgt) {
        *tgt = *src;
    }
}

/* computes floor(a / 2), 2's complement. */
static void big_halve(bigval_t* tgt, bigval_t const* a)
{
    uint32_t shiftval;
    uint32_t new_shiftval;
    int i;

    /* most significant word is 2's complement.  Do it separately. */
    shiftval = a->data[MSW] & 1;
    tgt->data[MSW] = (uint32_t)((int32_t)a->data[MSW] >> 1);

    for (i = MSW - 1; i >= 0; --i) {
        new_shiftval = a->data[i] & 1;
        tgt->data[i] = (a->data[i] >> 1) | (shiftval << 31);
        shiftval = new_shiftval;
    }
}

/*
 * computes tgt, such that 2 * tgt === a, (mod modulusP).  NOTE WELL:
 * arg a must be precisely reduced.  This function could do that, but
 * in some cases, arg a is known to already be reduced and we don't
 * want to waste cycles.  The code could be written more cleverly to
 * avoid passing over the data twice in the case of an odd value.
 */
static void big_halveP(bigval_t* tgt, bigval_t const* a)
{
    if (a->data[0] & 1) {
        /* odd */
        big_adjustP(tgt, a, 1);
        big_halve(tgt, tgt);
    } else {
        /* even */
        big_halve(tgt, a);
    }
}

/* returns B_TRUE if a is zero */
boolean_t big_is_zero(bigval_t const* a)
{
    int i;

    for (i = 0; i < BIGLEN; ++i) {
        if (a->data[i] != 0) {
            return (B_FALSE);
        }
    }
    return (B_TRUE);
}

/* returns B_TRUE if a is one */
static boolean_t big_is_one(bigval_t const* a)
{
    int i;

    if (a->data[0] != 1) {
        return (B_FALSE);
    }
    for (i = 1; i < BIGLEN; ++i) {
        if (a->data[i] != 0) {
            return (B_FALSE);
        }
    }
    return (B_TRUE);
}


/*
 * This uses the extended binary GCD (Greatest Common Divisor)
 * algorithm.  The binary GCD algorithm is presented in [KnuthV2] as
 * Algorithm X.  The extension to do division is presented in Homework
 * Problem 15 and its solution in the back of the book.
 *
 * The implementation here follows the presentation in [HMV] Algorithm
 * 2.22.
 *
 * If the denominator is zero, it will loop forever.  Be careful!
 * Modulus must be odd.  num and den must be positive.
 */
void big_divide(bigval_t* tgt, bigval_t const* num, bigval_t const* den,
                bigval_t const* modulus)
{
    bigval_t u, v, x1, x2;

    u = *den;
    v = *modulus;
    x1 = *num;
    x2 = big_zero;

    while (!big_is_one(&u) && !big_is_one(&v)) {
        while (!big_is_odd(&u)) {
            big_halve(&u, &u);
            if (big_is_odd(&x1)) {
                big_add(&x1, &x1, modulus);
            }
            big_halve(&x1, &x1);
        }
        while (!big_is_odd(&v)) {
            big_halve(&v, &v);
            if (big_is_odd(&x2)) {
                big_add(&x2, &x2, modulus);
            }
            big_halve(&x2, &x2);
        }
        if (big_cmp(&u, &v) >= 0) {
            big_sub(&u, &u, &v);
            big_sub(&x1, &x1, &x2);
        } else {
            big_sub(&v, &v, &u);
            big_sub(&x2, &x2, &x1);
        }
    }

    if (big_is_one(&u)) {
        big_precise_reduce(tgt, &x1, modulus);
    } else {
        big_precise_reduce(tgt, &x2, modulus);
    }
}


static void big_triple(bigval_t* tgt, bigval_t const* a)
{
    int i;
    uint64_t accum = 0;

    /* technically, the lower significance words should be treated as
       unsigned and the most significant word treated as signed
       (arithmetic right shift instead of logical right shift), but
       accum can never get negative during processing the lower
       significance words, and the most significant word is the last
       word processed, so what is left in the accum after the final
       shift does not matter.
     */

    for (i = 0; i < BIGLEN; ++i) {
        accum += a->data[i];
        accum += a->data[i];
        accum += a->data[i];
        tgt->data[i] = (uint32_t)accum;
        accum >>= 32;
    }
}



/*
 * The point add and point double algorithms use mixed Jacobian
 * and affine coordinates.  The affine point (x,y) corresponds
 * to the Jacobian point (X, Y, Z), for any non-zero Z, with X = Z^2 * x
 * and Y = Z^3 * y.  The infinite point is represented in Jacobian
 * coordinates as (1, 1, 0).
 */

#define jacobian_point_is_infinity(P) (big_is_zero(&(P)->Z))

void toJacobian(jacobian_point_t* tgt, affine_point_t const* a) {
    tgt->X = a->x;
    tgt->Y = a->y;
    tgt->Z = big_one;
}

/* a->Z must be precisely reduced */
void toAffine(affine_point_t* tgt, jacobian_point_t const* a)
{
    bigval_t zinv, zinvpwr;

    if (big_is_zero(&a->Z)) {
        *tgt = affine_infinity;
        return;
    }
    big_divide(&zinv, &big_one, &a->Z, &modulusP);
    big_sqrP(&zinvpwr, &zinv);  /* Zinv^2 */
    big_mpyP(&tgt->x, &a->X, &zinvpwr, MOD_MODULUS);
    big_mpyP(&zinvpwr, &zinvpwr, &zinv, MOD_MODULUS); /* Zinv^3 */
    big_mpyP(&tgt->y, &a->Y, &zinvpwr, MOD_MODULUS);
    big_precise_reduce(&tgt->x, &tgt->x, &modulusP);
    big_precise_reduce(&tgt->y, &tgt->y, &modulusP);
    tgt->infinity = B_FALSE;
}

/*
 * From [HMV] Algorithm 3.21.
 */

/* tgt = 2 * P.  P->Z must be precisely reduced and
   tgt->Z will be precisely reduced */
static void pointDouble(jacobian_point_t* tgt, jacobian_point_t const* P)
{
    bigval_t x3loc, y3loc, z3loc, t1, t2, t3;

#define x1 (&P->X)
#define y1 (&P->Y)
#define z1 (&P->Z)
#define x3 (&x3loc)
#define y3 (&y3loc)
#define z3 (&z3loc)

    /* This requires P->Z be precisely reduced */
    if (jacobian_point_is_infinity(P)) {
        *tgt = jacobian_infinity;
        return;
    }

    big_sqrP(&t1, z1);
    big_subP(&t2, x1, &t1);
    big_addP(&t1, x1, &t1);
    big_mpyP(&t2, &t2, &t1, MOD_MODULUS);
    big_triple(&t2, &t2);
    big_addP(y3, y1, y1);
    big_mpyP(z3, y3, z1, MOD_MODULUS);
    big_sqrP(y3, y3);
    big_mpyP(&t3, y3, x1, MOD_MODULUS);
    big_sqrP(y3, y3);
    big_halveP(y3, y3);
    big_sqrP(x3, &t2);
    big_addP(&t1, &t3, &t3);
    /* x1 not used after this point.  Safe to store to tgt, even if aliased */
    big_subP(&tgt->X /* x3 */, x3, &t1);
#undef  x3
#define x3 (&tgt->X)
    big_subP(&t1, &t3, x3);
    big_mpyP(&t1, &t1, &t2, MOD_MODULUS);
    big_subP(&tgt->Y, &t1, y3);

    /* Z components of returned Jacobian points must
       be precisely reduced */
    big_precise_reduce(&tgt->Z, z3, &modulusP);
#undef x1
#undef y1
#undef z1
#undef x3
#undef y3
#undef z3
}


/* From [HMV] Algorithm 3.22 */

/* tgt = P + Q.  P->Z must be precisely reduced.
   tgt->Z will be precisely reduced.  tgt and P can be aliased.
 */
void pointAdd(jacobian_point_t* tgt, jacobian_point_t const* P,
              affine_point_t const* Q)
{
    bigval_t t1, t2, t3, t4, x3loc;

    if (Q->infinity) {
        if (tgt != P) {
            *tgt = *P;
        }
        return;
    }

    /* This requires that P->Z be precisely reduced */
    if (jacobian_point_is_infinity(P)) {
        toJacobian(tgt, Q);
        return;
    }


#define x1 (&P->X)
#define y1 (&P->Y)
#define z1 (&P->Z)
#define x2 (&Q->x)
#define y2 (&Q->y)
#define x3 (&x3loc)
#define y3 (&y3loc)
#define z3 (&tgt->Z)

    big_sqrP(&t1, z1);
    big_mpyP(&t2, &t1, z1, MOD_MODULUS);
    big_mpyP(&t1, &t1, x2, MOD_MODULUS);
    big_mpyP(&t2, &t2, y2, MOD_MODULUS);
    big_subP(&t1, &t1, x1);
    big_subP(&t2, &t2, y1);
    /* big_is_zero requires precisely reduced arg */
    big_precise_reduce(&t1, &t1, &modulusP);
    if (big_is_zero(&t1)) {
        big_precise_reduce(&t2, &t2, &modulusP);
        if (big_is_zero(&t2)) {
            toJacobian(tgt, Q);
            pointDouble(tgt, tgt);
        } else {
            *tgt = jacobian_infinity;
        }
        return;
    }
    /* store into target.  okay, even if tgt is aliased with P,
       as z1 is not subsequently used */
    big_mpyP(z3, z1, &t1, MOD_MODULUS);
    /* z coordinates of returned jacobians must be precisely reduced. */
    big_precise_reduce(z3, z3, &modulusP);
    big_sqrP(&t3, &t1);
    big_mpyP(&t4, &t3, &t1, MOD_MODULUS);
    big_mpyP(&t3, &t3, x1, MOD_MODULUS);
    big_addP(&t1, &t3, &t3);
    big_sqrP(x3, &t2);
    big_subP(x3, x3, &t1);
    big_subP(&tgt->X /* x3 */, x3, &t4);
    /* switch x3 to tgt */
#undef x3
#define x3 (&tgt->X)
    big_subP(&t3, &t3, x3);
    big_mpyP(&t3, &t3, &t2, MOD_MODULUS);
    big_mpyP(&t4, &t4, y1, MOD_MODULUS);
    /* switch y3 to tgt */
#undef y3
#define y3 (&tgt->Y)
    big_subP(y3, &t3, &t4);
#undef  x1
#undef  y1
#undef  z1
#undef  x2
#undef  y2
#undef  x3
#undef  y3
#undef  z3

}

/* pointMpyP uses a left-to-right binary double-and-add method, which
 * is an exact analogy to the left-to-right binary meethod for
 * exponentiation described in [KnuthV2] Section 4.6.3.
 */

/* returns bit i of bignum n.  LSB of n is bit 0. */
#define big_get_bit(n, i) (((n)->data[(i) / 32] >> ((i) % 32)) & 1)
/* returns bits i+1 and i of bignum n.  LSB of n is bit 0; i <= 30 */
#define big_get_2bits(n, i) (((n)->data[(i) / 32] >> ((i) % 32)) & 3)

/* k must be non-negative.  Negative values (incorrectly)
   return the infinite point */

void pointMpyP(affine_point_t* tgt, bigval_t const* k, affine_point_t const* P)
{
    int i;
    jacobian_point_t Q;

    if (big_is_negative(k)) {
        /* This should never happen.*/
        *tgt = affine_infinity;
        return;
    }

    Q = jacobian_infinity;

    /* faster */
#ifdef MPY2BITS
    affine_point_t const*mpyset[4];
    affine_point_t twoP, threeP;
#endif /* MPY2BITS */

    if (big_is_zero(k) || big_is_negative(k)) {
        *tgt = affine_infinity;
        return;
    }

#ifndef MPY2BITS
    /* Classical high-to-low method */
    /* discard high order zeros */
    for (i = BIGLEN * 32 - 1; i >= 0; --i) {
        if (big_get_bit(k, i)) {
            break;
        }
    }
    /* Can't fall through since k is non-zero.  We get here only via the break */
    /* discard highest order 1 bit */
    --i;

    toJacobian(&Q, P);
    for (; i >= 0; --i) {
        pointDouble(&Q, &Q);
        if (big_get_bit(k, i)) {
            pointAdd(&Q, &Q, P);
        }
    }
#else /* MPY2BITS defined */
      /* multiply 2 bits at a time */
      /* precompute 1P, 2P, and 3P */
    mpyset[0] = (affine_point_t*)0;
    mpyset[1] = P;
    toJacobian(&Q, P);  /* Q = P */
    pointDouble(&Q, &Q); /* now Q = 2P */
    toAffine(&twoP, &Q);
    mpyset[2] = &twoP;
    pointAdd(&Q, &Q, P); /* now Q = 3P */
    toAffine(&threeP, &Q);
    mpyset[3] = &threeP;

    /* discard high order zeros (in pairs) */
    for (i = BIGLEN * 32 - 2; i >= 0; i -= 2) {
        if (big_get_2bits(k, i)) {
            break;
        }
    }

    Q = jacobian_infinity;

    for (; i >= 0; i -= 2) {
        int mbits = big_get_2bits(k, i);
        pointDouble(&Q, &Q);
        pointDouble(&Q, &Q);
        if (mpyset[mbits] != (affine_point_t*)0) {
            pointAdd(&Q, &Q, mpyset[mbits]);
        }
    }

#endif /* MPY2BITS */

    toAffine(tgt, &Q);
}

boolean_t in_curveP(affine_point_t const* P)
{
    ecpoint_t Pt;
    ec_t curve;

    boolean_t fInfinity;
    boolean_t fValid;

    QStatus status;

    status = ec_getcurve(&curve, NISTP256r1);
    if (status != ER_OK) {
        return B_FALSE;
    }

    fInfinity = (boolean_t)(0 != P->infinity);

    bigval_to_digit256(&P->x, Pt.x);
    bigval_to_digit256(&P->y, Pt.y);

    fValid = (boolean_t)ecpoint_validation(&Pt, &curve);

    ec_freecurve(&curve);
    return (boolean_t)(fInfinity | fValid);
}

/*
 * Convert a digit256_t (internal representation of field elements) to a
 * bigval_t. Note: dst must have space for sizeof(digit256_t) + 4 bytes.
 */
void digit256_to_bigval(digit256_tc src, bigval_t* dst)
{
    QCC_ASSERT((BIGLEN - 1) * sizeof(uint32_t) == sizeof(digit256_t));

    memcpy(dst->data, src, sizeof(digit256_t));
    dst->data[BIGLEN - 1] = 0;

#if (QCC_TARGET_ENDIAN == QCC_BIG_ENDIAN)
    int i;
    for (i = 0; i < (BIGLEN - 1); i += 2) {    /* Swap adjacent 32-bit words */
        SWAP(dst->data[i], dst->data[i + 1]);
    }
#endif

}

/*
 * Convert a bigval_t to a digit256_t.  Return TRUE if src was
 * successfully converted, FALSE otherwise.
 */
bool bigval_to_digit256(const bigval_t* src, digit256_t dst)
{
    QCC_ASSERT((BIGLEN - 1) * sizeof(uint32_t) == sizeof(digit256_t));

    /* Fail on negative inputs, since any negative value received in the
     * bigval_t format is invalid.
     */
    if (big_is_negative(src)) {
        return false;
    }

    memcpy(dst, src->data, sizeof(digit256_t));

#if (QCC_TARGET_ENDIAN == QCC_BIG_ENDIAN)
    int i;
    uint32_t* data = (uint32_t*)dst;
    for (i = 0; i < (BIGLEN - 1); i += 2) {    /* Swap adjacent 32-bit words */
        SWAP(data[i], data[i + 1]);
    }
#endif

    return true;
}

/*
 * computes a secret value, k, and a point, P1, to send to the other
 * party.  Returns 0 on success, -1 on failure (of the RNG).
 */
QStatus ECDH_generate(affine_point_t* P1, bigval_t* k)
{
    /* Compute a key pair (r, Q) then re-encode and output as (k, P1). */
    digit256_t r;
    ecpoint_t g, Q;
    ec_t curve;
    QStatus status;

    status = ec_getcurve(&curve, NISTP256r1);
    if (status != ER_OK) {
        goto Exit;
    }

    /* Choose random r in [0, curve order - 1]*/
    do {
        status = Crypto_GetRandomBytes((uint8_t*)r, sizeof(digit256_t));
        if (status != ER_OK) {
            goto Exit;
        }
    } while (!validate_256(r, curve.order));

    ec_get_generator(&g, &curve);
    status = ec_scalarmul(&g, r, &Q, &curve);        /* Q = g^r */

    /* Convert out of internal representation. */
    digit256_to_bigval(r, k);
    digit256_to_bigval(Q.x, &(P1->x));
    digit256_to_bigval(Q.y, &(P1->y));
    P1->infinity = false;

Exit:
    fpzero_p256(r);
    fpzero_p256(Q.x);
    fpzero_p256(Q.y);
    ec_freecurve(&curve);
    return status;
}

/*
 * Converts bigval_t src to a network order (big-endian) binary (byte
 * vector) representation. The if tgtlen is longer that the bigval_t,
 * the value is written sign-extended.  If tgtlen is too small to hold
 * the value, high order bytes are silently dropped.
 */
void bigval_to_binary(bigval_t const* src, void* tgt, size_t tgtlen)
{
    size_t i;
    uint8_t v;
    uint8_t highbytes = big_is_negative(src) ? 0xff : 0;

    /* LSbyte to MS_byte */
    for (i = 0; i < 4 * BIGLEN; ++i) {
        if (i < tgtlen) {
            v = src->data[i / 4] >> (8 * (i % 4));
            ((uint8_t*)tgt)[tgtlen - 1 - i] = v;
        }
    }
    /* i is live */
    for (; i < tgtlen; ++i) {
        ((uint8_t*)tgt)[tgtlen - 1 - i] = highbytes;
    }
}

/*
 * Converts a network-order (big-endian) binary value (byte array) at
 * *src of length srclen to a bigval_t at *bn.  If srclen is larger
 * then the length of a bigval_t, the high order bytes are silently
 * dropped.
 */
void binary_to_bigval(const void* src, bigval_t* tgt, size_t srclen)
{
    size_t i;
    uint8_t v;

    /* zero the bigval_t */
    for (i = 0; i < (size_t) BIGLEN; ++i) {
        tgt->data[i] = 0;
    }
    /* scan from LSbyte to MSbyte */
    for (i = 0; i < srclen && i < 4 * BIGLEN; ++i) {
        v = ((uint8_t*)src)[srclen - 1 - i];
        tgt->data[i / 4] |= (uint32_t)v << (8 * (i % 4));
    }
}

/* Take the point sent by the other party, and verify that it is a
   valid point.  If 1 <= k < orderP and the point is valid, store
   the resuling point *tgt and returns B_TRUE.  If the point is invalid
   return B_FALSE.  The behavior with k out of range is unspecified,
   but safe. */

bool ECDH_derive_pt(affine_point_t* tgt, bigval_t const* k, affine_point_t const* Q)
{
    bool status;
    QStatus ajstatus;
    ecpoint_t theirPublic;      /* internal representation of Q */
    ecpoint_t sharedSecret;
    digit256_t ourPrivate;      /* internal representation of k */
    ec_t curve;

    ajstatus = ec_getcurve(&curve, NISTP256r1);
    if (ajstatus != ER_OK) {
        status = false;
        goto Exit;
    }

    /* Convert to internal representation */
    status = bigval_to_digit256(k, ourPrivate);
    status = status && bigval_to_digit256(&(Q->x), theirPublic.x);
    status = status && bigval_to_digit256(&(Q->y), theirPublic.y);
    if (!status) {
        goto Exit;
    }

    if (!ecpoint_validation(&theirPublic, &curve)) {
        status = false;
        goto Exit;
    }

    /* Compute sharedSecret = theirPublic^ourPrivate */
    ajstatus = ec_scalarmul(&theirPublic, ourPrivate, &sharedSecret, &curve);

    status = ec_oncurve(&sharedSecret, &curve);

    status &= (ER_OK == ajstatus);

    /* Copy sharedSecret to tgt */
    digit256_to_bigval(sharedSecret.x, &(tgt->x));
    digit256_to_bigval(sharedSecret.y, &(tgt->y));
    tgt->infinity = ec_is_infinity(&sharedSecret, &curve);

Exit:
    /* Clean up local copies. */
    fpzero_p256(sharedSecret.x);
    fpzero_p256(sharedSecret.y);
    fpzero_p256(ourPrivate);
    ec_freecurve(&curve);
    return status;
}

#ifdef ECC_TEST
char*ECC_feature_list(void)
{
    return (
        "ECC_P256"
#ifdef ECDSA
        " ECDSA"
#endif
#ifdef SPECIAL_SQUARE
        " SPECIAL_SQUARE"
#endif
#ifdef SMALL_CODE
        " SMALL_CODE"
#endif
#ifdef MPY2BITS
        " MPY2BITS"
#endif
#ifdef ARM7_ASM
        " ARM7_ASM"
#endif
        );
}
#endif /* ECC_TEST */


/*
 * convert a host uint32_t array to a big-endian byte array
 */
void U32ArrayToU8BeArray(const uint32_t* src, size_t len, uint8_t* dest)
{
    uint32_t* p = (uint32_t*) dest;
    for (size_t cnt = 0; cnt < len; cnt++) {
        p[cnt] = htobe32(src[cnt]);
    }
}

/*
 * convert a big-endian byte array to a host uint32_t array
 */
void U8BeArrayToU32Array(const uint8_t* src, size_t len, uint32_t* dest)
{
    uint32_t* p = (uint32_t*) src;
    size_t u32Len = len / sizeof(uint32_t);
    for (size_t cnt = 0; cnt < u32Len; cnt++) {
        dest[cnt] = betoh32(p[cnt]);
    }
}

/*
 * Generates the Diffie-Hellman shared secret.
 * @param   peerPublicKey the peer's public key
 * @param   privateKey the local private key
 * @param[out]   secret the output shared secret
 * @return
 *      ER_OK if the shared secret is successfully generated.
 *      ER_FAIL otherwise
 *      Other error status.
 */
QStatus Crypto_ECC_GenerateSharedSecret(const ECCPublicKey* peerPublicKey, const ECCPrivateKey* privateKey, ECCSecretOldEncoding* secret)
{

    bool derive_rv;
    affine_point_t localSecret;
    affine_point_t pub;
    bigval_t pk;

    pub.infinity = 0;
    binary_to_bigval(peerPublicKey->GetX(), &pub.x, peerPublicKey->GetCoordinateSize());
    binary_to_bigval(peerPublicKey->GetY(), &pub.y, peerPublicKey->GetCoordinateSize());
    binary_to_bigval(privateKey->GetD(), &pk, privateKey->GetDSize());
    derive_rv = ECDH_derive_pt(&localSecret, &pk, &pub);
    if (!derive_rv) {
        return ER_FAIL;  /* bad */
    }
    U32ArrayToU8BeArray((const uint32_t*)&localSecret, U32_AFFINEPOINT_SZ, (uint8_t*)secret);
    return ER_OK;
}

/*
 * Not a general-purpose implementation of REDP-1 from IEEE 1363.
 * Only used in AllJoyn to derive two basepoints, from the fixed constants
 * "ALLJOYN-ECSPEKE-1" and "ALLJOYN-ECSPEKE-2"
 * pi is not treated as a secret value.
 * This function is not constant-time.
 */
QStatus ec_REDP1(const uint8_t* pi, size_t len, ecpoint_t* Q, ec_t* curve)
{
    QStatus status = ER_OK;
    Crypto_SHA256 hash;
    uint8_t digest_i1[Crypto_SHA256::DIGEST_SIZE];
    uint8_t bytes_O3[Crypto_SHA256::DIGEST_SIZE];
    digit256_t x, alpha, beta;
    digit256_t tmp;
    digit_t temps[P256_TEMPS];
    int mu, carry, i;
    const digit256_tc P256_A = { 0xFFFFFFFFFFFFFFFCULL, 0x00000000FFFFFFFFULL, 0x0000000000000000ULL, 0xFFFFFFFF00000001ULL };
    const digit256_tc P256_B = { 0x3BCE3C3E27D2604BULL, 0x651D06B0CC53B0F6ULL, 0xB3EBBD55769886BCULL, 0x5AC635D8AA3A93E7ULL };

    /* Steps and notation follow IEEE 1363.2 Section 8.2.17 "[EC]REDP-1" */

    /* Hash pi to an octet string --  Step (a)*/
    status = hash.Init();
    if (status != ER_OK) {
        goto Exit;
    }
    status = hash.Update(pi, len);
    if (status != ER_OK) {
        goto Exit;
    }
    status = hash.GetDigest(digest_i1);
    if (status != ER_OK) {
        goto Exit;
    }


    while (1) {
        /* mu is rightmost bit of digest_i1 */
        mu = digest_i1[sizeof(digest_i1) - 1] % 2;

        /* Hash the hash -- Steps (b), (c), (d). */
        status = hash.Init();
        if (status != ER_OK) {
            goto Exit;
        }
        status = hash.Update(digest_i1, sizeof(digest_i1));
        if (status != ER_OK) {
            goto Exit;
        }
        status = hash.GetDigest(bytes_O3);
        if (status != ER_OK) {
            goto Exit;
        }

        /* Convert octets O3 to the field element x -- Step (e) */
        fpimport_p256(bytes_O3, x, temps, true);

        /* Compute alpha = x^3 + a*x + b (mod p)  */
        fpmul_p256(x, x, alpha, temps);                     /* alpha = x^2 */
        fpmul_p256(alpha, x, alpha, temps);                 /* alpha = x^3 */
        fpmul_p256(x, P256_A, tmp, temps);                  /* tmp = a*x */
        fpadd_p256(alpha, tmp, alpha);                      /* alpha = x^3 + a*x */
        fpadd_p256(alpha, P256_B, alpha);                   /* alpha = x^3 + a*x + b */

        /* Compute beta = a sqrt of alpha, if possible, if not begin a new iteration. */
        if (fpissquare_p256(alpha, temps)) {
            fpsqrt_p256(alpha, beta, temps);
        } else {
            /* Increment digest_i1 (as a big endian integer) then start a new iteration */
            carry = 1;
            for (i = sizeof(digest_i1) - 1; i >= 0; i--) {
                digest_i1[i] += carry;
                carry = (digest_i1[i] == 0);
            }

            if (carry) {
                /* It's overflown sizeof(digest_i1), fail. The probability of
                 * this occuring is negligible.
                 */
                status = ER_CRYPTO_ERROR;
                goto Exit;
            }
            continue;
        }

        if (mu) {
            fpneg_p256(beta);
        }

        /* Output (x,beta) */
        memcpy(Q->x, x, sizeof(digit256_t));
        memcpy(Q->y, beta, sizeof(digit256_t));
        break;
    }

    /* Make sure the point is valid, and is not the identity. */
    if (!ecpoint_validation(Q, curve)) {
        status = ER_CRYPTO_ERROR;
        goto Exit;
    }

Exit:
    /* Nothing to zero since inputs are public. */
    return status;
}

/* Computes R = Q1*Q2^pi */
QStatus ec_REDP2(const uint8_t pi[sizeof(digit256_t)], const ecpoint_t* Q1, const ecpoint_t* Q2, ecpoint_t* R, ec_t* curve)
{
    digit256_t t;
    digit_t temps[P256_TEMPS];
    QStatus status = ER_OK;

    fpimport_p256(pi, t, temps, true);
    status = ec_scalarmul(Q2, t, R, curve);             /* R = Q2^t*/
    ec_add(R, Q1, curve);                               /* R = Q1*Q2^t*/

    fpzero_p256(t);
    ClearMemory(temps, P256_TEMPS * sizeof(digit_t));

    return status;
}

/*
 * Get the two precomputed points
 * Q1 = REDP-1(ALLJOYN-ECSPEKE-1), Q2 = REDP-1(ALLJOYN-ECSPEKE-2).
 */
void ec_get_REDP_basepoints(ecpoint_t* Q1, ecpoint_t* Q2, curveid_t curveid)
{
    const digit256_tc x1 = { 0x9F011EB0E927BBB7ULL, 0xDCD485337A6C1035ULL, 0x0AF630115AA734C0ULL, 0xE7F425D4C27D2BA1ULL };
    const digit256_tc y1 = { 0xDD836A9DF0702B55ULL, 0x8A4AE230F7C50D50ULL, 0x4115DB75D35208F6ULL, 0x8B4ADF4EBD690598ULL };
    const digit256_tc x2 = { 0x4CEC1D03497217AAULL, 0x966C293CD3634462ULL, 0xE4E36BBB81CD843DULL, 0xF9F2EF394FCB375EULL };
    const digit256_tc y2 = { 0x40D6ACB2274CCFC2ULL, 0x5EAAF49A32B58CFAULL, 0x77999C42D8DDAB41ULL, 0xF5EFE6B53FF34102ULL };

    QCC_ASSERT(curveid == NISTP256r1);
    QCC_UNUSED(curveid);        /* unused in release builds */

    fpcopy_p256(x1, Q1->x);
    fpcopy_p256(y1, Q1->y);

    fpcopy_p256(x2, Q2->x);
    fpcopy_p256(y2, Q2->y);
}

static QStatus GenerateSPEKEKeyPair_inner(ecpoint_t* publicKey, digit256_t privateKey, const uint8_t* pw, const size_t pwLen, const GUID128 clientGUID, const GUID128 serviceGUID)
{
    QStatus status;
    Crypto_SHA256 hash;
    uint8_t digest[Crypto_SHA256::DIGEST_SIZE];
    digit_t temps[P256_TEMPS];
    ecpoint_t Q1, Q2;               /* Base points for REDP-2. */
    ecpoint_t B;                    /* Base point for ECDH, derived from pw. */
    ec_t curve;

    if (pw == NULL || pwLen == 0) {
        return ER_CRYPTO_ILLEGAL_PARAMETERS;
    }

    status = ec_getcurve(&curve, NISTP256r1);
    if (status != ER_OK) {
        goto Exit;
    }

    status = hash.Init();
    if (status != ER_OK) {
        goto Exit;
    }
    status = hash.Update(pw, pwLen);
    if (status != ER_OK) {
        goto Exit;
    }
    status = hash.Update(clientGUID.GetBytes(), GUID128::SIZE);
    if (status != ER_OK) {
        goto Exit;
    }
    status = hash.Update(serviceGUID.GetBytes(), GUID128::SIZE);
    if (status != ER_OK) {
        goto Exit;
    }
    status = hash.GetDigest(digest);
    if (status != ER_OK) {
        goto Exit;
    }

    /* Compute basepoint B for keypair. */
    ec_get_REDP_basepoints(&Q1, &Q2, curve.curveid);
    status = ec_REDP2(digest, &Q1, &Q2, &B, &curve);
    if (status != ER_OK) {
        goto Exit;
    }

    /* Compute private key. */
    do {
        status = Crypto_GetRandomBytes((uint8_t*)privateKey, sizeof(digit256_t));
        if (status != ER_OK) {
            goto Exit;
        }
    } while (!validate_256(privateKey, curve.order));

    status = ec_scalarmul(&B, privateKey, publicKey, &curve);                /* Public key publicKey = B^r */

Exit:
    fpzero_p256(B.x);
    fpzero_p256(B.y);
    ClearMemory(temps, P256_TEMPS * sizeof(digit_t));
    ClearMemory(digest, Crypto_SHA256::DIGEST_SIZE);
    ec_freecurve(&curve);

    return status;
}

QStatus Crypto_ECC_GenerateSPEKEKeyPair(ECCPublicKey* publicKey, ECCPrivateKey* privateKey, const uint8_t* pw, const size_t pwLen, const GUID128 clientGUID, const GUID128 serviceGUID)
{
    QStatus status;
    ecpoint_t pub;
    digit256_t priv;
    affine_point_t pubTemp;
    bigval_t privTemp;

    if (publicKey == NULL || privateKey == NULL) {
        return ER_CRYPTO_ILLEGAL_PARAMETERS;
    }

    status = GenerateSPEKEKeyPair_inner(&pub, priv, pw, pwLen, clientGUID, serviceGUID);
    if (status != ER_OK) {
        return status;
    }

    /* Convert pub to ecc_publickey then ECCPublicKey */
    const size_t coordinateSize = publicKey->GetCoordinateSize();
    uint8_t* X = new uint8_t[coordinateSize];
    uint8_t* Y = new uint8_t[coordinateSize];
    digit256_to_bigval(pub.x, &(pubTemp.x));
    digit256_to_bigval(pub.y, &(pubTemp.y));
    bigval_to_binary(&pubTemp.x, X, coordinateSize);
    bigval_to_binary(&pubTemp.y, Y, coordinateSize);

    /* Convert priv to ecc_privatekey then ECCPrivateKey */
    uint8_t* D = new uint8_t[privateKey->GetDSize()];
    digit256_to_bigval(priv, &privTemp);
    bigval_to_binary(&privTemp, D, privateKey->GetDSize());

    status = publicKey->Import(X, coordinateSize, Y, coordinateSize);
    if (ER_OK == status) {
        status = privateKey->Import(D, privateKey->GetDSize());
    }

    delete[] X;
    delete[] Y;
    ClearMemory(&privTemp, BIGLEN);
    qcc::ClearMemory(D, privateKey->GetDSize());
    delete[] D;

    return status;
}

} /*namespace qcc*/