#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <msg/msg.h>
#include "tiff.h"

const unsigned tiff_type_size[13] = {
  0,
  1,				/* BYTE */
  1,				/* ASCII */
  2,				/* SHORT */
  4,				/* LONG */
  8,				/* RATIONAL */
  1,				/* SBYTE */
  1,				/* UNDEFINED */
  2,				/* SSHORT */
  4,				/* SLONG */
  8,				/* SRATIONAL */
  4,				/* FLOAT */
  8,				/* DOUBLE */
};

struct tiff_tag* tiff_ifd_add(struct tiff_ifd* ifd,
			      enum tiff_tag_id id,
			      enum tiff_tag_type type,
			      uint32 count)
{
  struct tiff_tag* tag;
  uint16 i;
  for (tag = 0, i = ifd->count; i > 0; --i) {
    if (ifd->tags[i].tag == id) {
      free(ifd->tags[i].data);
      tag = &ifd->tags[i];
      break;
    }
  }
  if (!tag) {
    while (ifd->count >= ifd->size) {
      uint16 newsize;
      struct tiff_tag* newtags;
      newsize = (ifd->size == 0) ? 8 : (ifd->size * 2);
      newtags = malloc(newsize * sizeof *newtags);
      memcpy(newtags, ifd->tags, ifd->count * sizeof *newtags);
      free(ifd->tags);
      ifd->tags = newtags;
      ifd->size = newsize;
    }

    tag = &ifd->tags[ifd->count];
    ++ifd->count;
  }

  tag->tag = id;
  tag->type = type;
  tag->count = count;
  tag->size = ((count * tiff_type_size[type]) + 1) & ~1UL;
  tag->data = malloc(tag->size);
  tag->offset = 0;

  return tag;
}

struct tiff_tag* tiff_ifd_add_ascii(struct tiff_ifd* ifd,
				    enum tiff_tag_id id,
				    const char* s)
{
  size_t len;
  struct tiff_tag* tag;
  
  len = strlen(s) + 1;
  if ((tag = tiff_ifd_add(ifd, id, ASCII, len)) != 0)
    memcpy(tag->data, s, len);
  return tag;
}

struct tiff_tag* tiff_ifd_add_byte(struct tiff_ifd* ifd,
				   enum tiff_tag_id id,
				   size_t len,
				   const char* s)
{
  struct tiff_tag* tag;
  
  if ((tag = tiff_ifd_add(ifd, id, BYTE, len)) != 0)
    memcpy(tag->data, s, len);
  return tag;
}

struct tiff_tag* tiff_ifd_add_undefined(struct tiff_ifd* ifd,
					enum tiff_tag_id id,
					size_t len,
					const char* s)
{
  struct tiff_tag* tag;
  
  if ((tag = tiff_ifd_add(ifd, id, UNDEFINED, len)) != 0)
    memcpy(tag->data, s, len);
  return tag;
}

struct tiff_tag* tiff_ifd_add_long(struct tiff_ifd* ifd,
				   enum tiff_tag_id id,
				   uint32 count,
				   ...)
{
  struct tiff_tag* tag;
  uint32 i;
  va_list ap;

  if ((tag = tiff_ifd_add(ifd, id, LONG, count)) != 0) {
    va_start(ap, count);
    for (i = 0; i < count; ++i)
      uint32_pack_lsb(va_arg(ap, uint32), tag->data + i*4);
    va_end(ap);
  }
  return tag;
}

struct tiff_tag* tiff_ifd_add_short(struct tiff_ifd* ifd,
				    enum tiff_tag_id id,
				    uint32 count,
				    ...)
{
  struct tiff_tag* tag;
  uint32 i;
  va_list ap;

  if ((tag = tiff_ifd_add(ifd, id, SHORT, count)) != 0) {
    va_start(ap, count);
    for (i = 0; i < count; ++i)
      uint16_pack_lsb(va_arg(ap, unsigned int), tag->data + i*2);
    va_end(ap);
  }
  return tag;
}

struct tiff_tag* tiff_ifd_add_sshort(struct tiff_ifd* ifd,
				     enum tiff_tag_id id,
				     uint32 count,
				     ...)
{
  struct tiff_tag* tag;
  uint32 i;
  va_list ap;

  if ((tag = tiff_ifd_add(ifd, id, SSHORT, count)) != 0) {
    va_start(ap, count);
    for (i = 0; i < count; ++i)
      uint16_pack_lsb(va_arg(ap, signed int), tag->data + i*2);
    va_end(ap);
  }
  return tag;
}

struct tiff_tag* tiff_ifd_add_rational(struct tiff_ifd* ifd,
				       enum tiff_tag_id id,
				       uint32 count,
				       ...)
{
  struct tiff_tag* tag;
  uint32 i;
  va_list ap;

  if ((tag = tiff_ifd_add(ifd, id, RATIONAL, count)) != 0) {
    va_start(ap, count);
    for (i = 0; i < count; ++i) {
      uint32_pack_lsb(va_arg(ap, uint32), tag->data + i*8);
      uint32_pack_lsb(va_arg(ap, uint32), tag->data + i*8+4);
    }
    va_end(ap);
  }
  return tag;
}

struct tiff_tag* tiff_ifd_add_srational(struct tiff_ifd* ifd,
					enum tiff_tag_id id,
					uint32 count,
					...)
{
  struct tiff_tag* tag;
  uint32 i;
  va_list ap;

  if ((tag = tiff_ifd_add(ifd, id, SRATIONAL, count)) != 0) {
    va_start(ap, count);
    for (i = 0; i < count; ++i) {
      uint32_pack_lsb(va_arg(ap, uint32), tag->data + i*8);
      uint32_pack_lsb(va_arg(ap, uint32), tag->data + i*8+4);
    }
    va_end(ap);
  }
  return tag;
}

static int tiff_tag_cmp(const void* aptr, const void* bptr)
{
  const struct tiff_tag* a = aptr;
  const struct tiff_tag* b = bptr;
  return a->tag - b->tag;
}

void tiff_ifd_sort(struct tiff_ifd* ifd)
{
  qsort(ifd->tags, ifd->count, sizeof *ifd->tags, tiff_tag_cmp);
}

uint32 tiff_ifd_size(const struct tiff_ifd* ifd)
{
  uint32 total;
  const struct tiff_tag* tag;
  uint32 i;

  for (total = 0, i = 0, tag = ifd->tags; i < ifd->count; ++i, ++tag) {
    if (tag->size > 4)
      total += tag->size;
  }
  return ((total + 2 + ifd->count * 12 + 4) + 3) & ~3UL;
}

void tiff_start(FILE* out, uint32 offset)
{
  unsigned char header[8] = "II\0\0\0\0\0\0";
  uint16_pack_lsb(42, header + 2);
  uint32_pack_lsb(offset, header + 4);
  fwrite(header, 1, 8, out);
}

uint32 tiff_write_ifd(FILE* out, struct tiff_ifd* ifd)
{
  uint32 i;
  struct tiff_tag* tag;
  uint32 start;
  uint32 offset;
  unsigned char buf[12];

  start = ftell(out);

  tiff_ifd_sort(ifd);
  offset = start + 2 + 12 * ifd->count + 4;

  uint16_pack_lsb(ifd->count, buf);
  fwrite(buf, 1, 2, out);

  for (i = 0, tag = ifd->tags; i < ifd->count; ++i, ++tag) {
    uint16_pack_lsb(tag->tag, buf);
    uint16_pack_lsb(tag->type, buf + 2);
    uint32_pack_lsb(tag->count, buf + 4);
    if (tag->size > 4) {
      uint32_pack_lsb(offset, buf + 8);
      offset += tag->size;
    }
    else {
      memset(buf + 8, 0, 4);
      memcpy(buf + 8, tag->data, tag->size);
    }
    fwrite(buf, 1, 12, out);
  }

  /* FIXME: no chaining here */
  uint32_pack_lsb(0, buf);
  fwrite(buf, 1, 4, out);

  for (i = 0, tag = ifd->tags; i < ifd->count; ++i, ++tag) {
    if (tag->size > 4)
      fwrite(tag->data, 1, tag->size, out);
  }

  if (offset != (uint32)ftell(out))
    die1(1, "Internal write error");

  if (offset % 4 != 0) {
    memset(buf, 0, 4);
    fwrite(buf, 1, 4 - (offset % 4), out);
  }
  
  return start;
}

void tiff_end(FILE* out, uint32 offset)
{
  unsigned char buf[4];
  uint32_pack_lsb(offset, buf);
  fseek(out, 4, SEEK_SET);
  fwrite(buf, 1, 4, out);
}
