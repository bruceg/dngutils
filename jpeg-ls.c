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

static void process_row(struct bitstream* stream,
			void (*fn)(struct bitstream* stream,
				   int diff,
				   void* data),
			const uint16* row0,
			const uint16* row1,
			unsigned cols,
			void* data[],
			int table1,
			int predictor)
{
  unsigned col;
  int pred0;
  int pred1;
  int diff0;
  int diff1;

  pred0 = row0[0];
  pred1 = row0[1];

  for (col = 0; col < cols; ++col, row0 += 2, row1 += 2) {

    diff0 = row1[0] - pred0;
    diff1 = row1[1] - pred1;

    fn(stream, diff0, data[0]);
    fn(stream, diff1, data[table1]);

    /* Predictor context: Px is the predictor to calculate. Ra is the
     * just encoded pixel and Rc and Rb are the pixels above as follows:
     *
     * Rc Rb
     * Ra Px
     */
    switch (predictor) {
    case 1:			/* Px = Ra */
      pred0 = row1[0];
      pred1 = row1[1];
      break;
    case 2:			/* Px = Rb */
      pred0 = row0[2];
      pred1 = row0[3];
      break;
    case 3:			/* Px = Rc */
      pred0 = row0[0];
      pred1 = row0[1];
      break;
    case 4:			/* Px = Ra + Rb - Rc */
      pred0 = row1[0] + row0[2] - row0[0];
      pred1 = row1[1] + row0[3] - row0[1];
      break;
    case 5:			/* Px = Ra + ((Rb - Rc) >> 1) */
      pred0 = row1[0] + ((row0[2] - row0[0]) >> 1);
      pred1 = row1[1] + ((row0[3] - row0[1]) >> 1);
      break;
    case 6:			/* Px = Rb + ((Ra - Rc) >> 1) */
      pred0 = row0[2] + ((row1[0] - row0[0]) >> 1);
      pred1 = row0[3] + ((row1[1] - row0[1]) >> 1);
      break;
    case 7:
      pred0 = (row1[0] + row0[2]) / 2;
      pred1 = (row1[1] + row0[3]) / 2;
      break;
    }
  }
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
			  int multi_table,
			  int predictor)
{
  unsigned row;
  unsigned vrow;
  unsigned col;
  unsigned vcol;
  const int table1 = !!multi_table;
  uint16 vrows[2][out_cols*channels*2];
  uint16* vrow0;
  uint16* vrow1;
  uint16* ptr;
  uint16 last0;
  uint16 last1;

  vrow0 = vrows[0];
  vrow1 = vrows[1];

  for (row = 0; row < enc_rows; row += 2) {

    if (row == 0)
      vrow0[0] = vrow0[1] = 1 << (bit_depth - 1);

    for (ptr = vrow1, vcol = vrow = 0; vrow < 2; ++vrow) {
      for (col = 0; col < enc_cols * channels; ++col, ++vcol, ++ptr)
	*ptr = rowptr[col];
      last0 = ptr[-2];
      last1 = ptr[-1];
      for (; col < out_cols * channels; col += 2, vcol += 2, ptr += 2) {
	ptr[0] = last0;
	ptr[1] = last1;
      }
      rowptr += row_width;
    }

    process_row(stream, fn, vrow0, vrow1, out_cols * 2, data, table1,
		(row == 0) ? 1 : predictor);

    ptr = vrow0;
    vrow0 = vrow1;
    vrow1 = ptr;
  }
  for (; row < out_rows; row += 2) {
#if 0
    /* This doesn't work for predictors 5 and 6, and so the bitstream is
     * larger. */
    for (col = 0; col < out_cols * 2 / channels; ++col) {
      fn(stream, 0, data[0]);
      fn(stream, 0, data[table1]);
    }
#else
    process_row(stream, fn, vrow0, vrow1, out_cols * 2, data, table1,
		predictor);
    ptr = vrow0;
    vrow0 = vrow1;
    vrow1 = ptr;
#endif
  }
}

static int best_predictor(const uint16* data,
			  unsigned enc_rows,
			  unsigned out_rows,
			  unsigned enc_cols,
			  unsigned out_cols,
			  unsigned channels,
			  unsigned bit_depth,
			  unsigned row_width,
			  int multi_table,
			  struct jpeg_huffman_encoder huffman[8][2])
{
  unsigned long bestbits;
  unsigned long bits;
  unsigned i;
  int bestpred;
  int pred;
  void* dataptrs[2];
  unsigned long freq[2][256];

  bestbits = ~0UL;
  bestpred = 1;
  for (pred = 1; pred < 8; ++pred) {
    memset(freq, 0, sizeof freq);
    dataptrs[0] = freq[0];
    dataptrs[1] = freq[1];
    process_image(0, count_diff, data,
		  enc_rows, out_rows, enc_cols, out_cols,
		  channels, bit_depth, row_width, dataptrs,
		  multi_table, pred);
    jpeg_huffman_generate(&huffman[pred][0], freq[0]);
    if (multi_table)
      jpeg_huffman_generate(&huffman[pred][1], freq[1]);
    /* Estimate roughly the number of bits used by this encoding. */
    for (bits = 0, i = 0; i < bit_depth; ++i)
      bits += (huffman[pred][0].ehufsi[i] + i) * freq[0][i];
    if (multi_table)
      for (bits = 0, i = 0; i < bit_depth; ++i)
	bits += (huffman[pred][1].ehufsi[i] + i) * freq[1][i];
    if (bits < bestbits) {
      bestbits = bits;
      bestpred = pred;
    }
  }
  return bestpred;
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
  struct jpeg_huffman_encoder huffman[8][2];
  struct bitstream bitstream = { stream, 0, 0 };
  int multi_table = 1;
  void* dataptrs[2];
  int predictor = 7;

  /* FIXME: This encoder only handles 2-channel data from raw images. */
  assert(channels == 2);

  init_numbits();
  predictor = best_predictor(data, enc_rows, out_rows, enc_cols, out_cols,
			     channels, bit_depth, row_width, multi_table,
			     huffman);

  /* The Bayer image matrix is typically similar to:
   *
   * RGRGRG...
   * GBGBGB...
   * RGRGRG...
   * GBGBGB...
   *
   * When JPEG-LS predictors are used that use pixels above the current
   * pixel, this pattern produces bad results since different colors are
   * used to predict.  By outputting one row for every input row, the
   * resulting data becomes:
   *
   * RGRGRG...GBGBGB...
   * RGRGRG...GBGBGB...
   * 
   * Thus allowing same color pixels to line up between rows.  Since the
   * color pairs switch in the middle of a row, there will be a pair of
   * poor predictions made at that switch, but that's a relatively minor
   * effect compared to the benefits of allowing better prediction
   * above. */
  /* FIXME: this 2-row merging should be made adjustable too. */
  jpeg_write_start(&bitstream, out_rows/2, out_cols*2, channels, bit_depth,
		   huffman[predictor], multi_table, predictor);
  dataptrs[0] = &huffman[predictor][0];
  dataptrs[1] = &huffman[predictor][1];
  process_image(&bitstream, write_diff, data,
		enc_rows, out_rows, enc_cols, out_cols,
		channels, bit_depth, row_width, dataptrs,
		multi_table, predictor);
  jpeg_write_flush(&bitstream);
  jpeg_write_end(&bitstream);

  return 1;
}
