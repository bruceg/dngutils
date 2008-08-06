#ifndef JPEG_LS__H__
#define JPEG_LS__H__

#include "stream.h"
#include "uint.h"

#define M_SOF3 0xc3
#define M_DHT 0xc4
#define M_SOI 0xd8
#define M_EOI 0xd9
#define M_SOS 0xda

struct jpeg_huffman_encoder
{
  unsigned bits[33];
  unsigned huffval[256];
  unsigned ehufco[256];
  unsigned ehufsi[256];
};

#define SINGLE_HUFFMAN 0

struct bitstream
{
  struct stream* stream;
  unsigned bitbuffer;
  unsigned bitcount;
};

extern void jpeg_huffman_generate(struct jpeg_huffman_encoder* h,
				  const unsigned long freq[256]);

extern void jpeg_write_byte(struct bitstream* out, unsigned char byte);
extern void jpeg_write_word(struct bitstream* out, unsigned word);
extern void jpeg_write_bits(struct bitstream* out,
			    unsigned value,
			    unsigned count);
extern void jpeg_write_flush(struct bitstream* out);
extern void jpeg_write_marker(struct bitstream* out, unsigned char marker);
extern void jpeg_write_start(struct bitstream* stream,
			     unsigned rows,
			     unsigned cols,
			     unsigned channels,
			     unsigned bit_depth,
			     struct jpeg_huffman_encoder* huffman,
			     int multi_table);
extern void jpeg_write_end(struct bitstream* stream);

extern int jpeg_ls_encode(struct stream* stream,
			  const uint16* data,
			  unsigned enc_rows,
			  unsigned out_rows,
			  unsigned enc_cols,
			  unsigned out_cols,
			  unsigned channels,
			  unsigned bit_depth,
			  unsigned row_step,
			  unsigned col_step);

#endif
