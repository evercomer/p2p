/* MD5.H - header file for MD5C.C */
/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
rights reserved.
License to copy and use this software is granted provided that it
is identified as the "RSA Data Security, Inc. MD5 Message-Digest
Algorithm" in all material mentioning or referencing this software
or this function.
License is also granted to make and use derivative works provided
that such works are identified as "derived from the RSA Data
Security, Inc. MD5 Message-Digest Algorithm" in all material
mentioning or referencing the derived work.
RSA Data Security, Inc. makes no representations concerning either
the merchantability of this software or the suitability of this
software for any particular purpose. It is provided "as is"
without express or implied warranty of any kind.
These notices must be retained in any copies of any part of this
documentation and/or software. *//* MD5 context. */

#include "platform_adpt.h"
#include "global.h"
#include <string.h>

typedef struct {
  UINT4 state[4];                                   /* state (ABCD) */
  UINT4 count[2];        /* number of bits, modulo 2^64 (lsb first) */
  unsigned char buffer[64];                         /* input buffer */
} MD5_CTX;

/* Constants for MD5Transform routine. */
#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

#ifdef __cplusplus
extern "C" {
#endif

void MD5Init(MD5_CTX *context);
void MD5Update (MD5_CTX *context, unsigned char *input, unsigned int inputLen);
void MD5Final (unsigned char* digest, MD5_CTX *context);

#ifdef __cplusplus
}
#endif

#define MD_CTX MD5_CTX
#define MDInit MD5Init
#define MDUpdate MD5Update
#define MDFinal MD5Final

BOOL MDString (unsigned char* string, unsigned int nLen, unsigned char* digest);

/* F, G, H and I are basic MD5 functions. */

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))
/* ROTATE_LEFT rotates x left n bits. */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))
/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
Rotation is separate from addition to prevent recomputation. */
#define FF(a, b, c, d, x, s, ac) { (a) += F ((b), (c), (d)) + (x) + (UINT4)(ac);  (a) = ROTATE_LEFT ((a), (s)); (a) += (b);  }
#define GG(a, b, c, d, x, s, ac) { (a) += G ((b), (c), (d)) + (x) + (UINT4)(ac);  (a) = ROTATE_LEFT ((a), (s)); (a) += (b);  }
#define HH(a, b, c, d, x, s, ac) { (a) += H ((b), (c), (d)) + (x) + (UINT4)(ac);  (a) = ROTATE_LEFT ((a), (s)); (a) += (b);  }
#define II(a, b, c, d, x, s, ac) { (a) += I ((b), (c), (d)) + (x) + (UINT4)(ac);  (a) = ROTATE_LEFT ((a), (s)); (a) += (b);  }
