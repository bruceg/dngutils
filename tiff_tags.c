#include "tiff.h"

struct tag_name
{
  enum tiff_tag_id id;
  const char* name;
};

#define TIFF_TAG(NAME,VALUE) { VALUE, #NAME }
static const struct tag_name tiff_tag_names[] = {
  #include "tiff_tags.h"
  { 0, 0 }
};
#undef TIFF_TAG

const char* tiff_tag_name(enum tiff_tag_id id)
{
  const struct tag_name* t;
  for (t = tiff_tag_names; t->id != 0; ++t)
    if (id == t->id)
      return t->name;
  return 0;
}
