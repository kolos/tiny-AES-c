/*

This is an implementation of the AES algorithm, specifically ECB, CTR and CBC mode.
Block size can be chosen in aes.h - available choices are AES128, AES192, AES256.

The implementation is verified against the test vectors in:
  National Institute of Standards and Technology Special Publication 800-38A 2001 ED

ECB-AES128
----------

  plain-text:
    6bc1bee22e409f96e93d7e117393172a
    ae2d8a571e03ac9c9eb76fac45af8e51
    30c81c46a35ce411e5fbc1191a0a52ef
    f69f2445df4f9b17ad2b417be66c3710

  key:
    2b7e151628aed2a6abf7158809cf4f3c

  resulting cipher
    3ad77bb40d7a3660a89ecaf32466ef97 
    f5d3d58503b9699de785895a96fdbaaf 
    43b1cd7f598ece23881b00e3ed030688 
    7b0c785e27e8ad3f8223207104725dd4 


NOTE:   String length must be evenly divisible by 16byte (str_len % 16 == 0)
        You should pad the end of the string with zeros if this is not the case.
        For AES192/256 the key size is proportionally larger.

*/


/*****************************************************************************/
/* Includes:                                                                 */
/*****************************************************************************/
#include <string.h> // CBC mode, for memset
#include "aes.h"

/*****************************************************************************/
/* Defines:                                                                  */
/*****************************************************************************/
#define MASK32_BYTE0  0x000000FF
#define MASK32_BYTE1  0x0000FF00
#define MASK32_BYTE2  0x00FF0000
#define MASK32_BYTE3  0xFF000000

#define MASK64_BYTE0  0x00000000000000FF
#define MASK64_BYTE1  0x000000000000FF00
#define MASK64_BYTE2  0x0000000000FF0000
#define MASK64_BYTE3  0x00000000FF000000
#define MASK64_BYTE4  0x000000FF00000000
#define MASK64_BYTE5  0x0000FF0000000000
#define MASK64_BYTE6  0x00FF000000000000
#define MASK64_BYTE7  0xFF00000000000000


#define INVMASK32_BYTE0  0xFFFFFF00
#define INVMASK32_BYTE1  0xFFFF00FF
#define INVMASK32_BYTE2  0xFF00FFFF
#define INVMASK32_BYTE3  0x00FFFFFF

#define INVMASK64_BYTE0  0xFFFFFFFFFFFFFF00
#define INVMASK64_BYTE1  0xFFFFFFFFFFFF00FF
#define INVMASK64_BYTE2  0xFFFFFFFFFF00FFFF
#define INVMASK64_BYTE3  0xFFFFFFFF00FFFFFF
#define INVMASK64_BYTE4  0xFFFFFF00FFFFFFFF
#define INVMASK64_BYTE5  0xFFFF00FFFFFFFFFF
#define INVMASK64_BYTE6  0xFF00FFFFFFFFFFFF
#define INVMASK64_BYTE7  0x00FFFFFFFFFFFFFF


#define OFS32_BYTE0   0
#define OFS32_BYTE1   8
#define OFS32_BYTE2   16
#define OFS32_BYTE3   24

#define OFS64_BYTE0   0
#define OFS64_BYTE1   8
#define OFS64_BYTE2   16
#define OFS64_BYTE3   24
#define OFS64_BYTE4   32
#define OFS64_BYTE5   40
#define OFS64_BYTE6   48
#define OFS64_BYTE7   56

// The number of columns comprising a state in AES. This is a constant in AES. Value=4
#define Nb 4

#if defined(AES256) && (AES256 == 1)
    #define Nk 8
    #define Nr 14
#elif defined(AES192) && (AES192 == 1)
    #define Nk 6
    #define Nr 12
#else
    #define Nk 4        // The number of 32 bit words in a key.
    #define Nr 10       // The number of rounds in AES Cipher.
#endif

// jcallan@github points out that declaring Multiply as a function 
// reduces code size considerably with the Keil ARM compiler.
// See this link for more information: https://github.com/kokke/tiny-AES-C/pull/3
#ifndef MULTIPLY_AS_A_FUNCTION
  #define MULTIPLY_AS_A_FUNCTION 0
#endif




/*****************************************************************************/
/* Private variables:                                                        */
/*****************************************************************************/
// state - array holding the intermediate results during decryption.
typedef union {
  uint8_t a[4][4];
  uint32_t i[4];
}__attribute__((packed)) state_t;

typedef union {
  uint8_t a[4];
  uint32_t i;
} helper_t;

// The lookup-tables are marked const so they can be placed in read-only storage instead of RAM
// The numbers below can be computed dynamically trading ROM for RAM - 
// This can be useful in (embedded) bootloader applications, where ROM is often limited.
static const uint8_t sbox[256] PROGMEM = {
  //0     1    2      3     4    5     6     7      8    9     A      B    C     D     E     F
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
  0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
  0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
  0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
  0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
  0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
  0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
  0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
  0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
  0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
  0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
  0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16 };

#if (defined(CBC) && CBC == 1) || (defined(ECB) && ECB == 1)
static const uint8_t rsbox[256] PROGMEM = {
  0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d };
#endif

// The round constant word array, Rcon[i], contains the values given by 
// x to the power (i-1) being powers of x (x is denoted as {02}) in the field GF(2^8)
static const uint8_t Rcon[11] PROGMEM = {
  0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36 };

/*
 * Jordan Goulder points out in PR #12 (https://github.com/kokke/tiny-AES-C/pull/12),
 * that you can remove most of the elements in the Rcon array, because they are unused.
 *
 * From Wikipedia's article on the Rijndael key schedule @ https://en.wikipedia.org/wiki/Rijndael_key_schedule#Rcon
 * 
 * "Only the first some of these constants are actually used – up to rcon[10] for AES-128 (as 11 round keys are needed), 
 *  up to rcon[8] for AES-192, up to rcon[7] for AES-256. rcon[0] is not used in AES algorithm."
 */


/*****************************************************************************/
/* Private functions:                                                        */
/*****************************************************************************/
/*
static uint8_t getSBoxValue(uint8_t num)
{
  return pgm_read_byte(sbox + (num));
}
*/
#define getSBoxValue(num) (pgm_read_byte(sbox + (num)))
#define getRconValue(num) (pgm_read_byte(Rcon + (num)))

// This function produces Nb(Nr+1) round keys. The round keys are used in each round to decrypt the states. 
static void KeyExpansion(roundKey_t* RoundKey, const uint8_t* Key)
{
  unsigned i, j, k;
  helper_t temp;
  
  // The first round key is the key itself.
  memcpy(RoundKey, Key, 4 * Nk);

  // All other round keys are found from the previous round keys.
  for (i = Nk; i < Nb * (Nr + 1); ++i)
  {
    {
      k = (i - 1);
      temp.i = RoundKey->i[k];
    }

    if (i % Nk == 0)
    {
      // This function shifts the 4 bytes in a word to the left once.
      // [a0,a1,a2,a3] becomes [a1,a2,a3,a0]

      // Function RotWord()
      {
        const uint8_t u8tmp = temp.a[0];
        temp.i >>= 8;
        temp.a[3] = u8tmp;
      }

      // SubWord() is a function that takes a four-byte input word and 
      // applies the S-box to each of the four bytes to produce an output word.

      // Function Subword()
      {
        temp.i = (getSBoxValue(temp.a[0]) << OFS32_BYTE0) |
                 (getSBoxValue(temp.a[1]) << OFS32_BYTE1) |
                 (getSBoxValue(temp.a[2]) << OFS32_BYTE2) |
                 (getSBoxValue(temp.a[3]) << OFS32_BYTE3);
      }

      temp.a[0] = temp.a[0] ^ getRconValue(i/Nk);
    }
#if defined(AES256) && (AES256 == 1)
    if (i % Nk == 4)
    {
      // Function Subword()
      {
        temp.i = (getSBoxValue(temp.a[0]) << OFS32_BYTE0) |
                 (getSBoxValue(temp.a[1]) << OFS32_BYTE1) |
                 (getSBoxValue(temp.a[2]) << OFS32_BYTE2) |
                 (getSBoxValue(temp.a[3]) << OFS32_BYTE3);
      }
    }
#endif
    j = i; k=(i - Nk);
    RoundKey->i[j] = RoundKey->i[k] ^ temp.i;
  }
}

void AES_init_ctx(struct AES_ctx* ctx, const uint8_t* key)
{
  KeyExpansion(ctx->RoundKey, key);
}
#if (defined(CBC) && (CBC == 1)) || (defined(CTR) && (CTR == 1))
void AES_init_ctx_iv(struct AES_ctx* ctx, const uint8_t* key, const uint8_t* iv)
{
  KeyExpansion(ctx->RoundKey, key);
  memcpy (ctx->Iv, iv, AES_BLOCKLEN);
}
void AES_ctx_set_iv(struct AES_ctx* ctx, const uint8_t* iv)
{
  memcpy (ctx->Iv, iv, AES_BLOCKLEN);
}
#endif

// This function adds the round key to state.
// The round key is added to the state by an XOR function.
static void AddRoundKey(uint8_t round, state_t* state, const roundKey_t* RoundKey)
{
  uint8_t i;
  for (i = 0; i < 4; ++i)
  {
    (*state).i[i] ^= RoundKey->i[(round * Nb) + i];
  }
}

// The SubBytes Function Substitutes the values in the
// state matrix with values in an S-box.
static void SubBytes(state_t* state)
{
  uint8_t i;
  for (i = 0; i < 4; i++)
  {
    helper_t tmp;
    tmp.i = (*state).i[i];

    (*state).i[i] = (getSBoxValue(tmp.a[0]) << OFS32_BYTE0) | 
                    (getSBoxValue(tmp.a[1]) << OFS32_BYTE1) |
                    (getSBoxValue(tmp.a[2]) << OFS32_BYTE2) |
                    (getSBoxValue(tmp.a[3]) << OFS32_BYTE3);
  }
}

// The ShiftRows() function shifts the rows in the state to the left.
// Each row is shifted with different offset.
// Offset = Row number. So the first row is not shifted.
static void ShiftRows(state_t* state)
{
  uint32_t line0 = (*state).i[0];
  uint32_t line1 = (*state).i[1];
  uint32_t line2 = (*state).i[2];
  uint32_t line3 = (*state).i[3];

  uint32_t temp;

  temp = line0 & MASK32_BYTE1;
  line0 &= INVMASK32_BYTE1;
  line0 |= line1 & MASK32_BYTE1;
  line1 &= INVMASK32_BYTE1;
  line1 |= line2 & MASK32_BYTE1;
  line2 &= INVMASK32_BYTE1;
  line2 |= line3 & MASK32_BYTE1;
  line3 &= INVMASK32_BYTE1;
  line3 |= temp;

  temp = line0 & MASK32_BYTE2;
  line0 &= INVMASK32_BYTE2;
  line0 |= line2 & MASK32_BYTE2;
  line2 &= INVMASK32_BYTE2;
  line2 |= temp;

  temp = line1 & MASK32_BYTE2;
  line1 &= INVMASK32_BYTE2;
  line1 |= line3 & MASK32_BYTE2;
  line3 &= INVMASK32_BYTE2;
  line3 |= temp;

  temp = line0 & MASK32_BYTE3;
  line0 &= INVMASK32_BYTE3;
  line0 |= line3 & MASK32_BYTE3;
  line3 &= INVMASK32_BYTE3;
  line3 |= line2 & MASK32_BYTE3;
  line2 &= INVMASK32_BYTE3;
  line2 |= line1 & MASK32_BYTE3;
  line1 &= INVMASK32_BYTE3;
  line1 |= temp;

  (*state).i[0] = line0;
  (*state).i[1] = line1;
  (*state).i[2] = line2;
  (*state).i[3] = line3;
}

static inline uint32_t xtime(uint32_t x)
{
  return ((x&0x7f7f7f7f)<<1)^(((x&0x80808080)>>7)*0x1b);
}

// MixColumns function mixes the columns of the state matrix
static void MixColumns(state_t* state)
{
  for (uint8_t i=0;i<4;i++) {
    uint32_t sp = (*state).i[i];

    (*state).i[i] = xtime((sp) ^ (((sp)>>8)|((sp)<<24))) ^
            (((sp)<<8)|((sp)>>24)) ^
            (((sp)<<16)|((sp)>>16)) ^ (((sp)<<24)|((sp)>>8));
  }
}

// Multiply is used to multiply numbers in the field GF(2^8)
// Note: The last call to xtime() is unneeded, but often ends up generating a smaller binary
//       The compiler seems to be able to vectorize the operation better this way.
//       See https://github.com/kokke/tiny-AES-c/pull/34
// #if MULTIPLY_AS_A_FUNCTION
static inline uint32_t Multiply(uint32_t x, uint32_t y)
{
    uint32_t xtimeX = xtime(x);
    uint32_t xtimeXX = xtime(xtimeX);
    uint32_t xtimeXXX = xtime(xtimeXX);

    return ((~((y & 1)-1) & x) ^
            (~((y>>1 & 1)-1) & xtimeX) ^
            (~((y>>2 & 1)-1) & xtimeXX) ^
            (~((y>>3 & 1)-1) & xtimeXXX)
#if defined(_MSC_VER) && defined(_M_AMD64)
            ^
       (~((y>>4 & 1)-1) & xtime(xtimeXXX))
#endif
    ); /* this last call to xtime() can be omitted */
}
#if (defined(CBC) && CBC == 1) || (defined(ECB) && ECB == 1)
/*
static uint8_t getSBoxInvert(uint8_t num)
{
  return pgm_read_byte(rsbox + (num));
}
*/
#define getSBoxInvert(num) (pgm_read_byte(rsbox + (num)))

// MixColumns function mixes the columns of the state matrix.
// The method used to multiply may be difficult to understand for the inexperienced.
// Please use the references to gain more information.
static void InvMixColumns(state_t* state)
{
  uint32_t spVal;
  uint32_t xtimeX;
  uint32_t xtimeXX;
  uint32_t xtimeXXX;
  uint32_t xtime_x9;
  uint32_t xtime_xb;
  uint32_t xtime_xd;
  uint32_t xtime_xe;

  for (uint8_t i=0; i<4; i++)
  {
    spVal = (*state).i[i];
    xtimeX = xtime(spVal);
    xtimeXX = xtime(xtimeX);
    xtimeXXX = xtime(xtimeXX);

    xtime_x9 = xtimeXXX ^ spVal;
    xtime_xb = xtimeXXX ^ xtimeX ^ spVal;
    xtime_xd = xtimeXXX ^ xtimeXX ^ spVal;
    xtime_xe = xtimeXXX ^ xtimeXX ^ xtimeX;

    uint32_t xtime_xb_r8 =  xtime_xb >> 8;
    uint32_t xtime_xd_r16 = xtime_xd >> 16;
    uint32_t xtime_x9_l8 =  xtime_x9 << 8;
    uint32_t xtime_xd_l16 = xtime_xd << 16;

    (*state).i[i] =
        /* byte 0:*/ (((xtime_xe         ^ xtime_xb_r8  ^ xtime_xd_r16 ^ (xtime_x9 >> 24)) & 0x000000ff) |
        /* byte 1:*/  ((xtime_x9_l8      ^ xtime_xe     ^ xtime_xb_r8  ^ xtime_xd_r16    ) & 0x0000ff00) |
        /* byte 2:*/  ((xtime_xd_l16     ^ xtime_x9_l8  ^ xtime_xe     ^ xtime_xb_r8     ) & 0x00ff0000) |
        /* byte 3:*/  (((xtime_xb << 24) ^ xtime_xd_l16 ^ xtime_x9_l8  ^ xtime_xe        ) & 0xff000000));
  }
}


// The SubBytes Function Substitutes the values in the
// state matrix with values in an S-box.
static void InvSubBytes(state_t* state)
{
  uint8_t i;
  for (i = 0; i < 4; i++)
  {
    helper_t tmp;
    tmp.i = (*state).i[i];

    (*state).i[i] = (getSBoxInvert(tmp.a[0]) << OFS32_BYTE0) |
                    (getSBoxInvert(tmp.a[1]) << OFS32_BYTE1) |
                    (getSBoxInvert(tmp.a[2]) << OFS32_BYTE2) |
                    (getSBoxInvert(tmp.a[3]) << OFS32_BYTE3);
  }
}

static void InvShiftRows(state_t* state)
{
  uint32_t line0 = (*state).i[0];
  uint32_t line1 = (*state).i[1];
  uint32_t line2 = (*state).i[2];
  uint32_t line3 = (*state).i[3];

  uint32_t temp;

  temp = line3 & MASK32_BYTE1;
  line3 &= INVMASK32_BYTE1;
  line3 |= line2 & MASK32_BYTE1;
  line2 &= INVMASK32_BYTE1;
  line2 |= line1 & MASK32_BYTE1;
  line1 &= INVMASK32_BYTE1;
  line1 |= line0 & MASK32_BYTE1;
  line0 &= INVMASK32_BYTE1;
  line0 |= temp;

  temp = line0 & MASK32_BYTE2;
  line0 &= INVMASK32_BYTE2;
  line0 |= line2 & MASK32_BYTE2;
  line2 &= INVMASK32_BYTE2;
  line2 |= temp;

  temp = line1 & MASK32_BYTE2;
  line1 &= INVMASK32_BYTE2;
  line1 |= line3 & MASK32_BYTE2;
  line3 &= INVMASK32_BYTE2;
  line3 |= temp;

  temp = line0 & MASK32_BYTE3;
  line0 &= INVMASK32_BYTE3;
  line0 |= line1 & MASK32_BYTE3;
  line1 &= INVMASK32_BYTE3;
  line1 |= line2 & MASK32_BYTE3;
  line2 &= INVMASK32_BYTE3;
  line2 |= line3 & MASK32_BYTE3;
  line3 &= INVMASK32_BYTE3;
  line3 |= temp;

  (*state).i[0] = line0;
  (*state).i[1] = line1;
  (*state).i[2] = line2;
  (*state).i[3] = line3;
}
#endif // #if (defined(CBC) && CBC == 1) || (defined(ECB) && ECB == 1)

// Cipher is the main function that encrypts the PlainText.
static void Cipher(state_t* state, const roundKey_t* RoundKey)
{
  uint8_t round = 0;

  // Add the First round key to the state before starting the rounds.
  AddRoundKey(0, state, RoundKey);

  // There will be Nr rounds.
  // The first Nr-1 rounds are identical.
  // These Nr rounds are executed in the loop below.
  // Last one without MixColumns()
  for (round = 1; ; ++round)
  {
    SubBytes(state);
    ShiftRows(state);
    if (round == Nr) {
      break;
    }
    MixColumns(state);
    AddRoundKey(round, state, RoundKey);
  }
  // Add round key to last round
  AddRoundKey(Nr, state, RoundKey);
}

#if (defined(CBC) && CBC == 1) || (defined(ECB) && ECB == 1)
static void InvCipher(state_t* state, const roundKey_t* RoundKey)
{
  uint8_t round = 0;

  // Add the First round key to the state before starting the rounds.
  AddRoundKey(Nr, state, RoundKey);

  // There will be Nr rounds.
  // The first Nr-1 rounds are identical.
  // These Nr rounds are executed in the loop below.
  // Last one without InvMixColumn()
  for (round = (Nr - 1); ; --round)
  {
    InvShiftRows(state);
    InvSubBytes(state);
    AddRoundKey(round, state, RoundKey);
    if (round == 0) {
      break;
    }
    InvMixColumns(state);
  }

}
#endif // #if (defined(CBC) && CBC == 1) || (defined(ECB) && ECB == 1)

/*****************************************************************************/
/* Public functions:                                                         */
/*****************************************************************************/
#if defined(ECB) && (ECB == 1)


void AES_ECB_encrypt(const struct AES_ctx* ctx, uint8_t* buf)
{
  // The next function call encrypts the PlainText with the Key using AES algorithm.
  Cipher((state_t*)buf, ctx->RoundKey);
}

void AES_ECB_decrypt(const struct AES_ctx* ctx, uint8_t* buf)
{
  // The next function call decrypts the PlainText with the Key using AES algorithm.
  InvCipher((state_t*)buf, ctx->RoundKey);
}


#endif // #if defined(ECB) && (ECB == 1)





#if defined(CBC) && (CBC == 1)


static void XorWithIv(uint8_t* buf, const uint8_t* Iv)
{
  uint8_t i;
  for (i = 0; i < AES_BLOCKLEN; ++i) // The block in AES is always 128bit no matter the key size
  {
    buf[i] ^= Iv[i];
  }
}

void AES_CBC_encrypt_buffer(struct AES_ctx *ctx, uint8_t* buf, size_t length)
{
  size_t i;
  uint8_t *Iv = ctx->Iv;
  for (i = 0; i < length; i += AES_BLOCKLEN)
  {
    XorWithIv(buf, Iv);
    Cipher((state_t*)buf, ctx->RoundKey);
    Iv = buf;
    buf += AES_BLOCKLEN;
  }
  /* store Iv in ctx for next call */
  memcpy(ctx->Iv, Iv, AES_BLOCKLEN);
}

void AES_CBC_decrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length)
{
  size_t i;
  uint8_t storeNextIv[AES_BLOCKLEN];
  for (i = 0; i < length; i += AES_BLOCKLEN)
  {
    memcpy(storeNextIv, buf, AES_BLOCKLEN);
    InvCipher((state_t*)buf, ctx->RoundKey);
    XorWithIv(buf, ctx->Iv);
    memcpy(ctx->Iv, storeNextIv, AES_BLOCKLEN);
    buf += AES_BLOCKLEN;
  }

}

#endif // #if defined(CBC) && (CBC == 1)



#if defined(CTR) && (CTR == 1)

/* Symmetrical operation: same function for encrypting as for decrypting. Note any IV/nonce should never be reused with the same key */
void AES_CTR_xcrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length)
{
  uint8_t buffer[AES_BLOCKLEN];
  
  size_t i;
  int bi;
  for (i = 0, bi = AES_BLOCKLEN; i < length; ++i, ++bi)
  {
    if (bi == AES_BLOCKLEN) /* we need to regen xor compliment in buffer */
    {
      
      memcpy(buffer, ctx->Iv, AES_BLOCKLEN);
      Cipher((state_t*)buffer,ctx->RoundKey);

      /* Increment Iv and handle overflow */
      for (bi = (AES_BLOCKLEN - 1); bi >= 0; --bi)
      {
	/* inc will overflow */
        if (ctx->Iv[bi] == 255)
	{
          ctx->Iv[bi] = 0;
          continue;
        } 
        ctx->Iv[bi] += 1;
        break;   
      }
      bi = 0;
    }

    buf[i] = (buf[i] ^ buffer[bi]);
  }
}

#endif // #if defined(CTR) && (CTR == 1)

