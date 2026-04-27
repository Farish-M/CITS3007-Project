#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "bun.h"

static int map_exit_code(bun_result_t r) {
  switch (r) {
    case BUN_OK: return 0;
    case BUN_MALFORMED: return 1;
    case BUN_UNSUPPORTED: return 2;
    default: return 3;
  }
}

static void print_errors(BunParseContext *ctx) {
  for (int i = 0; i < ctx->error_count; i++) {
    fprintf(stderr, "%s\n", ctx->errors[i]);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <file.bun>\n", argv[0]);
    return BUN_ERR_IO;
  }
  const char *path = argv[1];

  BunParseContext ctx = {0};
  BunHeader header  = {0};

  bun_result_t result = bun_open(path, &ctx);
  if (result != BUN_OK) {
    fprintf(stderr, "Error: could not open '%s'\n", path);
    return map_exit_code(result);
  }

  bun_result_t header_result = bun_parse_header(&ctx, &header);
  if (header_result != BUN_OK) {
    // bun_parse_header returns a code; printing the specifics is up to
    // you -- you may want to extend the API to return error details
    print_errors(&ctx);
    bun_close(&ctx);
    return map_exit_code(header_result);
  }

  bun_result_t asset_result = bun_parse_assets(&ctx, &header);
  if (asset_result != BUN_OK) {
    print_errors(&ctx);
    bun_close(&ctx);
    return map_exit_code(asset_result);
  }

  // TODO: on BUN_OK, print human-readable summary to stdout.
  //     on BUN_MALFORMED / BUN_UNSUPPORTED, print violation list to stderr.
  //     See project brief for output requirements.
if (header_result == BUN_OK && asset_result == BUN_OK) {
  printf("------------ BUN Header ------------\n");
  printf("Magic:                0x%08X (BUN0)\n", header.magic);
  printf("Version:              %u.%u\n", header.version_major, header.version_minor);
  printf("Asset Count:          %u\n", header.asset_count);
  printf("Asset Table Offset:   %" PRIu64 "\n", header.asset_table_offset);
  printf("String Table Offset:  %" PRIu64 "\n", header.string_table_offset);
  printf("String Table Size:    %" PRIu64 "\n", header.string_table_size);
  printf("Data Section Offset:  %" PRIu64 "\n", header.data_section_offset);
  printf("Data Section Size:    %" PRIu64 "\n", header.data_section_size);

  printf("\n");

  // Read and print one asset record at a time (constant memory, works for any file size)
  for (u32 i = 0; i < header.asset_count; i++) {
    u8 rec[BUN_ASSET_RECORD_SIZE];
    fseek(ctx.file, (long)(header.asset_table_offset + (u64)i * BUN_ASSET_RECORD_SIZE), SEEK_SET);
    fread(rec, 1, BUN_ASSET_RECORD_SIZE, ctx.file);

    BunAssetRecord a;
    size_t o = 0;
    a.name_offset       = (u32)rec[o]|(u32)rec[o+1]<<8|(u32)rec[o+2]<<16|(u32)rec[o+3]<<24; o+=4;
    a.name_length       = (u32)rec[o]|(u32)rec[o+1]<<8|(u32)rec[o+2]<<16|(u32)rec[o+3]<<24; o+=4;
    a.data_offset       = (u64)rec[o]|(u64)rec[o+1]<<8|(u64)rec[o+2]<<16|(u64)rec[o+3]<<24|(u64)rec[o+4]<<32|(u64)rec[o+5]<<40|(u64)rec[o+6]<<48|(u64)rec[o+7]<<56; o+=8;
    a.data_size         = (u64)rec[o]|(u64)rec[o+1]<<8|(u64)rec[o+2]<<16|(u64)rec[o+3]<<24|(u64)rec[o+4]<<32|(u64)rec[o+5]<<40|(u64)rec[o+6]<<48|(u64)rec[o+7]<<56; o+=8;
    a.uncompressed_size = (u64)rec[o]|(u64)rec[o+1]<<8|(u64)rec[o+2]<<16|(u64)rec[o+3]<<24|(u64)rec[o+4]<<32|(u64)rec[o+5]<<40|(u64)rec[o+6]<<48|(u64)rec[o+7]<<56; o+=8;
    a.compression       = (u32)rec[o]|(u32)rec[o+1]<<8|(u32)rec[o+2]<<16|(u32)rec[o+3]<<24; o+=4;
    a.type              = (u32)rec[o]|(u32)rec[o+1]<<8|(u32)rec[o+2]<<16|(u32)rec[o+3]<<24; o+=4;
    a.checksum          = (u32)rec[o]|(u32)rec[o+1]<<8|(u32)rec[o+2]<<16|(u32)rec[o+3]<<24; o+=4;
    a.flags             = (u32)rec[o]|(u32)rec[o+1]<<8|(u32)rec[o+2]<<16|(u32)rec[o+3]<<24;

    // read name from string table
    u32 name_len = a.name_length < 255 ? a.name_length : 255;
    char name[256] = {0};
    fseek(ctx.file, (long)(header.string_table_offset + a.name_offset), SEEK_SET);
    fread(name, 1, name_len, ctx.file);

    printf("------------ Asset %u ------------\n", i);
    printf("Name:               %s\n",          name);
    printf("Name Offset:        %u\n",           a.name_offset);
    printf("Name Length:        %u\n",           a.name_length);
    printf("Data Offset:        %" PRIu64 "\n", a.data_offset);
    printf("Data Size:          %" PRIu64 "\n", a.data_size);
    printf("Uncompressed Size:  %" PRIu64 "\n", a.uncompressed_size);
    printf("Compression:        %u\n",           a.compression);
    printf("Type:               %u\n",           a.type);
    printf("Checksum:           0x%08X\n",       a.checksum);
    printf("Flags:              0x%08X\n",       a.flags);
    printf("\n");
  }
}

  bun_close(&ctx);
  return map_exit_code(result);
}