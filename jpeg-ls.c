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

static void count_diff(int diff, unsigned long freq[256])
{
  if (diff < 0)
    diff = -diff;
  ++freq[numbits[diff]];
}

static void calc_frequency(unsigned long freq[256],
			   const uint16* data,
			   unsigned enc_rows,
			   unsigned out_rows,
			   unsigned enc_cols,
			   unsigned out_cols,
			   unsigned bit_depth,
			   unsigned row_step,
			   unsigned col_step)
{
  unsigned row;
  unsigned col;
  const uint16* rowptr;
  const uint16* colptr;
  unsigned pred0;
  unsigned pred1;
  int diff0;
  int diff1;
  
  pred0 = pred1 = 1 << (bit_depth - 1);
  
  for (row = 0, rowptr = data;
       row < enc_rows;
       ++row, rowptr += row_step) {
    for (col = 0, colptr = rowptr;
	 col < enc_cols;
	 ++col, colptr += col_step) {
      diff0 = colptr[0] - pred0;
      diff1 = colptr[1] - pred1;

      count_diff(diff0, freq);
      count_diff(diff1, freq);

      pred0 += diff0;
      pred1 += diff1;
    }
    for (; col < out_cols; ++col) {
      count_diff(0, freq);
      count_diff(0, freq);
    }
    pred0 = rowptr[0];
    pred1 = rowptr[1];
  }
  for (; row < out_rows; ++row) {
    for (col = 0; col < out_cols; ++col) {
      count_diff(0, freq);
      count_diff(0, freq);
    }
  }
}

static void write_diff(struct bitstream* stream,
		       int diff,
		       const struct jpeg_huffman_encoder* huffman)
{
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

static void encode_image(struct bitstream* stream,
			 const uint16* data,
			 unsigned enc_rows,
			 unsigned out_rows,
			 unsigned enc_cols,
			 unsigned out_cols,
			 unsigned bit_depth,
			 unsigned row_step,
			 unsigned col_step,
			 const struct jpeg_huffman_encoder* huffman)
{
  unsigned row;
  unsigned col;
  const uint16* rowptr;
  const uint16* colptr;
  unsigned pred0;
  unsigned pred1;
  int diff0;
  int diff1;
  
  pred0 = pred1 = 1 << (bit_depth - 1);

  for (row = 0, rowptr = data;
       row < enc_rows;
       ++row, rowptr += row_step) {
    for (col = 0, colptr = rowptr;
	 col < enc_cols;
	 ++col, colptr += col_step) {
      diff0 = colptr[0] - pred0;
      diff1 = colptr[1] - pred1;

      write_diff(stream, diff0, huffman);
      write_diff(stream, diff1, huffman);

      pred0 += diff0;
      pred1 += diff1;
    }
    for (; col < out_cols; ++col) {
      write_diff(stream, 0, huffman);
      write_diff(stream, 0, huffman);
    }
    pred0 = rowptr[0];
    pred1 = rowptr[1];
  }
  for (; row < out_rows; ++row) {
    for (col = 0; col < out_cols; ++col) {
      write_diff(stream, 0, huffman);
      write_diff(stream, 0, huffman);
    }
  }
  jpeg_write_flush(stream);
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
		   unsigned row_step,
		   unsigned col_step)
{
  struct jpeg_huffman_encoder huffman;
  unsigned long freq[256];
  struct bitstream bitstream = { stream, 0, 0 };

  /* FIXME: This implementation uses a single huffman table for all the
   * data.  With multiple tables, the same tables end up being used for
   * different colors, resulting in effectively identical results. Using
   * a single table eliminates a one DHT header without sacrificing
   * compression efficiency. For the general case (standard 3-color
   * images), the use of a single table is undesireable. */

  /* FIXME: This encoder only handles 2-channel data from raw images. */
  assert(channels == 2);
  assert(channels == col_step);

  init_numbits();
  memset(freq, 0, sizeof freq);
  calc_frequency(freq, data, enc_rows, out_rows, enc_cols, out_cols, bit_depth,
		 row_step, col_step);

  jpeg_huffman_generate(&huffman, freq);

  jpeg_write_start(&bitstream, out_rows, out_cols, channels, bit_depth, &huffman, 0);
  encode_image(&bitstream, data, enc_rows, out_rows, enc_cols, out_cols, bit_depth,
	       row_step, col_step, &huffman);
  jpeg_write_end(&bitstream);

  return 1;
}
