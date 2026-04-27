#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#include "bun.h"

/**
 * Example helper: convert 4 bytes in `buf`, positioned at `offset`,
 * into a little-endian u32.
 */

 static void add_error(BunParseContext *ctx, const char *msg) {
  if (ctx->error_count < MAX_ERRORS) {
    ctx->errors[ctx->error_count++] = msg;
  }
}

static u32 read_u32_le(const u8 *buf, size_t offset) {
  return (u32)buf[offset]
     | (u32)buf[offset + 1] << 8
     | (u32)buf[offset + 2] << 16
     | (u32)buf[offset + 3] << 24;
}

static u64 read_u64_le(const u8 *b, size_t o) {
  return (u64)b[o]
     | (u64)b[o + 1] << 8
     | (u64)b[o + 2] << 16
     | (u64)b[o + 3] << 24
     | (u64)b[o + 4] << 32
     | (u64)b[o + 5] << 40
     | (u64)b[o + 6] << 48
     | (u64)b[o + 7] << 56;
}

static int isMagic(const BunHeader *header) {
  return header->magic == BUN_MAGIC;
}

static int validVersion(const BunHeader *header) {
  return header->version_major == BUN_VERSION_MAJOR;
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
    return BUN_MALFORMED;
  }

  // slurp the header into `buf`
  if (fread(buf, 1, BUN_HEADER_SIZE, ctx->file) != BUN_HEADER_SIZE) {
    add_error(ctx, "Failed to read header");
    return BUN_ERR_IO;
  }

  // TODO: populate `header` from `buf`.
  size_t offset = 0;
  header->magic = read_u32_le(buf, offset);
  offset+= 4;

  header->version_major = (buf[offset] | (buf[offset + 1] << 8));
  offset += 2;
  header->version_minor = (buf[offset] | (buf[offset + 1] << 8));
  offset += 2;

  header->asset_count = read_u32_le(buf, offset);
  offset+= 4;

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
    return BUN_MALFORMED;
  }

  if (!validVersion(header)) {
    add_error(ctx, "Invalid version");
    return BUN_UNSUPPORTED;
  }

  return BUN_OK;
}

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {

  // TODO: implement asset record parsing and validation
  if (!sectionsAligned(header)) {
    add_error(ctx, "Misaligned section offset or size");
    return BUN_MALFORMED;
  }

  if(fseek(ctx->file, header-> asset_table_offset, SEEK_SET) != 0) {
    add_error(ctx, "Failed to seek asset table");
    return BUN_MALFORMED;
  }

  if(header->asset_table_offset > (u64)ctx-> file_size) {
    add_error(ctx, "Invalid asset table offset");
    return BUN_MALFORMED;
  }

  if(header->data_section_offset > (u64)ctx->file_size) {
    add_error(ctx, "Invalid data section offset");
    return BUN_MALFORMED;
  }

  if(header->string_table_offset > (u64)ctx->file_size) {
    add_error(ctx, "Invalid string table offset");
    return BUN_MALFORMED;
  }

  u32 counttest = header->asset_count;
  fprintf(stderr, "[DEBUG] asset_count=%" PRIu32 "\n", counttest);
  
  u64 assetTableStart = header->asset_table_offset;
  u64 assetTableEnd = assetTableStart + (u64)header->asset_count * BUN_ASSET_RECORD_SIZE;

  u64 stringTableStart = header->string_table_offset;
  u64 stringTableEnd = stringTableStart + header->string_table_size;

  u64 dataTableStart = header->data_section_offset;
  u64 dataTableEnd = dataTableStart + header->data_section_size;

  fprintf(stderr, "[DEBUG] assetTableStart=%" PRIu64 " assetTableEnd=%" PRIu64 "\n", assetTableStart, assetTableEnd);
  fprintf(stderr, "[DEBUG] stringTableStart=%" PRIu64 " stringTableEnd=%" PRIu64 "\n", stringTableStart, stringTableEnd);
  fprintf(stderr, "[DEBUG] dataTableStart=%" PRIu64 " dataTableEnd=%" PRIu64 "\n", dataTableStart, dataTableEnd);

  if (assetTableEnd > stringTableStart && assetTableStart < stringTableEnd) {
    add_error(ctx, "Asset and string table overlap");
    return BUN_MALFORMED;
  }

  if (assetTableEnd > dataTableStart && assetTableStart < dataTableEnd) {
    add_error(ctx, "Asset and data section overlap");
    return BUN_MALFORMED;
  }

  if (stringTableEnd > dataTableStart && stringTableStart < dataTableEnd) {
    add_error(ctx, "String and data section overlap");
    return BUN_MALFORMED;
  }


  return BUN_OK;
}

bun_result_t bun_parse_asset(BunParseContext *ctx, const BunHeader *header,
                              u32 i, BunAssetRecord *out_record,
                              char *name_buf, size_t name_buf_size) {
  u8 buf[BUN_ASSET_RECORD_SIZE];

  u64 record_offset = header->asset_table_offset + (u64)i * BUN_ASSET_RECORD_SIZE;
  if (fseek(ctx->file, (long)record_offset, SEEK_SET) != 0) {
    add_error(ctx, "Failed to seek to asset record");
    return BUN_MALFORMED;
  }

  if (fread(buf, 1, BUN_ASSET_RECORD_SIZE, ctx->file) != BUN_ASSET_RECORD_SIZE) {
    add_error(ctx, "Unexpected EOF in asset record");
    return BUN_MALFORMED;
  }

  size_t o = 0;
  out_record->name_offset      = read_u32_le(buf, o); o += 4;
  out_record->name_length      = read_u32_le(buf, o); o += 4;
  out_record->data_offset      = read_u64_le(buf, o); o += 8;
  out_record->data_size        = read_u64_le(buf, o); o += 8;
  out_record->uncompressed_size = read_u64_le(buf, o); o += 8;
  out_record->compression      = read_u32_le(buf, o); o += 4;
  out_record->type             = read_u32_le(buf, o); o += 4;
  out_record->checksum         = read_u32_le(buf, o); o += 4;
  out_record->flags            = read_u32_le(buf, o);

  if (out_record->name_length == 0) {
    add_error(ctx, "Invalid asset name length");
    return BUN_MALFORMED;
  }
  if (out_record->name_length >= name_buf_size) {
    add_error(ctx, "Asset name too large for buffer");
    return BUN_MALFORMED;
  }
  if ((u64)out_record->name_offset + (u64)out_record->name_length > header->string_table_size) {
    add_error(ctx, "Asset name out of string table bounds");
    return BUN_MALFORMED;
  }

  u64 name_start_abs = header->string_table_offset + out_record->name_offset;
  if (fseek(ctx->file, (long)name_start_abs, SEEK_SET) != 0) {
    add_error(ctx, "Failed to seek to asset name");
    return BUN_MALFORMED;
  }
  if (fread(name_buf, 1, out_record->name_length, ctx->file) != out_record->name_length) {
    add_error(ctx, "Failed to read asset name");
    return BUN_MALFORMED;
  }
  name_buf[out_record->name_length] = '\0';

  for (u32 j = 0; j < out_record->name_length; j++) {
    unsigned char c = (unsigned char)name_buf[j];
    if (c < 32 || c > 126) {
      add_error(ctx, "Non-printable asset name");
      return BUN_MALFORMED;
    }
  }

  if (out_record->data_offset + out_record->data_size > header->data_section_size) {
    add_error(ctx, "Asset data out of bounds");
    return BUN_MALFORMED;
  }

  return BUN_OK;
}

bun_result_t bun_close(BunParseContext *ctx) {
  int res = fclose(ctx->file);
  if (res) {
    return BUN_ERR_IO;
  }
  ctx->file = NULL;
  return BUN_OK;
}
