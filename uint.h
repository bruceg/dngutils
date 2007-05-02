#ifndef UINT__H__
#define UINT__H__

#include <byteswap.h>
#include <endian.h>
#include <stdint.h>
#include <string.h>

typedef uint16_t uint16;
typedef uint32_t uint32;

static inline uint16 uint16_get_msb(const unsigned char* c)
{
  uint16 v;
  memcpy(&v, c, 2);
#if __BYTE_ORDER == __LITTLE_ENDIAN
  v = bswap_16(v);
#endif
  return v;
}

static inline uint32 uint32_get_msb(const unsigned char* c)
{
  uint32 v;
  memcpy(&v, c, 4);
#if __BYTE_ORDER == __LITTLE_ENDIAN
  v = bswap_32(v);
#endif
  return v;
}

static inline uint32 uint32_get_lsb(const unsigned char* c)
{
  uint32 v;
  memcpy(&v, c, 4);
#if __BYTE_ORDER == __BIG_ENDIAN
  v = bswap_32(v);
#endif
  return v;
}

static inline void uint16_pack_lsb(uint16 v, unsigned char* c)
{
#if __BYTE_ORDER == __BIG_ENDIAN
  v = bswap_16(v);
#endif
  memcpy(c, &v, 2);
}

static inline void uint32_pack_lsb(uint32 v, unsigned char* c)
{
#if __BYTE_ORDER == __BIG_ENDIAN
  v = bswap_32(v);
#endif
  memcpy(c, &v, 4);
}

static inline void uint32_pack_msb(uint32 v, unsigned char* c)
{
#if __BYTE_ORDER == __LITTLE_ENDIAN
  v = bswap_32(v);
#endif
  memcpy(c, &v, 4);
}

#endif
