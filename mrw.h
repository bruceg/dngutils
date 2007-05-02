#ifndef MRW__H__
#define MRW__H__

#include <stdio.h>
#include "uint.h"

struct mrw_block
{
  uint32 offset;
  uint32 length;
  const unsigned char* data;
};

struct mrw
{
  uint32 header_length;
  const unsigned char* header;

  struct mrw_block prd;
  struct mrw_block ttw;
  struct mrw_block wbg;
  struct mrw_block rif;

  uint32 width;
  uint32 height;
  const uint16* raw;
};

extern int mrw_load(struct mrw* mrw, FILE* in);

#endif
