#include <stdio.h>
#include <stdlib.h>

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
  printf("------------ BUN FILE ------------\n");
  printf("Magic: 0x%X\n", header.magic);
  printf("Version: %u.%u\n", header.version_major, header.version_minor);
  printf("Assets: %u\n", header.asset_count);
}

  bun_close(&ctx);
  return map_exit_code(result);
}