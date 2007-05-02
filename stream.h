#ifndef STREAM__H__
#define STREAM__H__

#define STREAM_BUFSIZE 8192

struct stream_buffer
{
  unsigned char data[STREAM_BUFSIZE];
  unsigned count;
  struct stream_buffer* next;
};

struct stream
{
  struct stream_buffer* head;
  struct stream_buffer* tail;
};

extern int stream_add_buffer(struct stream* s);
extern int stream_init(struct stream* s);
extern void stream_free(struct stream* s);
extern int stream_length(const struct stream* s);

static inline int stream_putc(struct stream* s, unsigned char c)
{
  struct stream_buffer* b = s->tail;

  b->data[b->count++] = c;
  if (b->count >= STREAM_BUFSIZE)
    return stream_add_buffer(s);
  return 1;
}

#endif
