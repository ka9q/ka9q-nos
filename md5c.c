/* MD5.C - RSA Data Security, Inc., MD5 message-digest algorithm */

#include <mem.h>

#ifdef	CPU386
#pragma inline
#endif

#ifndef	NULL
#define	NULL	(void *)0
#endif

/* This version taken from RSAREF 2.0 and modified for use
 * under Borland C++ with DOS by Phil Karn, KA9Q, September 1994.
 * The changes include the replacement of MD5_memset() and MD5_memcpy()
 * with calls to memset() and memcpy(), and the inclusion of an optional
 * 386/486 inline assembler version of MD5Transform().
 *
 * The 386/486 assembler version of MD5Transform() included here was previously
 * released on 22 Feb 1992 with an earlier version of md5.c from RSA;
 * it has been modified here to suit the different
 * internal calling conventions subsequently adopted by RSA.
 */

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
   documentation and/or software.  
 */

/*#include "global.h" */
#include "md5.h"

/* Constants for MD5Transform routine.
 */
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

static void MD5Transform PROTO_LIST ((UINT4 [4], unsigned char [64]));
static void Encode PROTO_LIST
  ((unsigned char *, UINT4 *, unsigned int));
#ifndef	CPU386
static void Decode PROTO_LIST
  ((UINT4 *, unsigned char *, unsigned int));
#endif

static unsigned char PADDING[64] = {
  0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#ifndef	CPU386

/* F, G, H and I are basic MD5 functions.
 */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

/* ROTATE_LEFT rotates x left n bits.
 */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
   Rotation is separate from addition to prevent recomputation.
 */
#define FF(a, b, c, d, x, s, ac) { \
    (a) += F ((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT ((a), (s)); \
    (a) += (b); \
  }
#define GG(a, b, c, d, x, s, ac) { \
    (a) += G ((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT ((a), (s)); \
    (a) += (b); \
  }
#define HH(a, b, c, d, x, s, ac) { \
    (a) += H ((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT ((a), (s)); \
    (a) += (b); \
  }
#define II(a, b, c, d, x, s, ac) { \
    (a) += I ((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT ((a), (s)); \
    (a) += (b); \
  }
#endif

/* MD5 initialization. Begins an MD5 operation, writing a new context.
 */
void MD5Init (context)
MD5_CTX *context;                                        /* context */
{
  context->count[0] = context->count[1] = 0;

  /* Load magic initialization constants.
   */
  context->state[0] = 0x67452301;
  context->state[1] = 0xefcdab89;
  context->state[2] = 0x98badcfe;
  context->state[3] = 0x10325476;
}

/* MD5 block update operation. Continues an MD5 message-digest
     operation, processing another message block, and updating the
     context.
 * Rewritten to better optimize the common case when MD5Update()
 * is only called with multiples of 64 bytes (e.g., when hashing
 * a stdio buffer) - PRK
 */
void MD5Update (context, input, inputLen)
MD5_CTX *context;                                        /* context */
unsigned char *input;                                /* input block */
unsigned int inputLen;                     /* length of input block */
{
  unsigned int i, index, partLen;

  /* Compute number of bytes mod 64 */
  index = (unsigned int)((context->count[0] >> 3) & 0x3F);

  /* Update number of bits */
  if ((context->count[0] += ((UINT4)inputLen << 3))
      < ((UINT4)inputLen << 3))
    context->count[1]++;
  context->count[1] += ((UINT4)inputLen >> 29);
  
  if(index != 0){
	/* Complete the partial buffer first */
	partLen = 64 - index;	/* room left in buffer */
	i = inputLen > partLen ? partLen : inputLen;	/* min(room,data) */
	memcpy(&context->buffer[index],input,i);
	input += i;
	inputLen -= i;
	if(i == partLen){
		/* Buffer now full, hash it */
		MD5Transform(context->state,context->buffer);
	} else
		return;	/* Not yet full */
  }	
  /* Now do as many 64 byte chunks direct from the input as possible */
  while(inputLen >= 64){
	MD5Transform(context->state,input);
	input += 64;
	inputLen -= 64;
  }
  /* Now buffer any remaining input */
  if(inputLen > 0)
	memcpy(context->buffer,input,inputLen);
}

/* MD5 finalization. Ends an MD5 message-digest operation, writing the
     the message digest and zeroizing the context.
 */
void MD5Final (digest, context)
unsigned char digest[16];                         /* message digest */
MD5_CTX *context;                                       /* context */
{
  unsigned char bits[8];
  unsigned int index, padLen;

  /* Save number of bits */
  Encode (bits, context->count, 8);

  /* Pad out to 56 mod 64.
   */
  index = (unsigned int)((context->count[0] >> 3) & 0x3f);
  padLen = (index < 56) ? (56 - index) : (120 - index);
  MD5Update (context, PADDING, padLen);
  
  /* Append length (before padding) */
  MD5Update (context, bits, 8);

  if(digest != NULL){	/* Bill Simpson's change to simplify AH */
	  /* Store state in digest */
	  Encode (digest, context->state, 16);
  
	  /* Zeroize sensitive information. */
	  memset ((POINTER)context, 0, sizeof (*context));
  }
}

#ifndef	CPU386	/* Not used; an asm version is available for the 386/486 */

/* MD5 basic transformation. Transforms state based on block.
 */
static void MD5Transform (state, block)
UINT4 state[4];
unsigned char block[64];
{
  UINT4 a = state[0], b = state[1], c = state[2], d = state[3], x[16];
  
  Decode (x, block, 64);

  /* Round 1 */
  FF (a, b, c, d, x[ 0], S11, 0xd76aa478); /* 1 */
  FF (d, a, b, c, x[ 1], S12, 0xe8c7b756); /* 2 */
  FF (c, d, a, b, x[ 2], S13, 0x242070db); /* 3 */
  FF (b, c, d, a, x[ 3], S14, 0xc1bdceee); /* 4 */
  FF (a, b, c, d, x[ 4], S11, 0xf57c0faf); /* 5 */
  FF (d, a, b, c, x[ 5], S12, 0x4787c62a); /* 6 */
  FF (c, d, a, b, x[ 6], S13, 0xa8304613); /* 7 */
  FF (b, c, d, a, x[ 7], S14, 0xfd469501); /* 8 */
  FF (a, b, c, d, x[ 8], S11, 0x698098d8); /* 9 */
  FF (d, a, b, c, x[ 9], S12, 0x8b44f7af); /* 10 */
  FF (c, d, a, b, x[10], S13, 0xffff5bb1); /* 11 */
  FF (b, c, d, a, x[11], S14, 0x895cd7be); /* 12 */
  FF (a, b, c, d, x[12], S11, 0x6b901122); /* 13 */
  FF (d, a, b, c, x[13], S12, 0xfd987193); /* 14 */
  FF (c, d, a, b, x[14], S13, 0xa679438e); /* 15 */
  FF (b, c, d, a, x[15], S14, 0x49b40821); /* 16 */

  /* Round 2 */
  GG (a, b, c, d, x[ 1], S21, 0xf61e2562); /* 17 */
  GG (d, a, b, c, x[ 6], S22, 0xc040b340); /* 18 */
  GG (c, d, a, b, x[11], S23, 0x265e5a51); /* 19 */
  GG (b, c, d, a, x[ 0], S24, 0xe9b6c7aa); /* 20 */
  GG (a, b, c, d, x[ 5], S21, 0xd62f105d); /* 21 */
  GG (d, a, b, c, x[10], S22,  0x2441453); /* 22 */
  GG (c, d, a, b, x[15], S23, 0xd8a1e681); /* 23 */
  GG (b, c, d, a, x[ 4], S24, 0xe7d3fbc8); /* 24 */
  GG (a, b, c, d, x[ 9], S21, 0x21e1cde6); /* 25 */
  GG (d, a, b, c, x[14], S22, 0xc33707d6); /* 26 */
  GG (c, d, a, b, x[ 3], S23, 0xf4d50d87); /* 27 */
  GG (b, c, d, a, x[ 8], S24, 0x455a14ed); /* 28 */
  GG (a, b, c, d, x[13], S21, 0xa9e3e905); /* 29 */
  GG (d, a, b, c, x[ 2], S22, 0xfcefa3f8); /* 30 */
  GG (c, d, a, b, x[ 7], S23, 0x676f02d9); /* 31 */
  GG (b, c, d, a, x[12], S24, 0x8d2a4c8a); /* 32 */

  /* Round 3 */
  HH (a, b, c, d, x[ 5], S31, 0xfffa3942); /* 33 */
  HH (d, a, b, c, x[ 8], S32, 0x8771f681); /* 34 */
  HH (c, d, a, b, x[11], S33, 0x6d9d6122); /* 35 */
  HH (b, c, d, a, x[14], S34, 0xfde5380c); /* 36 */
  HH (a, b, c, d, x[ 1], S31, 0xa4beea44); /* 37 */
  HH (d, a, b, c, x[ 4], S32, 0x4bdecfa9); /* 38 */
  HH (c, d, a, b, x[ 7], S33, 0xf6bb4b60); /* 39 */
  HH (b, c, d, a, x[10], S34, 0xbebfbc70); /* 40 */
  HH (a, b, c, d, x[13], S31, 0x289b7ec6); /* 41 */
  HH (d, a, b, c, x[ 0], S32, 0xeaa127fa); /* 42 */
  HH (c, d, a, b, x[ 3], S33, 0xd4ef3085); /* 43 */
  HH (b, c, d, a, x[ 6], S34,  0x4881d05); /* 44 */
  HH (a, b, c, d, x[ 9], S31, 0xd9d4d039); /* 45 */
  HH (d, a, b, c, x[12], S32, 0xe6db99e5); /* 46 */
  HH (c, d, a, b, x[15], S33, 0x1fa27cf8); /* 47 */
  HH (b, c, d, a, x[ 2], S34, 0xc4ac5665); /* 48 */

  /* Round 4 */
  II (a, b, c, d, x[ 0], S41, 0xf4292244); /* 49 */
  II (d, a, b, c, x[ 7], S42, 0x432aff97); /* 50 */
  II (c, d, a, b, x[14], S43, 0xab9423a7); /* 51 */
  II (b, c, d, a, x[ 5], S44, 0xfc93a039); /* 52 */
  II (a, b, c, d, x[12], S41, 0x655b59c3); /* 53 */
  II (d, a, b, c, x[ 3], S42, 0x8f0ccc92); /* 54 */
  II (c, d, a, b, x[10], S43, 0xffeff47d); /* 55 */
  II (b, c, d, a, x[ 1], S44, 0x85845dd1); /* 56 */
  II (a, b, c, d, x[ 8], S41, 0x6fa87e4f); /* 57 */
  II (d, a, b, c, x[15], S42, 0xfe2ce6e0); /* 58 */
  II (c, d, a, b, x[ 6], S43, 0xa3014314); /* 59 */
  II (b, c, d, a, x[13], S44, 0x4e0811a1); /* 60 */
  II (a, b, c, d, x[ 4], S41, 0xf7537e82); /* 61 */
  II (d, a, b, c, x[11], S42, 0xbd3af235); /* 62 */
  II (c, d, a, b, x[ 2], S43, 0x2ad7d2bb); /* 63 */
  II (b, c, d, a, x[ 9], S44, 0xeb86d391); /* 64 */

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  
  /* Zeroize sensitive information.
   */
  memset ((POINTER)x, 0, sizeof (x));
}

#endif

/* Encodes input (UINT4) into output (unsigned char). Assumes len is
     a multiple of 4.
 */
static void Encode (output, input, len)
unsigned char *output;
UINT4 *input;
unsigned int len;
{
  unsigned int i, j;

  for (i = 0, j = 0; j < len; i++, j += 4) {
    output[j] = (unsigned char)(input[i] & 0xff);
    output[j+1] = (unsigned char)((input[i] >> 8) & 0xff);
    output[j+2] = (unsigned char)((input[i] >> 16) & 0xff);
    output[j+3] = (unsigned char)((input[i] >> 24) & 0xff);
  }
}

#ifndef	CPU386	/* Not needed in assembler version */
/* Decodes input (unsigned char) into output (UINT4). Assumes len is
     a multiple of 4.
 */
static void Decode (output, input, len)
UINT4 *output;
unsigned char *input;
unsigned int len;
{
  unsigned int i, j;

  for (i = 0, j = 0; j < len; i++, j += 4)
    output[i] = ((UINT4)input[j]) | (((UINT4)input[j+1]) << 8) |
      (((UINT4)input[j+2]) << 16) | (((UINT4)input[j+3]) << 24);
}
#else	/* CPU386 */
/* Fast 386 Borland C inline assembler version of the transform() function
 * from the RSA Data Security, Inc, MD5 Message Digest Algorithm.
 *
 * This version uses native 32 bit registers, so it needs a 386 or 486 CPU.
 * It also assumes large model (-ml)
 *
 * Because this function does *lots* of 32-bit operations, this version is
 * MUCH faster than the reference C version compiled with a garden-
 * variety 16-bit MS-DOS C compiler.
 *
 * Originally written and placed into the public domain on
 * 22 February 1992 by Phil Karn, KA9Q
 *
 * Updated 1 Sept 1994 by Phil Karn to match newer version of MD5.C released
 * in the RSAREF package. Also changed some register assignments to get rid
 * of the segment override prefixes on memory references
 */


/* Code sequence common to all four rounds.
 * evaluates a = b + (a + edi + x + t) <<< s
 * where it is assumed a and b are registers, x is a memory location,
 * edi is the edi register, and s and t are integer constants
 */
#define	COM(a,b,x,s,t)\
	lea a,t[a+edi];\
	add a,x;\
	rol a,s;\
	add a,b;

/* Round 1 functions */
/* edi = F(x,y,z) = (x & y) | (~x & z) */
#define	F(x,y,z)\
	mov edi,x;\
	and edi,y;\
	mov ebp,x;\
	not ebp;\
	and ebp,z;\
	or edi,ebp

/* a = b + ((a + F(b,c,d) + x + t) <<< s); */
#define	FF(a,b,c,d,x,s,t)\
	F(b,c,d);\
	COM(a,b,x,s,t)

/* Round 2 functions */
/* edi = G(x,y,z) = F(z,x,y) = (x & z) | (y & ~z) */
#define	G(x,y,z) F(z,x,y)

/* a = b + ((a + G(b,c,d) + x + t) <<< s) */
#define	GG(a,b,c,d,x,s,t)\
	G(b,c,d);\
	COM(a,b,x,s,t)

/* Round 3 functions */
/* edi = H(x,y,z) = x ^ y ^ z */
#define	H(x,y,z)\
	mov edi,x;\
	xor edi,y;\
	xor edi,z

/* a = b + ((a + H(b,c,d) + x + t) <<< s) */
#define	HH(a,b,c,d,x,s,t)\
	H(b,c,d);\
	COM(a,b,x,s,t)

/* Round 4 functions */
/* edi = I(x,y,z) = y ^ (x | ~z) */
#define	I(x,y,z)\
	mov edi,z;\
	not edi;\
	or edi,x;\
	xor edi,y

/* a = b + ((a + I(b,c,d) + x + t) <<< s) */
#define	II(a,b,c,d,x,s,t)\
	I(b,c,d);\
	COM(a,b,x,s,t)

/* Register assignments */
#define	A	eax
#define	B	ebx
#define	C	ecx
#define	D	edx

static void
MD5Transform (state, block)
UINT4 state[4];
unsigned char block[64];
{
	asm {

	/* Save caller's registers */
	push si;
	push edi;
	push ds;

	lds si,state;	/* Read input state */

	mov A,dword ptr si[0*4];	/* A = state[0] */
	mov B,dword ptr si[1*4];	/* B = state[1] */
	mov C,dword ptr si[2*4];	/* C = state[2] */
	mov D,dword ptr si[3*4];	/* D = state[3] */

	lds si,block;	/* Set up for data block read */

	/* The FF macro uses ebp as scratch. This makes our args
	 * inaccessible until it is restored!
	 */
	push ebp;

	/* Round 1. The *4 factors in the subscripts to si account for the
	 * byte offsets of each long element in the input array. The input
	 * is actually a byte array, but we can treat it directly as a long
	 * array because MD5 is little-endian, like the 386/486.
	 *
	 * The only hazard is if the input buffer isn't 32-bit aligned,
	 * things will run a little more slowly.
	 */
	FF(A,B,C,D,si[ 0*4],S11,3614090360); /* 1 */
	FF(D,A,B,C,si[ 1*4],S12,3905402710); /* 2 */
	FF(C,D,A,B,si[ 2*4],S13, 606105819); /* 3 */
	FF(B,C,D,A,si[ 3*4],S14,3250441966); /* 4 */
	FF(A,B,C,D,si[ 4*4],S11,4118548399); /* 5 */
	FF(D,A,B,C,si[ 5*4],S12,1200080426); /* 6 */
	FF(C,D,A,B,si[ 6*4],S13,2821735955); /* 7 */
	FF(B,C,D,A,si[ 7*4],S14,4249261313); /* 8 */
	FF(A,B,C,D,si[ 8*4],S11,1770035416); /* 9 */
	FF(D,A,B,C,si[ 9*4],S12,2336552879); /* 10 */
	FF(C,D,A,B,si[10*4],S13,4294925233); /* 11 */
	FF(B,C,D,A,si[11*4],S14,2304563134); /* 12 */
	FF(A,B,C,D,si[12*4],S11,1804603682); /* 13 */
	FF(D,A,B,C,si[13*4],S12,4254626195); /* 14 */
	FF(C,D,A,B,si[14*4],S13,2792965006); /* 15 */
	FF(B,C,D,A,si[15*4],S14,1236535329); /* 16 */

	/* Round 2 */
	GG(A,B,C,D,si[ 1*4],S21,4129170786); /* 17 */
	GG(D,A,B,C,si[ 6*4],S22,3225465664); /* 18 */
	GG(C,D,A,B,si[11*4],S23, 643717713); /* 19 */
	GG(B,C,D,A,si[ 0*4],S24,3921069994); /* 20 */
	GG(A,B,C,D,si[ 5*4],S21,3593408605); /* 21 */
	GG(D,A,B,C,si[10*4],S22,  38016083); /* 22 */
	GG(C,D,A,B,si[15*4],S23,3634488961); /* 23 */
	GG(B,C,D,A,si[ 4*4],S24,3889429448); /* 24 */
	GG(A,B,C,D,si[ 9*4],S21, 568446438); /* 25 */
	GG(D,A,B,C,si[14*4],S22,3275163606); /* 26 */
	GG(C,D,A,B,si[ 3*4],S23,4107603335); /* 27 */
	GG(B,C,D,A,si[ 8*4],S24,1163531501); /* 28 */
	GG(A,B,C,D,si[13*4],S21,2850285829); /* 29 */
	GG(D,A,B,C,si[ 2*4],S22,4243563512); /* 30 */
	GG(C,D,A,B,si[ 7*4],S23,1735328473); /* 31 */
	GG(B,C,D,A,si[12*4],S24,2368359562); /* 32 */

	/* Round 3 */
	HH(A,B,C,D,si[ 5*4],S31,4294588738); /* 33 */
	HH(D,A,B,C,si[ 8*4],S32,2272392833); /* 34 */
	HH(C,D,A,B,si[11*4],S33,1839030562); /* 35 */
	HH(B,C,D,A,si[14*4],S34,4259657740); /* 36 */
	HH(A,B,C,D,si[ 1*4],S31,2763975236); /* 37 */
	HH(D,A,B,C,si[ 4*4],S32,1272893353); /* 38 */
	HH(C,D,A,B,si[ 7*4],S33,4139469664); /* 39 */
	HH(B,C,D,A,si[10*4],S34,3200236656); /* 40 */
	HH(A,B,C,D,si[13*4],S31, 681279174); /* 41 */
	HH(D,A,B,C,si[ 0*4],S32,3936430074); /* 42 */
	HH(C,D,A,B,si[ 3*4],S33,3572445317); /* 43 */
	HH(B,C,D,A,si[ 6*4],S34,  76029189); /* 44 */
	HH(A,B,C,D,si[ 9*4],S31,3654602809); /* 45 */
	HH(D,A,B,C,si[12*4],S32,3873151461); /* 46 */
	HH(C,D,A,B,si[15*4],S33, 530742520); /* 47 */
	HH(B,C,D,A,si[ 2*4],S34,3299628645); /* 48 */

	/* Round 4 */
	II(A,B,C,D,si[ 0*4],S41,4096336452); /* 49 */
	II(D,A,B,C,si[ 7*4],S42,1126891415); /* 50 */
	II(C,D,A,B,si[14*4],S43,2878612391); /* 51 */
	II(B,C,D,A,si[ 5*4],S44,4237533241); /* 52 */
	II(A,B,C,D,si[12*4],S41,1700485571); /* 53 */
	II(D,A,B,C,si[ 3*4],S42,2399980690); /* 54 */
	II(C,D,A,B,si[10*4],S43,4293915773); /* 55 */
	II(B,C,D,A,si[ 1*4],S44,2240044497); /* 56 */
	II(A,B,C,D,si[ 8*4],S41,1873313359); /* 57 */
	II(D,A,B,C,si[15*4],S42,4264355552); /* 58 */
	II(C,D,A,B,si[ 6*4],S43,2734768916); /* 59 */
	II(B,C,D,A,si[13*4],S44,1309151649); /* 60 */
	II(A,B,C,D,si[ 4*4],S41,4149444226); /* 61 */
	II(D,A,B,C,si[11*4],S42,3174756917); /* 62 */
	II(C,D,A,B,si[ 2*4],S43, 718787259); /* 63 */
	II(B,C,D,A,si[ 9*4],S44,3951481745); /* 64 */

	pop ebp;		/* We can address our args again */

	lds si,state;		/* Update the caller's state */
	add dword ptr si[0*4],A;	/* state[0] += A */
	add dword ptr si[1*4],B;	/* state[1] += B */
	add dword ptr si[2*4],C;	/* state[2] += C */
	add dword ptr si[3*4],D;	/* state[3] += D */

	/* Restore caller's registers */
	pop ds;
	pop edi;
	pop si;
	}
}
#endif	/* CPU386 */

