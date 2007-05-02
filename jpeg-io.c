#include <string.h>

#include "jpeg-ls.h"

/*****************************************************************************
 * Output routines
 *****************************************************************************/
void jpeg_write_byte(struct bitstream* out, unsigned char byte)
{
  stream_putc(out->stream, byte);
}

void jpeg_write_bits(struct bitstream* out,
		     unsigned count,
		     unsigned value)
{
  unsigned bitbuffer = out->bitbuffer;
  unsigned bitcount = out->bitcount;
  unsigned byte;
  
  bitbuffer = (bitbuffer << count) | value;
  bitcount += count;
  while (bitcount >= 8) {
    byte = (bitbuffer >> (bitcount - 8)) & 0xff;
    jpeg_write_byte(out, byte);
    /* Apparently, the JPEG guarantees against all-one Huffman codes
     * prevents this from happening, but just in case, it's better to be
     * safe. */
    if (byte == 0xff)
      jpeg_write_byte(out, 0);
    bitcount -= 8;
  }

  out->bitbuffer = bitbuffer;
  out->bitcount = bitcount;
}

void jpeg_write_flush(struct bitstream* out)
{
  jpeg_write_bits(out, 7, 0x7f);
  out->bitbuffer = 0;
  out->bitcount = 0;
}

void jpeg_write_word(struct bitstream* out, unsigned word)
{
  jpeg_write_byte(out, word >> 8);
  jpeg_write_byte(out, word & 0xff);
}

void jpeg_write_marker(struct bitstream* out, unsigned char marker)
{
  jpeg_write_byte(out, 0xff);
  jpeg_write_byte(out, marker);
}

/*****************************************************************************
 * Write out the JPEG Huffman table data
 * Section B.2.4.2
 *****************************************************************************/
static void jpeg_write_huffman(struct bitstream* out,
			       const struct jpeg_huffman_encoder* h,
			       unsigned tablenum)
{
  unsigned i;
  unsigned length;
  
  for (length = 0, i = 1; i <= 16; ++i)
    length += h->bits[i];
  
  jpeg_write_marker(out, M_DHT);
  jpeg_write_word(out, length + 2 + 1 + 16);

  jpeg_write_byte(out, tablenum);

  for (i = 1; i <= 16; ++i)
    jpeg_write_byte(out, h->bits[i]);

  for (i = 0; i < length; ++i)
    jpeg_write_byte(out, h->huffval[i]);
}

/*****************************************************************************
 * Write out the JPEG file start
 *****************************************************************************/
void jpeg_write_start(struct bitstream* stream,
		      unsigned rows,
		      unsigned cols,
		      unsigned channels,
		      unsigned bit_depth,
		      struct jpeg_huffman_encoder* huffman,
		      int multi_table)
{
  unsigned channel;
  
  /* B.2.1 High-level syntax */
  jpeg_write_marker(stream, M_SOI);

  /* B.2.2 Frame header syntax */
  jpeg_write_marker(stream, M_SOF3);
  jpeg_write_word(stream, 8 + 3 * channels);	/* Lf = 8 + 3 * Nf */
  jpeg_write_byte(stream, bit_depth); /* P */
  jpeg_write_word(stream, rows);	/* Y */
  jpeg_write_word(stream, cols);	/* X */
  jpeg_write_byte(stream, channels);	/* Nf */

  for (channel = 0; channel < channels; ++channel) {
    jpeg_write_byte(stream, channel); /* C[n] */
    jpeg_write_byte(stream, 0x11);	/* H[n] | V[n] */
    jpeg_write_byte(stream, 0);	/* Tq[n] */
  }

  if (multi_table) {
    for (channel = 0; channel < channels; ++channel)
      jpeg_write_huffman(stream, &huffman[channel], channel);
  }
  else
    jpeg_write_huffman(stream, huffman, channel);

  /* B.2.3 Scan header syntax*/
  jpeg_write_marker(stream, M_SOS);
  jpeg_write_word(stream, 6 + 2 * channels);	/* Ls */
  jpeg_write_byte(stream, channels);	/* Ns */
  for (channel = 0; channel < channels; ++channel) {
    jpeg_write_byte(stream, channel); /* Cs[n] */
    jpeg_write_byte(stream, multi_table
		    ? channel << 4 
		    : 0);	/* Td[n] | Ta[n] */
  }
  jpeg_write_byte(stream, 1);	/* Ss -- only use predictor 1 */
  jpeg_write_byte(stream, 0);	/* Se */
  jpeg_write_byte(stream, 0);	/* Ah | Al */
}

/*****************************************************************************
 * Write out the JPEG file end
 *****************************************************************************/
void jpeg_write_end(struct bitstream* stream)
{
  jpeg_write_marker(stream, M_EOI);
}
