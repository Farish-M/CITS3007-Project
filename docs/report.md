# CITS3007 Phase 1 Report

**Group Number:** 40

**Group Members:**
* Name: David Pang, Student Number: 24128968, GitHub: pohhui247
* Name: Farish Mohamed Rozlan, Student Number: 23403278, GitHub: Farish-M
* Name: James Turner, Student Number: 23460542, GitHub: 23460542
* Name: Rania Khan, Student Number: 23770327, GitHub: raniak33
* Name: Ruslan Veselov, Student Number: 24185633, GitHub: rakkateichou

---

## 1. Output Format
*Document the human-readable output format produced by your parser for both valid and invalid files, and the exit codes used for non-BUN-OK outcomes.*
*Document the codes you use in your report.*

### Valid Files
When parsing a valid file there will be at least 1 output, which is the bun header as displayed below

**Bun Header**

------------ BUN Header ------------

Magic:                magic

Version:              version major.version minor

Asset Count:          asset count

Asset Table Offset:   asset table offset

String Table Offset:  string table offset

String Table Size:    string table size

Data Section Offset:  data section offset

Data Section Size:    data section size

In the bun header we display the magic number, version major and minor, the amount of assets and offsets in the bun file, and the offset and size of the string table and data section.

**Asset Table Display**

------------ Asset # ------------

Name:                name

Type:                type

Size:                data size

Uncompressed Size:   uncompressed size

Compression:         compression

Checksum:            checksum

Flags:               flags

For each asset table in the bun file we will display it's name, consisting of non-zero printable ASCII characters, type, size of asset in bytes, the size of the asset uncompressed, the checksum validation (typically CRC-32), and flags for the asset being encrypted or executable.

### Invalid Files
When parsing an invalid file, the parser will attempt to display as much of the file as it can safely and sensibly output. Followed by error messages displaying the cause of the error, i.e. "Asset and data section overlap"

As the parser runs through the BUN file it accumulates errors, once finished/terminated it will display each error that was encountered (1 per line).

Example:
Using "10-overlapping-with-nonprintable.bun", from the sample folder, will output the asset table and the error messages:

Asset and string table overlap
Non-printable asset name

### Exit Codes

The command-line parser returns `0` for valid files, `1` for malformed files, and `2` for unsupported BUN features. Additional parser-specific result codes are also defined: truncated files and sections that extend past EOF return exit code `4` (`BUN_ERR_TRUNCATED`). Some additional codes, such as `BUN_ERR_OVERFLOW`, `BUN_ERR_SECURITY`, and `BUN_ERR_CORRUPT`, are defined for more specific failure cases. 

## 2. Decisions and Assumptions
*Describe any decisions or assumptions you made while implementing the parser.*
*For example: Do you address issues such as integer overflow or wraparound in offset and size arithmetic?*
*Is special handling necessary or desirable?*
*Are there any ambiguities in the BUN specification that required you to make a judgement call?*
*If so, what did you decide, and why?*

### Decisions on Malformed RLE Data

In response to [issue #6](https://github.com/Farish-M/CITS3007-Project/issues/6), "RLE zero-count asset reports no error", we decided that malformed RLE data should cause parsing to fail. RLE payloads are made of count/value integer pairs and a zero-count for one or more pairs means that there are integer pairs which do not contribute to the decompressed output but are still included in the compressed input. This indicates malformed compressed data. 
The parser may still be able to read the asset record and display metadata such as the asset name, size, and compression type. However, once the RLE payload fails validation, the asset should not be treated as valid. Therefore we report the malformed RLE data and return an error instead of accepting the file.

### Decisions on Truncating Asset Name Data

In response to [issue #10](https://github.com/Farish-M/CITS3007-Project/issues/10), which in part concerned the behaviour of the BUN parser when encountering an asset with a name that is longer than the assigned display buffer (`name[256]`). In old versions of the parser, the parser would exit with code `1` if any asset name was too long for the buffer to avoid a buffer overflow. After a significant rewrite of the parser, names too long for the disiplay buffer would instead be truncated and the file was accepted as valid. To settle this issue, we decided that the truncating behaviour was correct as the BUN spec does not specify a limit to the size of an asset name as long as it fits within the string table (`(u64)name_offset + (u64)name_length <= string_table_size`). When the asset name must be truncated, an ellipsis is appended to the asset name in the output so as to inform the user that it has been truncated without returning an error or warning. 

### Additional Status Codes

These codes are an implementation decision rather than a requirement of the BUN specification. We decided to use additional parser result codes beyond the basic outcomes of `BUN_OK`, `BUN_MALFORMED`, `BUN_UNSUPPORTED`, and `BUN_ERR_IO` because more specific failures are useful when debugging parser behaviour.

We use `BUN_ERR_TRUNCATED` for files that end before a required structure can be fully read, such as a shortened header or a section that extends past EOF. This is treated separately because it can indicate an incomplete download or damaged file transfer rather than a logically invalid field value. This code appears as exit code `4`.

We use `BUN_ERR_OVERFLOW` when offset and size arithmetic cannot be performed safely. The BUN format uses 64-bit offsets and sizes, so calculations such as `asset_table_offset + asset_count * BUN_ASSET_RECORD_SIZE`, `string_table_offset + string_table_size`, and `data_section_offset + data_section_size` must be checked before use. If arithmetic wraps around, a section can appear to be inside the file even though the original values describe an impossible or malicious range. 

We use `BUN_ERR_SECURITY` for cases where a file may be representable but is unsafe or unreasonable to process, such as an excessive `asset_count` that could cause denial-of-service behaviour by forcing the parser to loop for too long. 

We use `BUN_ERR_CORRUPT` for cases where asset data is internally inconsistent after decoding, such as RLE data that expands to a different size from `uncompressed_size`. This separates corrupted payload data from broader structural problems in the file. 

## 3. Libraries Used
*List any third-party libraries your executable depends on and briefly describe their purpose.*

The main `bun_parser` executable does not depend on any third-party libraries. It is built from `main.c` and `bun_parse.c`, and only uses standard C library headers such as `stdio.h`, `stdlib.h`, `stdint.h`, `inttypes.h`, `string.h`, and `assert.h`.

### Other Programs in the Repository

The `tests/test_runner` executable depends on the third-party Check unit testing framework. This is included through `#include <check.h>` in `tests/test_bun.c`, and the Makefile links it using `pkg-config --cflags --libs check`. Check provides the test suite, test case, assertion, and test runner APIs used by the unit tests, such as `START_TEST`, `ck_assert_int_eq`, `suite_create`, `tcase_add_test`, and `srunner_run_all`. This dependency is only required for building and running the test executable, not by the main parser. 

The Python scripts do not use third-party Python packages. They only use Python standard library modules

## 4. Tools Used
### GCC Compiler Warnings (`-Wall -Wextra -Wpedantic`)
We utilized strict GCC compiler warnings as a form of static analysis to catch potential bugs and code quality issues during compilation. 
*   **Evidence:** The compiler flagged a warning: `bun_parse.c:254:7: warning: unused variable 'counttest' [-Wunused-variable]`. We removed this unused variable to keep the code clean. The commit hash before the fix: `e1aca9e`; the commit hash after the fix: `587e661`.

### Clang-Format
We used `clang-format` to enforce consistent code styling across the codebase. This prevented syntax formatting issues and merge conflicts during development.
*   **Evidence:** We added a `format` target to our `Makefile` and formatted the codebase in commit `698f488` (`chore: format code, add format to Makefile`). This can be reproduced by running `make format` on any unfixed commit.

### Libcheck (Unit Testing)
We used `libcheck` as the unit testing framework to verify our parser against both valid and invalid BUN sample files.
*   **Evidence:** `feature/tests` branch has a commit (`331833a`) that adds unit tests executed via running `make test`. It verifies that the parser is handling cases such as RLE zero-count payloads and section overlaps correctly.

## 5. Security Aspects
*Your management has proposed deploying your parser in the client for Trinity's new game, Brutal Orc Battles In Space a large-scale science fiction MMORPG.* [cite: 130] *In this deployment, the game client would automatically download, from Trinity's servers, BUN files that describe new galaxy regions and player-created content, and pass them to your parser.*
*Discuss:*
* *What security risks does this deployment scenario raise?*
* *Would you recommend any changes to the BUN format or the parser to address them?*
* *If so, describe the changes and explain how they would help.*

### Security Risks
**Malicious player-created content**
Allowing content to be created by players is the most serious risk, as they are attacker-controlled. An attacker can craft a BUN file with precisely chosen field values to probe for parser bugs, test overflow conditions, or attempt to exploit the client. Highlighting the risk of malicious players to attack other player's devices.

**Compromised server**
Trinity's content is a potential risk if the game's server is comprimised, due to to client automatically downloading and parsing BUN files. Without verification of the origin of parsed BUN files, any server breach means an attacker can push malicious files to ever player all at once.

**No cryptographic authentication**
The BUN format does not have a signature or MAC field to prove it's authenticity, and proof it came from Trinity's servers. There is no way to detect tampering.

**Denial of service via large asset_count**
A lack of protection from a denial of service attack if `asset_count` is an excessive amount. Since the parser processes one record at a time, it won't crash due to memory. However, it will praser for a very long time, hanging the client.

**Name-based path traversal**
Asset names are validated as printable ASCII characters, but file paths such as ../../../etc/passwd are printable. If the game uses asset names to construct the file paths, this is a directory traversal vulnerability.

**Unverified encryption flag**
The parser doesn't enforce whether or not the content is encrypted, so a file can falsely claim its content as encrypted or not. This can confuse the game into processing unwanted raw data.

### Recommended Changes and How They Help
**Generative AI was used to recommend changes**
**Adding a cryptographic signature to the BUN format**
Adding a signature field to the header covering the entire file content, would allow the parser to verify the file against a hardcoded public key before processing it. This solution fixes two risks, the compromised server and tampered file risks; if a file doesn't have a valid signature its rejected immediately. So when players create content it would require a submission to Trinity's servers for moderation and signing, preventing players from developing malicious content and pushing it to other clients.

**Capping `asset_count`**
Limiting the amount of assets prevents risk from a denial-of-service loops. This can be done from adding conditionals to check the amount of assets counted, comparing it to a value and terminating immediately with an error when encountered.

**Rejecting dangerous asset names in the parser**
Detecting if any assets has names, with characters like '..', '/', '\', or null bytes, in the parser and displaying the error. Checking for directory traversal risk at the parser level, rather than the game sanitising the names.

**Run the parser in a sandbox**
When deployed, the parser should be ran in a separate process with restricted OS permissions such as, no network access, limiting filesystem access to the downloads directory, prevention to spawn child processes. This isn't directly a parser change, but rather where to use the parser, which can mitigate and limit the damage from an attacker.

## 6. Coding Standards

### C Source Code Formatting

For C source formatting, the project adopted `clang-format`. This was added in the formatting commit [`698f488`](https://github.com/Farish-M/CITS3007-Project/commit/698f4884c1330540a883b1991344e1d03b9ea15b) and connected to the build process through the `format` target in the `Makefile`. The project is compiled with `gcc` using `-std=c11 -Wall -Wextra -Wpedantic`, so code is expected to remain C11-compatible and free of common compiler warnings. The formatter uses an LLVM-based style with an 80-column limit, attached braces, and consistent indentation.

The parser code also follows several local conventions. Fixed-width integer aliases such as `u8`, `u16`, `u32`, and `u64` are used for on-disk BUN fields so that file layout remains explicit. Parser functions return `bun_result_t` status codes rather than exiting directly, while `main.c` is responsible for command-line output.

### Git Usage

The project used a feature-branch workflow, as described in `CONTRIBUTING.md`. New work was generally developed on branches named by purpose, such as `feature/asset-parser`, `feature/rle-validation`, `display-multiple-errors`, `feature/spec-features-fixes`, `feature/tests`, and `feature/additonal-status-codes`, before being merged into `main` through pull requests. The commit history also shows the use of conventional commit prefixes such as `feat:`, `bugfix:`, `test:`, `docs:`, and `chore:`. This made it easier to distinguish parser behaviour changes from test, documentation, and maintenance work.

## 7. Challenges
*Describe any challenges - technical or logistical - your group encountered during the project, and how you addressed them (or, if you were unable to, what the impact was).*

[Your answer here]

## 8. Academic Conduct and GenAI Usage
*You may use genAI tools to assist in coming up with ideas, or generating code - but all content must be reviewed by members of the group, and you must note (in your report, or your code) where you have used it.*

OpenAI ChatGPT 5.5 was utilised via Codex in the creation of `bunfile_fixture_generator.py`. Initially there was an attempt to nest parts of `bunfile_generator.py` into reusable functions that could be modified to generate malformed data but it relied on updated libraries not available for the version of Python (3.8) that is present in the CITS3007 SDE.

Claude Sonnet 4.6 was used in the recommendations for changes to security risks faced in Trinity's deployment. Claude was also used in the debugging various bugs in the parser, such as with identifying an issues with the offset and creating stderr messages for me to identify the outlying issue, and identiying the lack of an overflow check in the parser.
