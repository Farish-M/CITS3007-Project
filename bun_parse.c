#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

// Accumulates new results, and prioritise the "worst" error
// Priority: BUN_ERROR_IO > BUN_MALFORMED > BUN_SUPPORTED
// If no errors then BUN_OK
static bun_result_t worst_error(bun_result_t current, bun_result_t incoming) {
  if(incoming == BUN_ERR_IO || current == BUN_ERR_IO) {
    return BUN_ERR_IO;
  }
  if(incoming == BUN_MALFORMED || current == BUN_MALFORMED) {
    return BUN_MALFORMED;
  }
  if(incoming == BUN_UNSUPPORTED || current == BUN_UNSUPPORTED) {
    return BUN_UNSUPPORTED;
  }
  return BUN_OK;
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
  size_t o = 0;
  header->magic = read_u32_le(buf, o);
  o+= 4;

  header->version_major = (buf[o] | (buf[o + 1] << 8));
  o += 2;
  header->version_minor = (buf[o] | (buf[o + 1] << 8));
  o += 2;

  header->asset_count = read_u32_le(buf, o);
  o+= 4;

  header->asset_table_offset = read_u64_le(buf, o);
  o += 8;
  header->string_table_offset = read_u64_le(buf, o);
  o += 8;
  header->string_table_size = read_u64_le(buf, o);
  o += 8;
  header->data_section_offset = read_u64_le(buf, o);
  o += 8;
  header->data_section_size = read_u64_le(buf, o);
  o += 8;
  header->reserved = read_u64_le(buf, o);

  // TODO: validate fields and return BUN_MALFORMED or BUN_UNSUPPORTED
  // as required by the spec. The magic check is a good place to start.

  bun_result_t result = BUN_OK;

  if (header->magic != BUN_MAGIC) {
    add_error(ctx, "Invalid magic number");
    result = worst_error(result, BUN_MALFORMED);
  }

  if(header->version_major != BUN_VERSION_MAJOR || header->version_minor != BUN_VERSION_MINOR) {
    add_error(ctx, "Invalid version");
    result = worst_error(result, BUN_UNSUPPORTED);
  }

  return BUN_OK;
}

/**
 * @brief Validates the integrity of RLE-compressed asset data.
 *
 * @param ctx Pointer to the current parse context for error reporting.
 * @param header Pointer to the parsed BUN header for section offsets.
 * @param r Pointer to the asset record being validated.
 * @return BUN_OK if valid, BUN_MALFORMED on spec violation, or BUN_ERR_IO on read error.
 */
static bun_result_t validate_rle_data(BunParseContext *ctx, 
                                      const BunHeader *header, 
                                      const BunAssetRecord *r) {
    // RLE data must be an even amount of bytes
    if (r->data_size % 2 != 0) {
        add_error(ctx, "RLE data size is not even");
        return BUN_MALFORMED;
    }

    // Save current file position to get back to later
    long saved_pos = ftell(ctx->file);
    if (saved_pos < 0) {
      add_error(ctx, "ftell failed before RLE validation");
      return BUN_ERR_IO;
    }
    u64 data_start_abs = header->data_section_offset + r->data_offset;

    if (fseek(ctx->file, data_start_abs, SEEK_SET) != 0) {
        add_error(ctx, "Failed to seek to RLE data");
        return BUN_ERR_IO;
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
            return BUN_MALFORMED;
        }

        // A count of zero is a spec violation
        if (count == 0) {
            add_error(ctx, "RLE pair has zero count");
            fseek(ctx->file, saved_pos, SEEK_SET);
            return BUN_MALFORMED;
        }

        total_expanded += (unsigned char)count;
        if (total_expanded > r->uncompressed_size) {
            add_error(ctx, "RLE data is bigger than specified uncompressed_size");
            return BUN_MALFORMED;
        }
    }

    // Check if our data size matches the header
    if (total_expanded != r->uncompressed_size) {
        add_error(ctx, "RLE expanded size mismatch");
        fseek(ctx->file, saved_pos, SEEK_SET);
        return BUN_MALFORMED;
    }

    // Restore original file position
    fseek(ctx->file, saved_pos, SEEK_SET);
    return BUN_OK;
}

bun_result_t bun_parse_assets(BunParseContext *ctx, const BunHeader *header) {
  bun_result_t result = BUN_OK;
  u64 file_size = (u64)ctx->file_size;

  // TODO: implement asset record parsing and validation
  if(header->asset_table_offset % 4 != 0) {
    add_error(ctx, "asset_table_offset is misaligned");
    result = worst_error(result, BUN_MALFORMED);
  }

  if(header->string_table_offset % 4 != 0) {
    add_error(ctx, "string_table_offset is misaligned");
    result = worst_error(result, BUN_MALFORMED);
  }

  if(header->data_section_offset % 4 != 0) {
    add_error(ctx, "data_section_offset is misaligned");
    result = worst_error(result, BUN_MALFORMED);
  }

  if(header->string_table_size % 4 != 0) {
    add_error(ctx, "string_table_size is misaligned");
    result = worst_error(result, BUN_MALFORMED);
  }
  if(header->data_section_size % 4 != 0) {
    add_error(ctx, "data_section_size is misalgined");
    result = worst_error(result, BUN_MALFORMED);
  }

  u64 a_start = header->asset_table_offset;
  u64 a_size = a_start + header->asset_count * BUN_ASSET_RECORD_SIZE;
  u64 a_end = a_start + a_size;

  u64 s_start = header->string_table_offset;
  u64 s_end = s_start + header->string_table_size;

  u64 d_start = header->data_section_offset;
  u64 d_end = d_start + header->data_section_size;

  if(a_end > file_size) {
    add_error(ctx, "Asset entry table exceeds boundaries");
    result = worst_error(result, BUN_MALFORMED);
  }

  if(s_end > file_size) {
    add_error(ctx, "String table exceeds boundaries");
    result = worst_error(result, BUN_MALFORMED);
  }

  if(d_end > file_size) {
    add_error(ctx, "Data section exceeds boundaries");
    result = worst_error(result, BUN_MALFORMED);
  }

  if (a_end > s_start && a_start < s_end) {
    add_error(ctx, "Asset and string table overlap");
    result = worst_error(result, BUN_MALFORMED);
  }

  if (a_end > d_start && a_start < d_end) {
    add_error(ctx, "Asset and data section overlap");
    result = worst_error(result, BUN_MALFORMED);
  }

  if (s_end > d_start && s_start < d_end) {
    add_error(ctx, "String and data section overlap");
    result = worst_error(result, BUN_MALFORMED);
  }

  if(fseek(ctx->file, (long)a_start, SEEK_SET) != 0) {
    add_error(ctx, "Failed to seek asset table");
    result = worst_error(result, BUN_ERR_IO);
  }

  for (u32 i = 0; i < header->asset_count; i++) {
    u8 buf[BUN_ASSET_RECORD_SIZE];

    if (fread(buf, 1, BUN_ASSET_RECORD_SIZE, ctx->file) != BUN_ASSET_RECORD_SIZE) {
      add_error(ctx, "Unexpected EOF in asset record");
      return BUN_MALFORMED;
    }

    BunAssetRecord r;
    size_t o = 0;

    r.name_offset = read_u32_le(buf, o); o += 4;
    r.name_length = read_u32_le(buf, o); o += 4;
    r.data_offset = read_u64_le(buf, o); o += 8;
    r.data_size = read_u64_le(buf, o); o += 8;

    r.uncompressed_size = read_u64_le(buf, o); o += 8;
    r.compression = read_u32_le(buf, o); o += 4;
    r.type = read_u32_le(buf, o); o += 4;
    r.checksum = read_u32_le(buf, o); o += 4;
    r.flags = read_u32_le(buf, o);

    char name[256];
    if(r.name_length > sizeof(name)) {
      add_error(ctx, "Asset name too large for buffer");
      return BUN_MALFORMED;
    }
    if (r.name_length == 0) {
      add_error(ctx, "Invalid asset name length");
      return BUN_MALFORMED;
    }

    //u64 name_end_abs = name_start_abs + r.name_length;
    
    if ((u64)r.name_offset + (u64)r.name_length > header->string_table_size) {
      add_error(ctx, "Asset name out of string table bounds");
      return BUN_MALFORMED;
    }

    u64 name_start_abs = header->string_table_offset + r.name_offset;
    
    if(fseek(ctx->file, name_start_abs, SEEK_SET) != 0) {
      add_error(ctx, "Failed to seek to asset name");
      return BUN_MALFORMED;
    }

    if(fread(name, 1, r.name_length, ctx->file) != r.name_length) {
      add_error(ctx, "Failed to read asset name");
      return BUN_MALFORMED;
    }

    for(u32 j = 0; j < r.name_length; j++) {
      unsigned char c = name[j];
      if(c < 32 || c > 126) {
        add_error(ctx, "Non-printable asset name");
        return BUN_MALFORMED;
      }
    }

    if (r.data_offset + r.data_size > header->data_section_size) {
      add_error(ctx, "Asset data out of bounds");
      return BUN_MALFORMED;
    }

    // Compression checks
    if (r.compression == 1) {
        bun_result_t rle_res = validate_rle_data(ctx, header, &r);
        if (rle_res != BUN_OK) {
            return rle_res;
        }
    } else if (r.compression == 0 && r.uncompressed_size != 0) {
        add_error(ctx, "Can't have non-zero uncompressed size for an uncompressed asset");
        return BUN_MALFORMED;
    }

    if(r.checksum != 0) {
      add_error(ctx, "CRC-32 checksum validation is not supported");
      result = worst_error(result, BUN_UNSUPPORTED);
    }

    if(r.flags & ~(BUN_FLAG_ENCRYPTED | BUN_FLAG_EXECUTABLE)) {
      add_error(ctx, "Asset flags contains unknown bits");
      result = worst_error(result, BUN_UNSUPPORTED);
    }

    printf("Asset %u\n", i);
    printf("Type: %u\n", r.type);
    printf("Size: %llu\n", (unsigned long long)r.data_size);
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
