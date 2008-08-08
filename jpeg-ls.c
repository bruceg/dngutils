#include <assert.h>
#include <string.h>

#include "jpeg-ls.h"

static unsigned numbits[65536];

static void init_numbits(void)
{
  unsigned i;
  unsigned j;
  unsigned k;

  for (j = 0, i = 0, k = 1; i <= 16; ++i, k <<= 1) {
    while (j < k)
      numbits[j++] = i;
  }
}

static void count_diff(struct bitstream* stream,
		       int diff,
		       void* dataptr)
{
  unsigned long* freq = dataptr;
  if (diff < 0)
    diff = -diff;
  ++freq[numbits[diff]];
  (void)stream;
}

static void write_diff(struct bitstream* stream,
		       int diff,
		       void* dataptr)
{
  const struct jpeg_huffman_encoder* huffman = dataptr;
  int data;
  unsigned bits;
  
  data = diff;
  if (diff < 0) {
    diff = -diff;
    data = ~diff;
  }

  bits = numbits[diff];
  jpeg_write_bits(stream, huffman->ehufsi[bits], huffman->ehufco[bits]);
  if (bits != 16)
    jpeg_write_bits(stream, bits, data & ~(~0U << bits));
}

static void process_image(struct bitstream* stream,
			  void (*fn)(struct bitstream* stream,
				     int diff,
				     void* data),
			  const uint16* rowptr,
			  unsigned enc_rows,
			  unsigned out_rows,
			  unsigned enc_cols,
			  unsigned out_cols,
			  unsigned channels,
			  unsigned bit_depth,
			  unsigned row_width,
			  void* data[],
			  int multi_table)
{
  unsigned row;
  unsigned col;
  const uint16* colptr;
  unsigned pred0;
  unsigned pred1;
  int diff0;
  int diff1;
  const int table1 = !!multi_table;
  
  pred0 = pred1 = 1 << (bit_depth - 1);

  for (row = 0;
       row < enc_rows;
       ++row, rowptr += row_width) {
    for (col = 0, colptr = rowptr;
	 col < enc_cols;
	 ++col, colptr += channels) {
      diff0 = colptr[0] - pred0;
      diff1 = colptr[1] - pred1;

      fn(stream, diff0, data[0]);
      fn(stream, diff1, data[table1]);

      pred0 += diff0;
      pred1 += diff1;
    }
    for (; col < out_cols; ++col) {
      fn(stream, 0, data[0]);
      fn(stream, 0, data[table1]);
    }
    pred0 = rowptr[0];
    pred1 = rowptr[1];
  }
  for (; row < out_rows; ++row) {
    for (col = 0; col < out_cols; ++col) {
      fn(stream, 0, data[0]);
      fn(stream, 0, data[table1]);
    }
  }
}

/*****************************************************************************/
int jpeg_ls_encode(struct stream* stream,
		   const uint16* data,
		   unsigned enc_rows,
		   unsigned out_rows,
		   unsigned enc_cols,
		   unsigned out_cols,
		   unsigned channels,
		   unsigned bit_depth,
		   unsigned row_width)
{
  struct jpeg_huffman_encoder huffman[2];
  unsigned long freq[2][256];
  struct bitstream bitstream = { stream, 0, 0 };
  int multi_table = 1;
  void* dataptrs[2];

  /* FIXME: This encoder only handles 2-channel data from raw images. */
  assert(channels == 2);

  init_numbits();
  memset(freq, 0, sizeof freq);
  dataptrs[0] = freq[0];
  dataptrs[1] = freq[1];
  process_image(0, count_diff, data,
		enc_rows, out_rows, enc_cols, out_cols,
		channels, bit_depth, row_width, dataptrs, multi_table);

  jpeg_huffman_generate(&huffman[0], freq[0]);
  if (multi_table)
    jpeg_huffman_generate(&huffman[1], freq[1]);

  jpeg_write_start(&bitstream, out_rows, out_cols, channels, bit_depth,
		   huffman, multi_table, 1);
  dataptrs[0] = &huffman[0];
  dataptrs[1] = &huffman[1];
  process_image(&bitstream, write_diff, data,
		enc_rows, out_rows, enc_cols, out_cols,
		channels, bit_depth, row_width, dataptrs, multi_table);
  jpeg_write_flush(&bitstream);
  jpeg_write_end(&bitstream);

  return 1;
}
