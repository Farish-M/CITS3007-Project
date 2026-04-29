#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bun.h"

typedef struct {
  BunParseContext *ctx;
  FILE *file;
  u32 name_offset;
  u32 name_length;
  u64 string_table_offset;
  u64 string_table_size;
  char *name_buf;
} AssetNameMetadata;

static void add_error();
static bun_result_t worst_error();
static u32 read_u32_le();
static u64 read_u64_le();
static int isMagic();
static int validVersion();
static int sectionsAligned();
static bun_result_t validate_rle_data();
static bun_result_t AssetNameCheck();
static bun_result_t OverlapCheck();
static BunAssetRecord parse_asset_record();


/**
 * Example helper: convert 4 bytes in `buf`, positioned at `offset`,
 * into a little-endian u32.
 */

static void add_error(BunParseContext *ctx, const char *msg) {
  if (ctx->error_count < MAX_ERRORS) {
    ctx->errors[ctx->error_count++] = msg;
  }
}

// Accumulates new results, and prioritise the "worst" error
// Priority: BUN_ERROR_IO > BUN_MALFORMED > BUN_SUPPORTED
// If no errors then BUN_OK
static bun_result_t worst_error(bun_result_t current, bun_result_t incoming) {
  if (incoming == BUN_ERR_IO || current == BUN_ERR_IO) {
    return BUN_ERR_IO;
  }
  if (incoming == BUN_MALFORMED || current == BUN_MALFORMED) {
    return BUN_MALFORMED;
  }
  if (incoming == BUN_UNSUPPORTED || current == BUN_UNSUPPORTED) {
    return BUN_UNSUPPORTED;
  }
  return BUN_OK;
}

static u32 read_u32_le(const u8 *buf, size_t offset) {
  return (u32)buf[offset] | (u32)buf[offset + 1] << 8 |
         (u32)buf[offset + 2] << 16 | (u32)buf[offset + 3] << 24;
}

static u64 read_u64_le(const u8 *b, size_t o) {
  return (u64)b[o] | (u64)b[o + 1] << 8 | (u64)b[o + 2] << 16 |
         (u64)b[o + 3] << 24 | (u64)b[o + 4] << 32 | (u64)b[o + 5] << 40 |
         (u64)b[o + 6] << 48 | (u64)b[o + 7] << 56;
}

bun_result_t AllFileStatus= BUN_OK;

static int isMagic(const BunHeader *header) {
  return header->magic == BUN_MAGIC;
}

static int validVersion(const BunHeader *header) {
  return header->version_major == BUN_VERSION_MAJOR &&
         header->version_minor == BUN_VERSION_MINOR;
}

static int sectionsAligned(const BunHeader *header) {
  return (header->asset_table_offset % 4 == 0 &&
          header->string_table_offset % 4 == 0 &&
          header->data_section_offset % 4 == 0 &&
          header->string_table_size % 4 == 0 &&
          header->data_section_size % 4 == 0);
}
//
// API implementation
//

bun_result_t bun_open(const char *path, BunParseContext *ctx) {
  // we open the file; seek to the end, to get the size; then jump back to the
  // beginning, ready to start parsing.

  ctx->file = fopen(path, "rb");
  if (!ctx->file) {
    return BUN_ERR_IO;
  }

  ctx->error_count = 0;

  if (fseek(ctx->file, 0, SEEK_END) != 0) {
    fclose(ctx->file);
    return BUN_ERR_IO;
  }
  ctx->file_size = ftell(ctx->file);
  if (ctx->file_size < 0) {
    fclose(ctx->file);
    return BUN_ERR_IO;
  }
  rewind(ctx->file);

  return BUN_OK;
}

bun_result_t bun_parse_header(BunParseContext *ctx, BunHeader *header) {
  u8 buf[BUN_HEADER_SIZE];

  // our file is far too short, and cannot be valid!
  // (query: how do we let `main` know that "file was too short"
  // was the exact problem? Where can we put details about the
  // exact validation problem that occurred?)
  if (ctx->file_size < (long)BUN_HEADER_SIZE) {
    add_error(ctx, "Truncated file");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  // slurp the header into `buf`
  if (fread(buf, 1, BUN_HEADER_SIZE, ctx->file) != BUN_HEADER_SIZE) {
    add_error(ctx, "Failed to read header");
    AllFileStatus= worst_error(AllFileStatus, BUN_ERR_IO);
  }

  // TODO: populate `header` from `buf`.
  size_t offset = 0;
  header->magic = read_u32_le(buf, offset);
  offset += 4;

  header->version_major = (buf[offset] | (buf[offset + 1] << 8));
  offset += 2;
  header->version_minor = (buf[offset] | (buf[offset + 1] << 8));
  offset += 2;

  header->asset_count = read_u32_le(buf, offset);
  offset += 4;

  header->asset_table_offset = read_u64_le(buf, offset);
  offset += 8;
  header->string_table_offset = read_u64_le(buf, offset);
  offset += 8;
  header->string_table_size = read_u64_le(buf, offset);
  offset += 8;
  header->data_section_offset = read_u64_le(buf, offset);
  offset += 8;
  header->data_section_size = read_u64_le(buf, offset);
  offset += 8;
  header->reserved = read_u64_le(buf, offset);

  // TODO: validate fields and return BUN_MALFORMED or BUN_UNSUPPORTED
  // as required by the spec. The magic check is a good place to start.

  if (!isMagic(header)) {
    add_error(ctx, "Invalid magic number");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  if (!validVersion(header)) {
    add_error(ctx, "Invalid version");
    AllFileStatus= worst_error(AllFileStatus, BUN_UNSUPPORTED);
  }

  return AllFileStatus;
}

/**
 * @brief Validates the integrity of RLE-compressed asset data.
 *
 * @param ctx Pointer to the current parse context for error reporting.
 * @param header Pointer to the parsed BUN header for section offsets.
 * @param r Pointer to the asset record being validated.
 * @return BUN_OK if valid, BUN_MALFORMED on spec violation, or BUN_ERR_IO on
 * read error.
 */
static bun_result_t validate_rle_data(BunParseContext *ctx,
                                      const BunHeader *header,
                                      const BunAssetRecord *r) {
  // RLE data must be an even amount of bytes
  if (r->data_size % 2 != 0) {
    add_error(ctx, "RLE data size is not even");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  // Save current file position to get back to later
  long saved_pos = ftell(ctx->file);
  u64 data_start_abs = header->data_section_offset + r->data_offset;

  if (saved_pos < 0) {
    add_error(ctx, "ftell failed before RLE validation");
    AllFileStatus= worst_error(AllFileStatus, BUN_ERR_IO);
  }
  if (fseek(ctx->file, data_start_abs, SEEK_SET) != 0) {
    add_error(ctx, "Failed to seek to RLE data");
    AllFileStatus= worst_error(AllFileStatus, BUN_ERR_IO);
  }

  // Getting the actual size of the data
  u64 total_expanded = 0;
  for (u64 j = 0; j < r->data_size; j += 2) {
    int count = fgetc(ctx->file);
    int value = fgetc(ctx->file);

    if (count == EOF || value == EOF) {
      add_error(ctx, "Unexpected EOF in RLE data");
      // Cleaning up
      fseek(ctx->file, saved_pos, SEEK_SET);
      AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
      break;
    }

    // A count of zero is a spec violation
    if (count == 0) {
      add_error(ctx, "RLE pair has zero count");
      fseek(ctx->file, saved_pos, SEEK_SET);
      AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
    }

    total_expanded += (unsigned char)count;
    if (total_expanded > r->uncompressed_size) {
      add_error(ctx, "RLE data is bigger than specified uncompressed_size");
      AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
    }
  }

  // Check if our data size matches the header
  if (total_expanded != r->uncompressed_size) {
    add_error(ctx, "RLE expanded size mismatch");
    fseek(ctx->file, saved_pos, SEEK_SET);
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  // Restore original file position
  fseek(ctx->file, saved_pos, SEEK_SET);
  return BUN_OK;
}

static bun_result_t AssetNameCheck(AssetNameMetadata *meta) {
  if (meta->name_length == 0) {
    add_error(meta->ctx, "Name does not exist");
    return BUN_MALFORMED;
  }

  if ((u64)meta->name_offset + (u64)meta->name_length > meta->string_table_size) {
    add_error(meta->ctx, "Asset name out of string table bounds");
    return BUN_MALFORMED;
  }

  if (meta->name_length > 255) {
    add_error(meta->ctx, "Asset name too large for buffer");
    return BUN_MALFORMED;
  }

  long saved_pos = ftell(meta->file);
  if (saved_pos < 0) {
    add_error(meta->ctx, "ftell failed before reading asset name");
    return BUN_ERR_IO;
  }

  u64 name_start_abs = meta->string_table_offset + meta->name_offset;
  if (fseek(meta->file, name_start_abs, SEEK_SET) != 0) {
    add_error(meta->ctx, "Failed to seek to asset name");
    fseek(meta->file, saved_pos, SEEK_SET);
    return BUN_MALFORMED;
  }

  if (fread(meta->name_buf, 1, meta->name_length, meta->file) != meta->name_length) {
    add_error(meta->ctx, "Failed to read asset name");
    fseek(meta->file, saved_pos, SEEK_SET);
    return BUN_MALFORMED;
  }

  fseek(meta->file, saved_pos, SEEK_SET);

  for (u32 j = 0; j < meta->name_length; j++) {
    unsigned char c = (unsigned char)meta->name_buf[j];
    if (c < 32 || c > 126) {
      add_error(meta->ctx, "Non-printable asset name");
      return BUN_MALFORMED;
    }
  }

  return BUN_OK;
}

static bun_result_t OverlapCheck(BunParseContext *ctx,
                                 u64 assetTableStart, u64 assetTableEnd,
                                 u64 stringTableStart, u64 stringTableEnd,
                                 u64 dataTableStart, u64 dataTableEnd) {
  bun_result_t status = BUN_OK;

  if (assetTableEnd > stringTableStart && assetTableStart < stringTableEnd) {
    add_error(ctx, "Asset and string table overlap");
    status = worst_error(status, BUN_MALFORMED);
  }

  if (assetTableEnd > dataTableStart && assetTableStart < dataTableEnd) {
    add_error(ctx, "Asset and data section overlap");
    status = worst_error(status, BUN_MALFORMED);
  }

  if (stringTableEnd > dataTableStart && stringTableStart < dataTableEnd) {
    add_error(ctx, "String and data section overlap");
    status = worst_error(status, BUN_MALFORMED);
  }

  return status;
}

static BunAssetRecord parse_asset_record(const u8 *buf) {
  BunAssetRecord r;
  size_t o = 0;
  r.name_offset       = read_u32_le(buf, o); o += 4;
  r.name_length       = read_u32_le(buf, o); o += 4;
  r.data_offset       = read_u64_le(buf, o); o += 8;
  r.data_size         = read_u64_le(buf, o); o += 8;
  r.uncompressed_size = read_u64_le(buf, o); o += 8;
  r.compression       = read_u32_le(buf, o); o += 4;
  r.type              = read_u32_le(buf, o); o += 4;
  r.checksum          = read_u32_le(buf, o); o += 4;
  r.flags             = read_u32_le(buf, o);
  return r;
}

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {
  bun_result_t AllFileStatus= BUN_OK;
  u64 file_size = (u64)ctx->file_size;

  // TODO: implement asset record parsing and validation
  if (!sectionsAligned(header)) {
    add_error(ctx, "Misaligned section offset or size");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  if (fseek(ctx->file, header->asset_table_offset, SEEK_SET) != 0) {
    add_error(ctx, "Failed to seek asset table");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  if (header->asset_table_offset > (u64)ctx->file_size) {
    add_error(ctx, "Invalid asset table offset");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  if (header->data_section_offset > (u64)ctx->file_size) {
    add_error(ctx, "Invalid data section offset");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  if (header->string_table_offset > (u64)ctx->file_size) {
    add_error(ctx, "Invalid string table offset");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  u32 counttest = header->asset_count;
  fprintf(stderr, "[DEBUG] asset_count=%" PRIu32 "\n", counttest);

  u64 assetTableStart = header->asset_table_offset;
  u64 assetTableEnd =
      assetTableStart + (u64)header->asset_count * BUN_ASSET_RECORD_SIZE;

  u64 stringTableStart = header->string_table_offset;
  u64 stringTableEnd = stringTableStart + header->string_table_size;

  u64 dataTableStart = header->data_section_offset;
  u64 dataTableEnd = dataTableStart + header->data_section_size;

  if (assetTableEnd > file_size) {
    add_error(ctx, "Asset entry table exceeds EOF");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  if (stringTableEnd > file_size) {
    add_error(ctx, "String table exceeds EOF");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  if (dataTableEnd > file_size) {
    add_error(ctx, "Data section exceeds EOF");
    AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
  }

  AllFileStatus = worst_error(AllFileStatus, OverlapCheck(ctx,
      assetTableStart, assetTableEnd,
      stringTableStart, stringTableEnd,
      dataTableStart, dataTableEnd));

  if (fseek(ctx->file, (long)assetTableStart, SEEK_SET) != 0) {
    add_error(ctx, "Failed to seek asset table");
    AllFileStatus= worst_error(AllFileStatus, BUN_ERR_IO);
  }

  for (u32 i = 0; i < header->asset_count; i++) {
    u8 buf[BUN_ASSET_RECORD_SIZE];

    long next_record_pos =
        (long)(assetTableStart + (u64)(i + 1) * BUN_ASSET_RECORD_SIZE);

    if (fread(buf, 1, BUN_ASSET_RECORD_SIZE, ctx->file) !=
        BUN_ASSET_RECORD_SIZE) {
      add_error(ctx, "Unexpected EOF in asset record");
      return BUN_MALFORMED;
    }

    BunAssetRecord AssetContent = parse_asset_record(buf);

    char name[256] = "<no name>";
    AssetNameMetadata name_meta = {
        .ctx = ctx,
        .file = ctx->file,
        .name_offset = AssetContent.name_offset,
        .name_length = AssetContent.name_length,
        .string_table_offset = header->string_table_offset,
        .string_table_size = header->string_table_size,
        .name_buf = name,
    };
    
    AllFileStatus= worst_error(AllFileStatus, AssetNameCheck(&name_meta));

    if (fseek(ctx->file, next_record_pos, SEEK_SET) != 0) {
      add_error(ctx, "Failed to seek asset table for next record");
      return worst_error(AllFileStatus, BUN_ERR_IO);
    }

    if (AssetContent.data_offset + AssetContent.data_size >
        header->data_section_size) {
      add_error(ctx, "Asset data out of bounds");
      AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
    }

    // Compression checks
    if (AssetContent.compression == 1) {
      // We use &AssetContent here because validate_rle_data needs a pointer
      bun_result_t rle_res = validate_rle_data(ctx, header, &AssetContent);
      if (rle_res != BUN_OK) {
        return rle_res;
      }
    } else if (AssetContent.compression == 0 &&
               AssetContent.uncompressed_size != 0) {
      add_error(
          ctx,
          "Can't have non-zero uncompressed size for an uncompressed asset");
      AllFileStatus= worst_error(AllFileStatus, BUN_MALFORMED);
    }

    printf("------------ Asset %u ------------\n", i);
    printf("Name:                %s\n", name);
    printf("Type:                %u\n", AssetContent.type);
    printf("Size:                %llu\n",
           (unsigned long long)AssetContent.data_size);
    printf("Uncompressed Size:   %llu\n",
           (unsigned long long)AssetContent.uncompressed_size);
    printf("Compression:         %u\n", AssetContent.compression);
    printf("Checksum:            0x%08X\n", AssetContent.checksum);
    printf("Flags:               0x%08X\n\n", AssetContent.flags);
  }

  return AllFileStatus;
}

bun_result_t bun_close(BunParseContext *ctx) {
  int res = fclose(ctx->file);
  ctx->file = NULL;
  return res ? BUN_ERR_IO : BUN_OK;
}
