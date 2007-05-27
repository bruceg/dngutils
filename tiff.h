#ifndef TIFF__H__
#define TIFF__H__

#include <stdio.h>
#include "uint.h"

static inline uint32 round_long(uint32 i)
{
  return (i + 3) & ~3UL;
}

#define TIFF_TAG(NAME,VALUE) NAME = VALUE
enum tiff_tag_id {
  #include "tiff_tags.h"
};
#undef TIFF_TAG

enum tiff_tag_type {
  BYTE = 1,
  ASCII,
  SHORT,
  LONG,
  RATIONAL,
  SBYTE,
  UNDEFINED,
  SSHORT,
  SLONG,
  SRATIONAL,
  FLOAT,
  DOUBLE,
};

struct tiff_tag
{
  struct tiff_tag* next;
  enum tiff_tag_id tag;
  enum tiff_tag_type type;
  uint32 count;
  uint32 size;
  unsigned char* data;
  /* The offset is generated when writing out the IFD */
  uint32 offset;
};

struct tiff_ifd
{
  uint16 count;
  struct tiff_tag* tags;
};

extern const unsigned tiff_type_size[13];

extern const char* tiff_tag_name(enum tiff_tag_id tag);

struct tiff_tag* tiff_ifd_add(struct tiff_ifd*,
			      enum tiff_tag_id,
			      enum tiff_tag_type,
			      uint32);

struct tiff_tag* tiff_ifd_add_ascii(struct tiff_ifd*,
				    enum tiff_tag_id,
				    const char*);
struct tiff_tag* tiff_ifd_add_byte(struct tiff_ifd* ifd,
				   enum tiff_tag_id id,
				   size_t len,
				   const char* s);
struct tiff_tag* tiff_ifd_add_undefined(struct tiff_ifd* ifd,
					enum tiff_tag_id id,
					size_t len,
					const char* s);
struct tiff_tag* tiff_ifd_add_long(struct tiff_ifd* ifd,
				   enum tiff_tag_id id,
				   uint32 count, ...);
struct tiff_tag* tiff_ifd_add_short(struct tiff_ifd* ifd,
				    enum tiff_tag_id id,
				    uint32 count, ...);
struct tiff_tag* tiff_ifd_add_sshort(struct tiff_ifd* ifd,
				     enum tiff_tag_id id,
				     uint32 count, ...);
struct tiff_tag* tiff_ifd_add_rational(struct tiff_ifd* ifd,
				       enum tiff_tag_id id,
				       uint32 count, ...);
struct tiff_tag* tiff_ifd_add_srational(struct tiff_ifd* ifd,
					enum tiff_tag_id id,
					uint32 count, ...);

uint32 tiff_ifd_size(const struct tiff_ifd*);
void tiff_ifd_sort(struct tiff_ifd*);

void tiff_start(FILE*, uint32);
uint32 tiff_write_ifd(FILE*, struct tiff_ifd*);
void tiff_end(FILE*, uint32);

#endif
