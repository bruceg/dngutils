#include <string.h>

#include "jpeg-ls.h"

/*****************************************************************************
 * JPEG Huffman implementation derived from the JPEG specification ITU-T.81.
 *****************************************************************************/

/*****************************************************************************
 * Section K.2 Figure K.1 Procedure to find Huffman code sizes
 *****************************************************************************/
static void huffman_code_size(unsigned codesize[257],
			      const unsigned long freqorig[256])
{
  unsigned long freq[257];
  int others[257];
  int i;
  int v1;
  int v2;
  unsigned long freq1;
  unsigned long freq2;

  memcpy(freq, freqorig, 256 * sizeof freq[0]);
  /* Before starting the procedure, the FREQ value for V = 256 is set to
   * 1 to reserve one code point. Reserving one code point guarantees
   * that no code word can ever be all "1" bits. */
  freq[256] = 1;
  /* The entries in CODESIZE are all set to 0. */
  memset(codesize, 0, 257 * sizeof codesize[0]);
  /* The indices in OTHERS are set to -1, the value which terminates a
   * chain of indices. */
  for (i = 0; i < 257; ++i)
    others[i] = -1;

  for (;;) {
    /* Find V1 for least value of FREQ(V1) > 0 */
    for (v1 = -1, freq1 = ~0UL, i = 0; i < 257; ++i) {
      if (freq[i] > 0 && freq[i] <= freq1) {
	freq1 = freq[i];
	v1 = i;
      }
    }
    /* Find V2 for next least value of FREQ(V2) > 0 */
    for (v2 = -1, freq2 = ~0UL, i = 0; i < 257; ++i) {
      if (i != v1 && freq[i] > 0 && freq[i] <= freq2) {
	freq2 = freq[i];
	v2 = i;
      }
    }

    /* V2 exists ? */
    if (v2 < 0)
      /* No: Done */
      break;

    /* FREQ(V1) = FREQ(V1) + FREQ(V2) */
    freq[v1] += freq[v2];
    /* FREQ(V2) = 0 */
    freq[v2] = 0;

    /* CODESIZE(V1) = CODESIZE(V1) + 1 */
    ++codesize[v1];
    /* OTHERS(V1) = -1 ? */
    while (others[v1] >= 0) {
      /* No: V1 = OTHERS(V1) */
      v1 = others[v1];
      ++codesize[v1];
    }

    /* OTHERS(V1) = V2 */
    others[v1] = v2;
    
    /* CODESIZE(V2) = CODESIZE(V2) + 1 */
    ++codesize[v2];
    /* OTHERS(V2) = -1 ? */
    while (others[v2] >= 0) {
      /* No: V2 = OTHERS(V2) */
      v2 = others[v2];
      ++codesize[v2];
    }
  }
}

/*****************************************************************************
 * Section K.2 Figure K.3 Procedure for limiting code lengths to 16 bits
 *
 * Figure K.3 gives the procedure for adjusting the BITS list so that no
 * code is longer than 16 bits.  Since symbols are paired for the
 * longest Huffman code, the symbols are removed from this length
 * category two at a time.  The prefix for the pair (which is one bit
 * shorter) is allocated to one of the pair; then (skipping the BITS
 * entry for that prefix length) a code word from the next shortest
 * non-zero BITS entry is converted into a prefix for two code words one
 * bit longer.  After the BITS list is reduced to a maximum code length
 * of 16 bits, the last step removes the reserved code point from the
 * code length count.
 *****************************************************************************/
static void huffman_adjust_bits(struct jpeg_huffman_encoder* h)
{
  unsigned i;
  unsigned j;

  for (i = 32; i > 16;) {
    if (h->bits[i] > 0) {
      for (j = i - 2; h->bits[j] == 0; --j)
	;
      h->bits[i] -= 2;
      h->bits[i - 1] += 1;
      h->bits[j + 1] += 2;
      h->bits[j] -= 1;
    }
    else
      --i;
  }
  while (h->bits[i] == 0)
    --i;
  h->bits[i] -= 1;
}

/*****************************************************************************
 * Section K.2 Figure K.2 Procedure to find the number of codes of each size
 *
 * Once the code lengths for each symbol have been obtained, the number
 * of codes of each length is obtained using the procedure in Figure
 * K.2.  The count for each size is contained in the list, BITS.  The
 * counts in BITS are zero at the start of the procedure.  The procedure
 * assumes that the probabilities are large enough that code lengths
 * greater than 32 bits never occur.  Note that until the final
 * Adjust_BITS procedure is complete, BITS may have more than the 16
 * entries required in the table specification (see Annex C).
 *****************************************************************************/
static void huffman_count_bits(struct jpeg_huffman_encoder* h,
			       const unsigned codesize[257])
{
  int i;

  memset(h->bits, 0, sizeof h->bits);

  for (i = 0; i < 257; ++i) {
    if (codesize[i] > 0)
      ++h->bits[codesize[i]];
  }
  huffman_adjust_bits(h);
}

/*****************************************************************************
 * Section K.2 Figure K.4 Sorting of input values according to code size
 *
 * The input values are sorted according to code size as shown in Figure
 * K.$.  HUFFVAL is the list containing the input values associated with
 * each code word, in order of increasing code length.
 *
 * At this point, the list of code lengths (BITS) and the list of values
 * (HUFFVAL) can be used to generate the code tables.  These procedures
 * are described in Annex C.
 *****************************************************************************/
static void huffman_sort_input(struct jpeg_huffman_encoder* h,
			       const unsigned codesize[256])
{
  unsigned i;
  unsigned j;
  unsigned k;

  memset(h->huffval, 0, sizeof h->huffval[0]);
  
  for (i = 1, k = 0; i <= 32; ++i) {
    for (j = 0; j < 256; ++j) {
      if (codesize[j] == i)
	h->huffval[k++] = j;
    }
  }
}

/*****************************************************************************
 * Section C.2 Figure C.1 Generation of table of Huffman code sizes
 *****************************************************************************/
static int huffman_generate_size_table(unsigned huffsize[256],
				       const struct jpeg_huffman_encoder* h)
{
  unsigned i;
  unsigned j;
  unsigned k;

  memset(huffsize, 0, 256 * sizeof huffsize[0]);
  
  for (k = 0, i = 1, j = 1; i <= 16; ++i, j = 1) {
    for (j = 1; j <= h->bits[i]; ++j, ++k)
      huffsize[k] = i;
  }
  huffsize[k] = 0;
  return k;
}

/*****************************************************************************
 * Section C.2 Figure C.2 Generation of table of Huffman codes
 *****************************************************************************/
static void huffman_generate_code_table(unsigned huffcode[256],
					const unsigned huffsize[256])
{
  unsigned k;
  unsigned si;
  unsigned code;
  
  memset(huffcode, 0, 256 * sizeof huffcode[0]);
  
  for (k = 0, code = 0, si = huffsize[0];;) {
    do {
      huffcode[k] = code;
      ++code;
      ++k;
    } while (huffsize[k] == si);
    
    if (huffsize[k] == 0)
      return;

    do {
      code <<= 1;
      ++si;
    } while (huffsize[k] != si);
  }
}

/*****************************************************************************
 * Section C.2 Figure C.3 Ordering procedure for encoding code tables
 *****************************************************************************/
static void huffman_order_codes(struct jpeg_huffman_encoder* h,
				const unsigned huffcode[256],
				const unsigned huffsize[256],
				unsigned lastk)
{
  unsigned k;
  unsigned i;
  
  for (k = 0; k < lastk; ++k) {
    i = h->huffval[k];
    h->ehufco[i] = huffcode[k];
    h->ehufsi[i] = huffsize[k];
  }
}

/*****************************************************************************
 * All the necessary steps to generate JPEG Huffman table information
 *****************************************************************************/
void jpeg_huffman_generate(struct jpeg_huffman_encoder* h,
			   const unsigned long freq[256])
{
  unsigned codesize[257];
  unsigned huffcode[256];
  unsigned lastk;

  /* Annex K */
  huffman_code_size(codesize, freq);
  huffman_count_bits(h, codesize);
  huffman_sort_input(h, codesize);

  /* Annex C */
  lastk = huffman_generate_size_table(codesize, h);
  huffman_generate_code_table(huffcode, codesize);
  huffman_order_codes(h, huffcode, codesize, lastk);
}
