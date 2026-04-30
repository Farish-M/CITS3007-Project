#!/usr/bin/env python3

"""
BUN file generator.

Provides functions for writing BUN headers and asset records to disk.
When this script is called, runs an interactive dialogue to generate 
valid or deliberately malformed BUN files for testing. 

Compatible with Python 3.8 as per the CITS3007 SDE. 

Script created with the help of OpenAI gpt-5.5 with codex.
"""

import struct
import sys
import random
from pathlib import Path

# Header fields for output
HEADER_FIELDS = [
    ("magic",               "hex"),
    ("version_major",       "dec"),
    ("version_minor",       "dec"),
    ("asset_count",         "dec"),
    ("asset_table_offset",  "dec"),
    ("string_table_offset", "dec"),
    ("string_table_size",   "dec"),
    ("data_section_offset", "dec"),
    ("data_section_size",   "dec"),
    ("reserved",            "dec"),
]

# Record fields for output
RECORD_FIELDS = [
    ("name_offset",       "dec"),
    ("name_length",       "dec"),
    ("data_offset",       "dec"),
    ("data_size",         "dec"),
    ("uncompressed_size", "dec"),
    ("compression",       "dec"),
    ("asset_type",        "dec"),
    ("checksum",          "dec"),
    ("flags",             "dec"),
]

# On-disk format strings (little-endian)
# See bun-spec.pdf sections 4 and 5.
# These are used by the Python *struct* library (https://docs.python.org/3/library/struct.html) -
# see <https://docs.python.org/3/library/struct.html#format-strings> for an explanation.

# Header fields in order:
#   magic, version_major, version_minor, asset_count,
#   asset_table_offset, string_table_offset, string_table_size,
#   data_section_offset, data_section_size, reserved
_HEADER_FMT = "<IHHIQQQQQQ"

# Asset record fields in order:
#   name_offset, name_length, data_offset, data_size,
#   uncompressed_size, compression, asset_type, checksum, flags
_RECORD_FMT = "<IIQQQIIII"

BUN_MAGIC         = 0x304E5542   # "BUN0" in little-endian
BUN_VERSION_MAJOR = 1
BUN_VERSION_MINOR = 0

COMPRESS_NONE = 0
COMPRESS_RLE  = 1
COMPRESS_ZLIB = 2

FLAG_ENCRYPTED  = 0x1
FLAG_EXECUTABLE = 0x2

ASSET_TYPE_TEXT    = 1
ASSET_TYPE_TEXTURE = 2
ASSET_TYPE_AUDIO   = 3
ASSET_TYPE_SCRIPT  = 4

def display_struct(header_dict: dict, field_names) -> str:
    "display a dict containing struct values"
    output = []

    # compute column widths based on max field length
    longest_field_len = max(list(len(pair[0]) for pair in field_names))

    for name, fmt in field_names:
        value = header_dict[name]
        if fmt == "hex":
            rendered = f"0x{value:08x}"
        else:
            rendered = str(value)
        output.append(f"{name:<{longest_field_len}} = {rendered}")

    return "\n".join(output)

def write_header(
    f,
    *,
    asset_count: int,
    asset_table_offset: int,
    string_table_offset: int,
    string_table_size: int,
    data_section_offset: int,
    data_section_size: int,
    magic: int = BUN_MAGIC,
    version_major: int = BUN_VERSION_MAJOR,
    version_minor: int = BUN_VERSION_MINOR,
    reserved: int = 0,
) -> None:
    """
    Write a BUN header to file object f at its current position.

    All offset and size arguments are in bytes from the start of the file.
    The magic, version, and reserved fields have sensible defaults and
    normally need not be specified -- but can be overridden to produce
    deliberately malformed files for testing.
    """
    header_args = dict(locals())
    print("writing header to disk:")
    print("\n" + display_struct(header_args, HEADER_FIELDS) + "\n")

    data = struct.pack(
        _HEADER_FMT,
        magic,
        version_major,
        version_minor,
        asset_count,
        asset_table_offset,
        string_table_offset,
        string_table_size,
        data_section_offset,
        data_section_size,
        reserved,
    )
    print("len of header data:", len(data))
    f.write(data)


def write_asset_record(
    f,
    *,
    name_offset: int,
    name_length: int,
    data_offset: int,
    data_size: int,
    uncompressed_size: int = 0,
    compression: int       = COMPRESS_NONE,
    asset_type: int        = 0,
    checksum: int          = 0,
    flags: int             = 0,
) -> None:
    """
    Write a single BUN asset record to file object f at its current position.

    name_offset and name_length describe the asset name within the string
    table. data_offset and data_size describe the asset payload within the
    data section. Both offsets are relative to the start of their respective
    sections (not the start of the file).

    uncompressed_size must be 0 if the data is not compressed (compression=0).
    If the data is compressed, uncompressed_size must be the expected size
    after decompression.

    asset_type is a user-defined value (e.g. 1=texture, 2=audio).
    checksum, if non-zero, is a CRC-32 of the uncompressed data.
    """
    record_args = dict(locals())
    print("\nwriting asset record to disk:")
    print("\n" + display_struct(record_args, RECORD_FIELDS) + "\n")

    data = struct.pack(
        _RECORD_FMT,
        name_offset,
        name_length,
        data_offset,
        data_size,
        uncompressed_size,
        compression,
        asset_type,
        checksum,
        flags,
    )
    print("len of record data:", len(data), "\n")
    f.write(data)


def _align4(n: int) -> int:
    """Round n up to the next multiple of 4."""
    return (n + 3) & ~3

def write_padding(f, n: int, description: str) -> None:
    """write n many NULL bytes"""
    assert n >= 0

    print(f"writing {n} null bytes {description}\n")
    f.write(b"\x00" * n)

class BunAsset:
    def __init__(
        self,
        name,
        payload,
        uncompressed_size=0,
        compression=COMPRESS_NONE,
        asset_type=0,
        checksum=0,
        flags=0,
    ):
        self.name = name
        self.payload = payload
        self.uncompressed_size = uncompressed_size
        self.compression = compression
        self.asset_type = asset_type
        self.checksum = checksum
        self.flags = flags

def _with_overrides(values, overrides):
    if overrides:
        values.update(overrides)
    return values

def write_bun_file(
    out_path,
    assets,
    *,
    header_overrides=None,
    record_overrides=None,
    truncate_to=None,
):
    """
    Write a BUN file containing zero or more assets.

    header_overrides and record_overrides are intentionally small escape
    hatches for creating malformed fixtures without duplicating the layout
    code used for valid files.
    """
    out_path.parent.mkdir(parents=True, exist_ok=True)
    record_overrides = record_overrides or {}

    names = b"".join(asset.name for asset in assets)
    payloads = b"".join(asset.payload for asset in assets)
    asset_count = len(assets)

    asset_table_offset = _align4(HEADER_SIZE)
    string_table_offset = _align4(asset_table_offset + asset_count * RECORD_SIZE)
    string_table_size = _align4(len(names))
    data_section_offset = _align4(string_table_offset + string_table_size)
    data_section_size = _align4(len(payloads))

    header = _with_overrides(
        {
            "asset_count": asset_count,
            "asset_table_offset": asset_table_offset,
            "string_table_offset": string_table_offset,
            "string_table_size": string_table_size,
            "data_section_offset": data_section_offset,
            "data_section_size": data_section_size,
        },
        header_overrides,
    )

    name_offset = 0
    data_offset = 0
    records = []
    for i, asset in enumerate(assets):
        record = _with_overrides(
            {
                "name_offset": name_offset,
                "name_length": len(asset.name),
                "data_offset": data_offset,
                "data_size": len(asset.payload),
                "uncompressed_size": asset.uncompressed_size,
                "compression": asset.compression,
                "asset_type": asset.asset_type,
                "checksum": asset.checksum,
                "flags": asset.flags,
            },
            record_overrides.get(i),
        )
        records.append(record)
        name_offset += len(asset.name)
        data_offset += len(asset.payload)

    with open(out_path, "wb") as f:
        write_header(f, **header)

        if header["asset_table_offset"] > HEADER_SIZE:
            write_padding(f, header["asset_table_offset"] - HEADER_SIZE, "header padding")

        for record in records:
            write_asset_record(f, **record)

        if header["string_table_offset"] > f.tell():
            write_padding(f, header["string_table_offset"] - f.tell(), "records padding")
        f.write(names)

        if header["string_table_offset"] + header["string_table_size"] > f.tell():
            write_padding(
                f,
                header["string_table_offset"] + header["string_table_size"] - f.tell(),
                "string table padding",
            )

        if header["data_section_offset"] > f.tell():
            write_padding(f, header["data_section_offset"] - f.tell(), "data section padding")
        f.write(payloads)

        if header["data_section_offset"] + header["data_section_size"] > f.tell():
            write_padding(
                f,
                header["data_section_offset"] + header["data_section_size"] - f.tell(),
                "payload padding",
            )

    if truncate_to is not None:
        with open(out_path, "r+b") as f:
            f.truncate(truncate_to)

    print(f"Wrote {out_path} ({out_path.stat().st_size} bytes)")

def patch_u32(path, offset, value):
    with open(path, "r+b") as f:
        f.seek(offset)
        f.write(struct.pack("<I", value))

def patch_u64(path, offset, value):
    with open(path, "r+b") as f:
        f.seek(offset)
        f.write(struct.pack("<Q", value))

def patch_header_u32(path, field, value):
    offsets = {
        "magic": 0,
        "asset_count": 8,
    }
    patch_u32(path, offsets[field], value)

def patch_header_u64(path, field, value):
    offsets = {
        "asset_table_offset": 12,
        "string_table_offset": 20,
        "string_table_size": 28,
        "data_section_offset": 36,
        "data_section_size": 44,
    }
    patch_u64(path, offsets[field], value)

def simple_asset(name=b"asset", payload=b"data"):
    return BunAsset(name, payload)

def fixture_path(root, category, filename):
    directory = root / category
    directory.mkdir(parents=True, exist_ok=True)
    return directory / filename

def generate_test_fixtures(root: Path = Path("tests/fixtures")) -> None:
    """Generate valid and deliberately malformed BUN fixtures."""
    write_bun_file(fixture_path(root, "valid", "01-empty.bun"), [])

    write_bun_file(
        fixture_path(root, "valid", "02-many-assets.bun"),
        [
            BunAsset(b"readme.txt", b"Line one\nLine two\n", asset_type=ASSET_TYPE_TEXT),
            BunAsset(
                b"sprites/player.rgba",
                bytes(range(32)),
                asset_type=ASSET_TYPE_TEXTURE,
            ),
            BunAsset(
                b"audio/jingle.rle",
                b"\x04A\x03B\x02C",
                uncompressed_size=9,
                compression=COMPRESS_RLE,
                asset_type=ASSET_TYPE_AUDIO,
            ),
            BunAsset(
                b"scripts/start.sh",
                b"#!/bin/sh\necho BUN\n",
                asset_type=ASSET_TYPE_SCRIPT,
                flags=FLAG_EXECUTABLE,
            ),
            BunAsset(b"empty.bin", b"", asset_type=0),
        ],
    )

    large_assets = []
    for i in range(1024):
        name = f"bulk/asset-{i:03d}.bin".encode("ascii")
        payload = bytes([(i + j) % 256 for j in range(4096)])
        large_assets.append(BunAsset(name, payload, asset_type=ASSET_TYPE_TEXTURE))
    write_bun_file(fixture_path(root, "valid", "03-large.bun"), large_assets)

    case_name_too_large(fixture_path(root, "valid", "21-name-too-large.bun"))

    write_bun_file(
        fixture_path(root, "invalid", "01-bad-magic.bun"),
        [],
        header_overrides={"magic": 0},
    )
    write_bun_file(
        fixture_path(root, "invalid", "02-bad-version.bun"),
        [],
        header_overrides={"version_major": BUN_VERSION_MAJOR + 1},
    )
    write_bun_file(
        fixture_path(root, "invalid", "03-misaligned-data-offset.bun"),
        [BunAsset(b"hello", b"world")],
        header_overrides={"data_section_offset": 157},
    )
    write_bun_file(
        fixture_path(root, "invalid", "04-name-out-of-bounds.bun"),
        [BunAsset(b"hello", b"world")],
        record_overrides={0: {"name_offset": 100, "name_length": 5}},
    )
    write_bun_file(
        fixture_path(root, "invalid", "05-data-out-of-bounds.bun"),
        [BunAsset(b"hello", b"world")],
        record_overrides={0: {"data_offset": 100, "data_size": 5}},
    )
    write_bun_file(
        fixture_path(root, "invalid", "06-rle-zero-count.bun"),
        [BunAsset(b"bad.rle", b"\x00A", uncompressed_size=1, compression=COMPRESS_RLE)],
    )
    write_bun_file(
        fixture_path(root, "invalid", "07-truncated-header.bun"),
        [BunAsset(b"hello", b"world")],
        truncate_to=20,
    )

def case_bad_magic(path):
    write_bun_file(path, [], header_overrides={"magic": 0})

def case_bad_version(path):
    write_bun_file(path, [], header_overrides={"version_major": BUN_VERSION_MAJOR + 1})

def case_truncated_header(path):
    write_bun_file(path, [simple_asset()], truncate_to=20)

def case_misaligned_section(path):
    write_bun_file(path, [simple_asset()], header_overrides={"data_section_offset": 117})

def case_asset_table_offset_past_eof(path):
    write_bun_file(path, [])
    patch_header_u64(path, "asset_table_offset", 1000)

def case_string_table_offset_past_eof(path):
    write_bun_file(path, [])
    patch_header_u64(path, "string_table_offset", 1000)

def case_data_section_offset_past_eof(path):
    write_bun_file(path, [])
    patch_header_u64(path, "data_section_offset", 1000)

def case_asset_table_exceeds_eof(path):
    write_bun_file(path, [simple_asset()], truncate_to=80)

def case_string_table_exceeds_eof(path):
    write_bun_file(path, [])
    patch_header_u64(path, "string_table_size", 4)

def case_data_section_exceeds_eof(path):
    write_bun_file(path, [])
    patch_header_u64(path, "data_section_size", 4)

def case_asset_string_overlap(path):
    write_bun_file(path, [simple_asset(b"ABCDEFGH", b"data")])
    patch_header_u64(path, "string_table_offset", 100)

def case_asset_data_overlap(path):
    write_bun_file(path, [simple_asset(b"ABCDEFGH", b"data")])
    patch_header_u64(path, "string_table_offset", 100)
    patch_header_u64(path, "data_section_offset", 96)

def case_string_data_overlap(path):
    write_bun_file(path, [simple_asset(b"ABCDEFGHIJKL", b"data")])
    patch_header_u64(path, "string_table_offset", 104)
    patch_header_u64(path, "string_table_size", 12)
    patch_header_u64(path, "data_section_offset", 112)

def case_truncated_asset_record(path):
    write_bun_file(path, [simple_asset()], truncate_to=80)

def case_empty_name(path):
    write_bun_file(path, [simple_asset()], record_overrides={0: {"name_length": 0}})

def case_name_out_of_bounds(path):
    write_bun_file(
        path,
        [simple_asset()],
        record_overrides={0: {"name_offset": 100, "name_length": 5}},
    )

def case_name_too_large(path):
    write_bun_file(path, [simple_asset(b"a" * 260, b"data")])

def case_non_printable_name(path):
    write_bun_file(path, [simple_asset(b"bad\x01name", b"data")])

def case_data_out_of_bounds(path):
    write_bun_file(
        path,
        [simple_asset()],
        record_overrides={0: {"data_offset": 100, "data_size": 4}},
    )

def case_rle_odd_size(path):
    write_bun_file(
        path,
        [BunAsset(b"odd.rle", b"\x01A\x02", uncompressed_size=3, compression=COMPRESS_RLE)],
    )

def case_rle_zero_count(path):
    write_bun_file(
        path,
        [BunAsset(b"zero.rle", b"\x00A", uncompressed_size=1, compression=COMPRESS_RLE)],
    )

def case_rle_expanded_too_large(path):
    write_bun_file(
        path,
        [BunAsset(b"large.rle", b"\x05A", uncompressed_size=4, compression=COMPRESS_RLE)],
    )

def case_rle_expanded_size_mismatch(path):
    write_bun_file(
        path,
        [BunAsset(b"mismatch.rle", b"\x02A", uncompressed_size=3, compression=COMPRESS_RLE)],
    )

def case_uncompressed_size_on_uncompressed_asset(path):
    write_bun_file(path, [BunAsset(b"plain", b"data", uncompressed_size=4)])

def case_unsupported_checksum(path):
    write_bun_file(path, [BunAsset(b"checksum", b"data", checksum=0x12345678)])

def case_unknown_flags(path):
    write_bun_file(path, [BunAsset(b"flags", b"data", flags=0x4)])

MALFORMED_CASES = [
    ("08-misaligned-section.bun", case_misaligned_section),
    ("09-asset-table-offset-past-eof.bun", case_asset_table_offset_past_eof),
    ("10-string-table-offset-past-eof.bun", case_string_table_offset_past_eof),
    ("11-data-section-offset-past-eof.bun", case_data_section_offset_past_eof),
    ("12-asset-table-exceeds-eof.bun", case_asset_table_exceeds_eof),
    ("13-string-table-exceeds-eof.bun", case_string_table_exceeds_eof),
    ("14-data-section-exceeds-eof.bun", case_data_section_exceeds_eof),
    ("15-asset-string-overlap.bun", case_asset_string_overlap),
    ("16-asset-data-overlap.bun", case_asset_data_overlap),
    ("17-string-data-overlap.bun", case_string_data_overlap),
    ("18-truncated-asset-record.bun", case_truncated_asset_record),
    ("19-empty-name.bun", case_empty_name),
    ("20-name-out-of-bounds.bun", case_name_out_of_bounds),
    ("22-non-printable-name.bun", case_non_printable_name),
    ("23-data-out-of-bounds.bun", case_data_out_of_bounds),
    ("24-rle-odd-size.bun", case_rle_odd_size),
    ("25-rle-zero-count.bun", case_rle_zero_count),
    ("26-rle-expanded-too-large.bun", case_rle_expanded_too_large),
    ("27-rle-expanded-size-mismatch.bun", case_rle_expanded_size_mismatch),
    (
        "28-uncompressed-size-on-uncompressed-asset.bun",
        case_uncompressed_size_on_uncompressed_asset,
    ),
    ("29-checksum.bun", case_unsupported_checksum),
    ("30-unknown-flags.bun", case_unknown_flags),
]

def generate_all_malformed(root: Path = Path("tests/fixtures")) -> None:
    """Generate one targeted fixture for each parser-detected malformed case."""
    generate_test_fixtures(root)
    for filename, generate in MALFORMED_CASES:
        generate(fixture_path(root, "invalid", filename))

def ask_choice(prompt, options):
    while True:
        print("\n" + prompt)
        for key, text in options:
            print(f"{key}. {text}")
        choice = input("> ").strip()
        if choice in [key for key, _ in options]:
            return choice
        print("Choose one of the listed numbers.")

def ask_int(prompt, default=0):
    raw = input(f"{prompt} [{default}]: ").strip()
    if not raw:
        return default
    return int(raw, 0)

def rand_ascii_name():
    letters = b"abcdefghijklmnopqrstuvwxyz"
    return b"asset-" + bytes(random.choice(letters) for _ in range(6)) + b".bin"

def rand_bytes(n):
    return bytes(random.randrange(0, 256) for _ in range(n))

def apply_custom_choice(part, mode, cfg):
    cfg["selected"].add(part)

    if part == "magic":
        if mode == "1":
            cfg["header"]["magic"] = BUN_MAGIC
        elif mode == "2":
            cfg["header"]["magic"] = 0
        else:
            cfg["header"]["magic"] = ask_int("Magic value, e.g. 0x304E5542", BUN_MAGIC)

    elif part == "version":
        if mode == "1":
            cfg["header"]["version_major"] = BUN_VERSION_MAJOR
            cfg["header"]["version_minor"] = BUN_VERSION_MINOR
        elif mode == "2":
            cfg["header"]["version_major"] = BUN_VERSION_MAJOR + 1
            cfg["header"]["version_minor"] = 0
        else:
            cfg["header"]["version_major"] = ask_int("Version major", BUN_VERSION_MAJOR)
            cfg["header"]["version_minor"] = ask_int("Version minor", BUN_VERSION_MINOR)

    elif part == "data":
        cfg["record"].pop("data_offset", None)
        cfg["record"].pop("data_size", None)
        cfg["asset"].compression = COMPRESS_NONE
        cfg["asset"].uncompressed_size = 0
        if mode == "1":
            cfg["asset"].payload = rand_bytes(16)
        elif mode == "2":
            cfg["asset"].payload = b"short"
            cfg["record"]["data_offset"] = 100
            cfg["record"]["data_size"] = 5
        else:
            cfg["asset"].payload = input("Payload text: ").encode("utf-8")

    elif part == "name":
        cfg["record"].pop("name_offset", None)
        cfg["record"].pop("name_length", None)
        if mode == "1":
            cfg["asset"].name = rand_ascii_name()
        elif mode == "2":
            cfg["asset"].name = b"bad"
            cfg["record"]["name_offset"] = 100
            cfg["record"]["name_length"] = 3
        else:
            cfg["asset"].name = input("Asset name: ").encode("utf-8")

    elif part == "rle count":
        cfg["record"].pop("data_offset", None)
        cfg["record"].pop("data_size", None)
        cfg["asset"].compression = COMPRESS_RLE
        if mode == "1":
            count = random.randrange(1, 10)
            value = random.randrange(65, 91)
            cfg["asset"].payload = bytes([count, value])
            cfg["asset"].uncompressed_size = count
        elif mode == "2":
            cfg["asset"].payload = b"\x00A"
            cfg["asset"].uncompressed_size = 1
        else:
            count = ask_int("RLE count byte", 1)
            value = ask_int("RLE value byte", 65)
            cfg["asset"].payload = bytes([count & 0xff, value & 0xff])
            cfg["asset"].uncompressed_size = count

    elif part == "header":
        cfg["header"].pop("asset_count", None)
        cfg["header"].pop("data_section_offset", None)
        cfg["truncate_to"] = None
        if mode == "1":
            pass
        elif mode == "2":
            bad = ask_choice(
                "Malformed header",
                [("1", "Misaligned data section offset"), ("2", "Truncated header")],
            )
            if bad == "1":
                cfg["header"]["data_section_offset"] = 157
            else:
                cfg["truncate_to"] = 20
        else:
            cfg["header"]["asset_count"] = ask_int("Header asset_count", 1)

def generate_custom_dialogue():
    cfg = {
        "asset": BunAsset(b"asset.bin", b"Hello, BUN!\n"),
        "header": {},
        "record": {},
        "truncate_to": None,
        "selected": set(),
    }
    parts = ["magic", "version", "data", "name", "rle count", "header"]

    while True:
        print("\nCustom BUN file")
        for i, part in enumerate(parts, 1):
            mark = "x" if part in cfg["selected"] else " "
            print(f"{i}. [{mark}] {part}")
        print("7. Generate custom.bun")
        print("8. Back")
        choice = input("> ").strip()

        if choice in ["1", "2", "3", "4", "5", "6"]:
            part = parts[int(choice) - 1]
            mode = ask_choice(
                part,
                [("1", "Random valid data"), ("2", "Malformed data"), ("3", "Manual input")],
            )
            apply_custom_choice(part, mode, cfg)
        elif choice == "7":
            write_bun_file(
                Path("custom.bun"),
                [cfg["asset"]],
                header_overrides=cfg["header"],
                record_overrides={0: cfg["record"]},
                truncate_to=cfg["truncate_to"],
            )
            return
        elif choice == "8":
            return
        else:
            print("Choose one of the listed numbers.")

def dialogue():
    while True:
        choice = ask_choice(
            "BUN fixture generator",
            [
                ("1", "Generate custom bunfile"),
                ("2", "Generate example suite"),
                ("3", "Generate full malformed suite"),
                ("4", "Quit"),
            ],
        )
        if choice == "1":
            generate_custom_dialogue()
        elif choice == "2":
            generate_test_fixtures()
        elif choice == "3":
            generate_all_malformed()
        else:
            return 0

# On-disk size -- useful for computing offsets.
HEADER_SIZE = struct.calcsize(_HEADER_FMT)
RECORD_SIZE = struct.calcsize(_RECORD_FMT)

assert HEADER_SIZE == 60, f"Unexpected record size: {HEADER_SIZE}"
assert RECORD_SIZE == 48, f"Unexpected record size: {RECORD_SIZE}"

def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--fixtures":
        generate_test_fixtures()
        return 0
    if len(sys.argv) == 2 and sys.argv[1] == "--all-malformed":
        generate_all_malformed()
        return 0
    if len(sys.argv) == 2 and sys.argv[1] == "--minimal":
        write_bun_file(Path("minimal.bun"), [BunAsset(b"hello", b"Hello, BUN world!\n")])
        return 0
    if len(sys.argv) != 1:
        print(
            "Usage: python3 bunfile_fixture_generator.py [--fixtures|--all-malformed|--minimal]",
            file=sys.stderr,
        )
        return 2
    return dialogue()


if __name__ == "__main__":
    sys.exit(main())
