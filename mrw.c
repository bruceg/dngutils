#include <string.h>
#include <msg/msg.h>
#include <uint16.h>
#include <uint32.h>

#include "mrw.h"

static int mrw_parse(struct mrw* mrw)
{
  uint32 offset;
  uint32 length;
  struct mrw_block* block;
  const unsigned char* ptr;
  
  for (offset = 0; offset < mrw->header_length; offset += length + 8) {
    ptr = mrw->header + offset;
    length = uint32_get_msb(ptr + 4);
    if (memcmp(ptr, "\0PRD", 4) == 0)
      block = &mrw->prd;
    else if (memcmp(ptr, "\0TTW", 4) == 0)
      block = &mrw->ttw;
    else if (memcmp(ptr, "\0WBG", 4) == 0)
      block = &mrw->wbg;
    else if (memcmp(ptr, "\0RIF", 4) == 0)
      block = &mrw->rif;
    else if (memcmp(ptr, "\0PAD", 4) == 0)
      continue;
    else {
      warnf("{Unknown MRW block type: }cccc", ptr[0], ptr[1], ptr[2], ptr[3]);
      continue;
    }

    block->offset = offset;
    block->length = length;
    block->data = ptr + 8;
  }

  if (mrw->prd.length == 0
      || mrw->ttw.length == 0
      || mrw->wbg.length == 0
      || mrw->rif.length == 0)
    return 0;
  
  mrw->width = uint16_get_msb(mrw->prd.data + 10);
  mrw->height = uint16_get_msb(mrw->prd.data + 8);

  return 1;
}

static int mrw_load_header(struct mrw* mrw, FILE* in)
{
  unsigned char header[8];
  unsigned char* h;

  memset(mrw, 0, sizeof *mrw);
  
  if (fread(header, 1, sizeof header, in) != sizeof header)
    return 0;
  if (memcmp(header, "\0MRM", 4) != 0)
    return 0;
  
  mrw->header_length = uint32_get_msb(header + 4);
  if ((h = malloc(mrw->header_length)) == 0)
    return 0;
  mrw->header = h;

  if (fread(h, 1, mrw->header_length, in) != mrw->header_length)
    return 0;
  
  return mrw_parse(mrw);
}

static int mrw_load_raw(struct mrw* mrw, FILE* in)
{
  unsigned char row[mrw->width * 3 / 2];
  unsigned char* srcptr;
  uint16* dstptr;
  uint32 x;
  uint32 y;
  
  if ((dstptr = malloc(mrw->width * mrw->height * sizeof *mrw->raw)) == 0)
    return 0;
  mrw->raw = dstptr;

  for (y = 0; y < mrw->height; ++y) {
    if (fread(row, 1, sizeof row, in) != sizeof row)
      return 0;
    for (x = 0, srcptr = row;
	 x < mrw->width;
	 x += 2, srcptr += 3, dstptr += 2) {
      dstptr[0] = ((uint16)srcptr[0] << 4) | (srcptr[1] >> 4);
      dstptr[1] = (((uint16)srcptr[1] << 8) | srcptr[2]) & 0xfff;
    }
  }
  return 1;
}

int mrw_load(struct mrw* mrw, FILE* in)
{
  return mrw_load_header(mrw, in)
    && mrw_load_raw(mrw, in);
}
