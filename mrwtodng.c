#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "die.h"
#include "jpeg-ls.h"
#include "tiff.h"
#include "mrw.h"
#include "stream.h"
#include "uint.h"

const char program[] = "mrwtodng";
const char usage[] =
"Usage: mrwtodng [options] SOURCE.mrw DESTINATION.dng\n"
"Convert Minolta raw (MRW) files to digital negatives (DNG)\n"
"\n"
"  -c, --compress         Compress the raw image data (default).\n"
"  -C, --no-compress      Do not compress the raw image data.\n"
"  -t, --tile             Break compressed data into tiles.\n"
"  -T, --no-tile          Compress the entire data as one block.\n"
"  -h, --tile-height=UNS  The maximum height of all the tiles.\n"
"  -w, --tile-width=UNS   The maximum width of all the tiles.\n";

static int opt_compress = 1;
static int opt_tile = 1;
static unsigned int opt_tile_height = 256;
static unsigned int opt_tile_width = 256;

#define PREVIEW 0

static struct tiff_ifd mainifd;
static struct tiff_ifd exififd;
static struct tiff_ifd subifd1;
static struct tiff_ifd iopifd;
#if PREVIEW
static struct tiff_ifd subifd2;
#endif

static struct mrw mrw;

static uint32 tile_count;
static struct stream* compressed_data;
static struct tiff_tag* raw_offset_tag;
static struct tiff_tag* raw_length_tag;
static struct tiff_tag* iop_offset_tag;

static const unsigned char* thumbnail_start;
static uint32 thumbnail_length;
static const struct tiff_tag* thumbnail_offset_tag;

static void parse_ifd(const unsigned char* start,
		      uint32 offset,
		      void (*fn)(const unsigned char* start,
				 enum tiff_tag_id tag,
				 enum tiff_tag_type type,
				 uint32 count,
				 uint32 value));

static void parse_prd(void)
{
  uint32 x;
  uint32 y;
  const unsigned char* data = mrw.prd.data;
  
  if (memcmp(data, "21810002", 8) == 0) {
    tiff_ifd_add_ascii(&mainifd, UniqueCameraModel,
		       "Konica Minolta Maxxum 7D");
    tiff_ifd_add_ascii(&mainifd, LocalizedCameraModel,
		       "Konica Minolta Maxxum 7D");
  }
  else
    die(1, "Unknown camera model");

  tiff_ifd_add_long(&subifd1, ImageWidth, 1, mrw.width);
  tiff_ifd_add_long(&subifd1, ImageLength, 1, mrw.height);
  tiff_ifd_add_long(&subifd1, ActiveArea, 4, 0, 0, mrw.height, mrw.width);
  
  y = uint16_get_msb(data + 12);
  x = uint16_get_msb(data + 14);
  tiff_ifd_add_rational(&subifd1, DefaultScale, 2, 1, 1, 1, 1);
  tiff_ifd_add_rational(&subifd1, DefaultCropOrigin, 2,
			(mrw.width - x) / 2, 1, (mrw.height - y) / 2, 1);
  tiff_ifd_add_rational(&subifd1, DefaultCropSize, 2, x, 1, y, 1);

  if (data[16] != 12)
    die(1, "Invalid DataSize number");
  if (data[17] != 12)
    die(1, "Invalid PixelSize number");
  if (data[18] != 0x59)
    die(1, "Invalid StorageMethod number");

  if (uint16_get_msb(data + 22) != 1)
    die(1, "Invalid BayerPattern number");
  tiff_ifd_add_short(&subifd1, CFARepeatPattern, 2, 2, 2);
  tiff_ifd_add_byte(&subifd1, CFAPattern, 4, "\0\1\1\2");
  tiff_ifd_add_byte(&subifd1, CFAPlaneColor, 3, "\0\1\2");
  tiff_ifd_add_short(&subifd1, CFALayout, 1, 1);
}

static void parse_ttw_makernote(const unsigned char* start,
				enum tiff_tag_id tag,
				enum tiff_tag_type type,
				uint32 count,
				uint32 value)
{
  switch (tag) {
  case MLTThumbnailOffset:
    thumbnail_start = start + value;
    break;
  case MLTThumbnailLength:
    thumbnail_length = value;
    break;
  default:
    ;
  }
  (void)type;
  (void)count;
}

/* DNGPrivateData is to contain:

1) Six bytes containing the zero-terminated string "Adobe" (the DNG spec
calls for the DNGPrivateData tag to start with an ASCII string
identifying the creator/format).

2) An ASCII string ("MakN" for a Makernote), presumably indicating what
sort of data is being stored here. Note that this is not
zero-terminated.

3) A four-byte count (number of data bytes following); for a simple
MakerNote copy this the length of the original MakerNote data, plus six
additional bytes to store the next two data items.

4) The byte-order indicator from the original file (the usual 'MM'/4D4D
or 'II'/4949).

5) The original file offset for the MakerNote tag data (stored according
to the byte order given above).

6) The contents of the MakerNote tag. This appears to be a simple
byte-for-byte copy, with no modification.
*/

static void add_makernote(const unsigned char* start,
			  uint32 offset,
			  uint32 length)
{
  unsigned char tmpstr[20 + length
		       + 8 + mrw.prd.length
		       + 8 + mrw.wbg.length
		       + 8 + mrw.rif.length
		       + 4];
  /* Stuff the original maker note into the DNG */
  memcpy(tmpstr, "Adobe\0MakN\0\0\0\0\x4d\x4d\x00\x00\x00\x00", 20);
  uint32_pack_msb(offset, tmpstr + 16);
  uint32_pack_msb(length + 6, tmpstr + 10);
  memcpy(tmpstr + 20, start, length);
  length += 20;
  /* Adobe RAW converter also adds other bits of the MRW here */
  memcpy(tmpstr + length, "MRW \0\0\0\0\x4d\x4d\x00\x03", 12);
  length += 12;
  
  uint32_pack_msb(8 + mrw.prd.length
		  + 8 + mrw.wbg.length
		  + 8 + mrw.rif.length
		  + 4, tmpstr + length - 8);
  memcpy(tmpstr + length, mrw.prd.data - 8, mrw.prd.length + 8);
  length += mrw.prd.length + 8;
  memcpy(tmpstr + length, mrw.wbg.data - 8, mrw.wbg.length + 8);
  length += mrw.wbg.length + 8;
  memcpy(tmpstr + length, mrw.rif.data - 8, mrw.rif.length + 8);
  length += mrw.rif.length + 8;
  tiff_ifd_add_byte(&mainifd, DNGPrivateData, length, tmpstr);
}

static struct tiff_tag* copy_tag(struct tiff_ifd* ifd,
				 const unsigned char* start,
				 enum tiff_tag_id tag,
				 enum tiff_tag_type type,
				 uint32 count,
				 uint32 value)
{
  struct tiff_tag* newtag;
  uint32 i;
  
  newtag = tiff_ifd_add(ifd, tag, type, count);
    
  switch (type) {
  case ASCII:
  case UNDEFINED:
    if (count > 4)
      memcpy(newtag->data, start + value, newtag->size);
    else {
      for (i = 0; i < count; ++i, value <<= 8)
	newtag->data[i] = value >> 24;
    }
    break;
  case SHORT:
  case SSHORT:
    if (count == 1)
      uint16_pack_lsb(value >> 16, newtag->data);
    else if (count == 2) {
      uint16_pack_lsb(value >> 16, newtag->data);
      uint16_pack_lsb(value, newtag->data + 2);
    }
    else
      for (i = 0; i < count; ++i)
	uint16_pack_lsb(uint16_get_msb(start + value + i * 2),
			newtag->data + i * 2);
    break;
  case RATIONAL:
  case SRATIONAL:
    for (i = 0; i < count; ++i) {
      uint32_pack_lsb(uint32_get_msb(start + value + i * 8),
		      newtag->data + i * 8);
      uint32_pack_lsb(uint32_get_msb(start + value + i * 8 + 4),
		      newtag->data + i * 8 + 4);
    }
    break;
  case LONG:
    if (count == 1)
      uint32_pack_lsb(value, newtag->data);
    else
      for (i = 0; i < count; ++i)
	uint32_pack_lsb(uint32_get_msb(start + value + i * 4),
			newtag->data + i * 4);
    break;
  default:
    warn(0, "Unhandled SubEXIF type #%d", type);
  }
  return newtag;
}

static void parse_ttw_iop(const unsigned char* start,
			  enum tiff_tag_id tag,
			  enum tiff_tag_type type,
			  uint32 count,
			  uint32 value)
{
  copy_tag(&iopifd, start, tag, type, count, value);
}

static void parse_ttw_subtag(const unsigned char* start,
			     enum tiff_tag_id tag,
			     enum tiff_tag_type type,
			     uint32 count,
			     uint32 value)
{
  switch (tag) {
  case MakerNote:
    parse_ifd(start, value, parse_ttw_makernote);
    add_makernote(start + value, value, count);
    break;
  case InteroperabilityIFD:
    iop_offset_tag = tiff_ifd_add_long(&exififd, tag, 1, 0);
    parse_ifd(start, value, parse_ttw_iop);
    break;
  default:
    copy_tag(&exififd, start, tag, type, count, value);
  }
}

static void parse_ttw_tag(const unsigned char* start,
			  enum tiff_tag_id tag,
			  enum tiff_tag_type type,
			  uint32 count,
			  uint32 value)
{
  switch (tag) {
  case ImageWidth:
  case ImageLength:
  case Compression:
    break;
    
  case DateTime:
  case ImageDescription:
  case Make:
  case Model:
  case Software:
    tiff_ifd_add_ascii(&mainifd, tag, (const char*)start + value);
    break;
  case ExifIFD:
    parse_ifd(start, value, parse_ttw_subtag);
    break;
  case Orientation:
    tiff_ifd_add_short(&mainifd, tag, 1, value >> 16);
    break;
  case PrintIM:
    tiff_ifd_add_undefined(&exififd, tag, count,
			   (const char*)start + value);
    break;
  case XResolution:
  case YResolution:
  case ResolutionUnit:
    break;
  default:
    warn(0, "Unhandled EXIF tag #%d", tag);
  }
  (void)type;
}

static void parse_ifd(const unsigned char* start,
		      uint32 offset,
		      void (*fn)(const unsigned char* start,
				 enum tiff_tag_id tag,
				 enum tiff_tag_type type,
				 uint32 count,
				 uint32 value))
{
  uint32 entries;
  enum tiff_tag_id tag;
  enum tiff_tag_type type;
  uint32 count;
  uint32 value;

  for (entries = uint16_get_msb(start + offset), offset += 2;
       entries > 0;
       --entries, offset += 12) {
    tag = uint16_get_msb(start + offset);
    type = uint16_get_msb(start + offset + 2);
    count = uint32_get_msb(start + offset + 4);
    value = uint32_get_msb(start + offset + 8);

    fn(start, tag, type, count, value);
  }
}

static void parse_ttw(void)
{
  if (memcmp(mrw.ttw.data, "MM\0\052\0\0\0\010", 8) != 0)
    die(1, "Invalid TTW block format");

  parse_ifd(mrw.ttw.data, 8, parse_ttw_tag);

  if (thumbnail_start != 0
      && thumbnail_length > 0) {

    tiff_ifd_add_long(&mainifd, ImageWidth, 1, 640);
    tiff_ifd_add_long(&mainifd, ImageLength, 1, 480);
    tiff_ifd_add_short(&mainifd, BitsPerSample, 3, 8, 8, 8);
    tiff_ifd_add_short(&mainifd, Compression, 1, 7);
    tiff_ifd_add_short(&mainifd, PhotometricInterpretation, 1, 6 /* 2 */);
    thumbnail_offset_tag = tiff_ifd_add_long(&mainifd, StripOffset, 1, 0);
    tiff_ifd_add_short(&mainifd, SamplesPerPixel, 1, 3);
    tiff_ifd_add_long(&mainifd, RowsPerStrip, 1, 480);
    tiff_ifd_add_long(&mainifd, StripByteCounts, 1, thumbnail_length);
    tiff_ifd_add_short(&mainifd, PlanarConfiguration, 1, 1);
    tiff_ifd_add_short(&mainifd, YCbCrSubSampling, 2, 2, 1);
    tiff_ifd_add_rational(&mainifd, RefBlackWhite, 6,
			  0,1, 255,1, 128,1, 255,1, 128,1, 255,1);

    tiff_ifd_add_rational(&mainifd, YCbCrCoefficients, 3,
			  299,1000, 587,1000, 114,1000);
    tiff_ifd_add_short(&mainifd, YCbCrPositioning, 1, 2);
  }
}

static void parse_wbg(void)
{
  double r;
  double g;
  double b;
  r = uint16_get_msb(mrw.wbg.data + 4) * 1.0 / (64 << mrw.wbg.data[0]);
  g = uint16_get_msb(mrw.wbg.data + 6) * 1.0 / (64 << mrw.wbg.data[1])
    + uint16_get_msb(mrw.wbg.data + 8) * 1.0 / (64 << mrw.wbg.data[2]);
  g /= 2.0;
  b = uint16_get_msb(mrw.wbg.data + 10) * 1.0 / (64 << mrw.wbg.data[3]);

  tiff_ifd_add_rational(&mainifd, AnalogBalance, 3,
			1000000, 1000000,
			1000000, 1000000,
			1000000, 1000000);
  tiff_ifd_add_rational(&mainifd, AsShotNeutral, 3,
			(uint32)(1000000 / r), 1000000,
			(uint32)(1000000 / g), 1000000,
			(uint32)(1000000 / b), 1000000);
}

static void start_dng(const char* filename)
{
  uint16 s;

  tzset();
  s = -timezone / 60 / 60;

  tiff_ifd_add_long(&mainifd, NewSubfileType, 1, 1);
  tiff_ifd_add_sshort(&mainifd, TimeZoneOffset, 2, s, s);
  tiff_ifd_add_byte(&mainifd, DNGVersion, 4, "\1\1\0\0");
  tiff_ifd_add_byte(&mainifd, DNGBackwardVersion, 4, "\1\1\0\0");
  tiff_ifd_add_ascii(&mainifd, OriginalRawFileName, filename);
  /* FIXME: are these all really constant? */
  tiff_ifd_add_srational(&mainifd, BaselineExposure, 1, -50,100);
  tiff_ifd_add_rational(&mainifd, BaselineNoise, 1, 133,100);
  tiff_ifd_add_rational(&mainifd, BaselineSharpness, 1, 133,100);
  tiff_ifd_add_rational(&mainifd, LinearResponseLimit, 1, 100,100);
  tiff_ifd_add_rational(&mainifd, ShadowScale, 1, 1,1);
  tiff_ifd_add_short(&mainifd, CalibrationIlluminant1, 1, 17);
  tiff_ifd_add_short(&mainifd, CalibrationIlluminant2, 1, 21);
  tiff_ifd_add_srational(&mainifd, ColorMatrix1, 9,
			 12036,10000, -4954,10000, -75,10000,
			 -7019,10000, 14449,10000, 2811,10000,
			 -513,10000, 635,10000, 6839,10000);
  tiff_ifd_add_srational(&mainifd, ColorMatrix2, 9,
			 10239,10000, -3104,10000, -1099,10000,
			 -8037,10000, 15727,10000, 2451,10000,
			 -927,10000, 925,10000, 6871,10000);
  
  tiff_ifd_add_long(&subifd1, NewSubfileType, 1, 0);
  tiff_ifd_add_short(&subifd1, PhotometricInterpretation, 1, 32803);
  tiff_ifd_add_short(&subifd1, BitsPerSample, 1, 16);
  tiff_ifd_add_long(&subifd1, BayerGreenSplit, 1, 500);
  tiff_ifd_add_short(&subifd1, PlanarConfiguration, 1, 1);
  tiff_ifd_add_short(&subifd1, Compression, 1, opt_compress ? 7 : 1);
  tiff_ifd_add_short(&subifd1, SamplesPerPixel, 1, 1);
  tiff_ifd_add_rational(&subifd1, AntiAliasStrength, 1, 100, 100);
  tiff_ifd_add_rational(&subifd1, BestQualityScale, 1, 1, 1);
  tiff_ifd_add_short(&subifd1, BlackLevelRepeatDim, 2, 1, 1);
  tiff_ifd_add_rational(&subifd1, BlackLevel, 1, 0, 256);
  tiff_ifd_add_short(&subifd1, WhiteLevel, 1, 4095);

#if PREVIEW
  tiff_ifd_add_long(&subifd2, NewSubfileType, 1, 1);
  tiff_ifd_add_short(&subifd2, PhotometricInterpretation, 1, 6);
  tiff_ifd_add_short(&subifd2, PlanarConfiguration, 1, 1);
  tiff_ifd_add_short(&subifd2, SamplesPerPixel, 1, 3);
  tiff_ifd_add_short(&subifd2, BitsPerSample, 3, 8, 8, 8);
  tiff_ifd_add_short(&subifd2, Compression, 1, 7);
  tiff_ifd_add_rational(&subifd2, RefBlackWhite, 6,
			0, 1, 255, 1, 128, 1, 255, 1, 128, 1, 255, 1);
  tiff_ifd_add_rational(&subifd2, YCbCrCoefficients, 3,
			299,1000, 587,1000, 114,1000);
  tiff_ifd_add_short(&subifd2, YCbCrSubSampling, 2, 2, 2);
  tiff_ifd_add_short(&subifd2, YCbCrPositioning, 1, 2);
#endif
}

static void end_dng(void)
{
  uint32 end;
  struct tiff_tag* sub_tag;
  struct tiff_tag* exif_tag;
  uint32 tile;

  sub_tag = tiff_ifd_add(&mainifd, SubIFDs, LONG, 1 + !!PREVIEW);
  exif_tag = tiff_ifd_add(&mainifd, ExifIFD, LONG, 1);

  end = 8 + tiff_ifd_size(&mainifd);

  uint32_pack_lsb(end, sub_tag->data);
  end += tiff_ifd_size(&subifd1);
#if PREVIEW
  uint32_pack_lsb(end, sub_tag->data + 4);
  end += tiff_ifd_size(&subifd2);
#endif
  uint32_pack_lsb(end, exif_tag->data);
  end += tiff_ifd_size(&exififd);

  if (iop_offset_tag != 0) {
    uint32_pack_lsb(end, iop_offset_tag->data);
    end += tiff_ifd_size(&iopifd);
  }
  
  uint32_pack_lsb(end, thumbnail_offset_tag->data);
  end += thumbnail_length;

  if (tile_count > 1) {
    for (tile = 0; tile < tile_count; ++tile) {
      uint32_pack_lsb(end, raw_offset_tag->data + tile * 4);
      end += uint32_get_lsb(raw_length_tag->data + tile * 4);
    }
  }
  else
    uint32_pack_lsb(end, raw_offset_tag->data);
}

static uint32 compress_block(struct stream* out,
			     uint32 xoffset,
			     uint32 width,
			     uint32 yoffset,
			     uint32 height)
{
  uint32 length;
  
  stream_init(out);
  jpeg_ls_encode(out,
		 mrw.raw + xoffset + yoffset * mrw.width,
		 height,
		 width / 2,
		 2,
		 12,
		 mrw.width,
		 2);
  if ((length = stream_length(out)) & 1) {
    stream_putc(out, 0);
    ++length;
  }
  return length;
}

inline uint32 minu(uint32 a, uint32 b)
{
  return (a < b) ? a : b;
}

static void parse_raw(void)
{
  int tile_w;
  int tile_h;
  uint32 x;
  uint32 y;
  int tile;
  uint32 raw_size;

  if (opt_compress) {
    if (opt_tile) {
      tile_w = (mrw.width + opt_tile_width - 1) / opt_tile_width;
      tile_h = (mrw.height + opt_tile_height - 1) / opt_tile_height;
      tile_count = tile_w * tile_h;
      compressed_data = malloc(tile_count * sizeof *compressed_data);

      tiff_ifd_add_long(&subifd1, TileWidth, 1, opt_tile_width);
      tiff_ifd_add_long(&subifd1, TileHeight, 1, opt_tile_height);
      raw_offset_tag = tiff_ifd_add(&subifd1, TileOffsets, LONG, tile_count);
      raw_length_tag = tiff_ifd_add(&subifd1, TileByteCounts, LONG, tile_count);

      for (tile = 0, y = 0; y < mrw.height; y += opt_tile_height) {
	for (x = 0; x < mrw.width; x += opt_tile_width, ++tile) {
	  raw_size = compress_block(&compressed_data[tile],
				    x,
				    minu(mrw.width - x, opt_tile_width),
				    y,
				    minu(mrw.height - y, opt_tile_height));
	  uint32_pack_lsb(raw_size, raw_length_tag->data + tile * 4);
	}
      }
    }
    else {
      tile_count = 1;
      compressed_data = malloc(sizeof *compressed_data);
      raw_size = compress_block(compressed_data, 0, mrw.width, 0, mrw.height);

      raw_offset_tag = tiff_ifd_add_long(&subifd1, StripOffset, 1, 0);
      tiff_ifd_add_long(&subifd1, RowsPerStrip, 1, mrw.height);
      tiff_ifd_add_long(&subifd1, StripByteCounts, 1, raw_size);
    }
  }
  else {
    raw_size = mrw.width * mrw.height * 2;

    tile_count = 1;
    raw_offset_tag = tiff_ifd_add_long(&subifd1, StripOffset, 1, 0);
    tiff_ifd_add_long(&subifd1, RowsPerStrip, 1, mrw.height);
    tiff_ifd_add_long(&subifd1, StripByteCounts, 1, raw_size);
  }
}

static void parse_file(void)
{
  parse_prd();
  parse_ttw();
  parse_wbg();
  /* The data in the RIF block is duplicated by the EXIF data in the TTW
   * block, which is copied in parse_ttw above. */
  parse_raw();
}

static void write_thumbnail(FILE* out)
{
  /* The embeded thumbnail appears to have a garbled JPEG SOI marker. */
  fwrite("\xff\xd8", 1, 2, out);
  fwrite(thumbnail_start + 2, 1, thumbnail_length - 2, out);
}

static void write_image(FILE* out)
{
  const struct stream_buffer* b;
  uint32 tile;
  
  if (opt_compress) {
    for (tile = 0; tile < tile_count; ++tile) {
      for (b = compressed_data[tile].head; b != 0; b = b->next)
	fwrite(b->data, 1, b->count, out);
    }
  }
  else
    fwrite(mrw.raw, 2, mrw.width * mrw.height, out);
}

static const struct option long_options[] = {
  { "compress", no_argument, &opt_compress, 1 },
  { "no-compress", no_argument, &opt_compress, 0 },
  { "tile", no_argument, &opt_tile, 1 },
  { "no-tile", no_argument, &opt_tile, 0 },
  { "tile-height", required_argument, 0, 'h' },
  { "tile-width", required_argument, 0, 'w' },
  { 0, 0, 0, 0 }
};

int main(int argc, char* argv[])
{
  FILE* in;
  FILE* out;
  int ch;

  while ((ch = getopt_long(argc, argv, "cCtTw:h:", long_options, 0)) != -1) {
    switch (ch) {
    case 0: break;
    case 'c': opt_compress = 1; break;
    case 'C': opt_compress = 0; break;
    case 't': opt_tile = 1; break;
    case 'T': opt_tile = 0; break;
    case 'h':
      if ((opt_tile_width = strtoul(optarg, 0, 10)) < 16)
	die(1, "Invalid tile width: %s", optarg);
      break;
    case 'w':
      if ((opt_tile_width = strtoul(optarg, 0, 10)) < 16)
	die(1, "Invalid tile width: %s", optarg);
      break;
    default:
      die_usage();
    }
  }

  if (argc - optind != 2)
    die_usage();
  
  if ((in = fopen(argv[optind], "rb")) == 0)
    die(-1, "Could not open '%s' for reading", argv[optind]);

  if (!mrw_load(&mrw, in))
    die(1, "Error while loading MRW file");
  fclose(in);

  start_dng(argv[0]);
  parse_file();
  end_dng();

  if ((out = fopen(argv[optind + 1], "wb")) == 0)
    die(-1, "Could not open '%s' for writing", argv[optind + 1]);
  tiff_start(out, 8);
  tiff_write_ifd(out, &mainifd);
  tiff_write_ifd(out, &subifd1);
#if PREVIEW
  tiff_write_ifd(out, &subifd2);
#endif
  tiff_write_ifd(out, &exififd);
  if (iop_offset_tag != 0)
    tiff_write_ifd(out, &iopifd);
  write_thumbnail(out);
  write_image(out);

  if (fclose(out) != 0)
    die(-1, "Could not write output");

  return 0;
  (void)argc;
}
