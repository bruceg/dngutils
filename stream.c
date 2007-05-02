#include <stdlib.h>

#include "stream.h"

static struct stream_buffer* buffer_new(void)
{
  struct stream_buffer* b;

  if ((b = malloc(sizeof *b)) == 0)
    return 0;
  b->count = 0;
  b->next = 0;
  return b;
}

int stream_add_buffer(struct stream* s)
{
  struct stream_buffer* b;

  if ((b = buffer_new()) == 0)
    return 0;
  s->tail->next = b;
  s->tail = b;
  return 1;
}

int stream_init(struct stream* s)
{
  struct stream_buffer* b;

  if ((b = buffer_new()) == 0)
    return 0;
  s->head = s->tail = b;
  return 1;
}

void stream_free(struct stream* s)
{
  struct stream_buffer* curr;
  struct stream_buffer* next;
  
  for (curr = s->head; curr != 0; curr = next) {
    next = curr->next;
    free(curr);
  }
}

int stream_length(const struct stream* s)
{
  const struct stream_buffer* b;
  unsigned long length;
  
  for (length = 0, b = s->head; b != 0; b = b->next)
    length += b->count;

  return length;
}
