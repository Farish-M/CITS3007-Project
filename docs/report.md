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
When parsing a valid file the following output will be displayed

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

**Asset Table Display**
------------ Asset ------------
Name:                name
Type:                type
Size:                data size
Uncompressed Size:   uncompressed size
Compression:         compression
Checksum:            checksum
Flags:               flags

### Invalid Files

## 2. Decisions and Assumptions
*Describe any decisions or assumptions you made while implementing the parser.*
*For example: Do you address issues such as integer overflow or wraparound in offset and size arithmetic?*
*Is special handling necessary or desirable?*
*Are there any ambiguities in the BUN specification that required you to make a judgement call?*
*If so, what did you decide, and why?*

[Your answer here]

## 3. Libraries Used
*List any third-party libraries your executable depends on and briefly describe their purpose.*

[Your answer here]

## 4. Tools Used
*Describe what tools - such as static analysers or dynamic analysis tools like the Google Sanitizers (AddressSanitizer, UndefinedBehaviorSanitizer, etc.) or other practices you used to improve the safety and security of your code.*
*For each tool, you must provide concrete evidence of its use- for example, a link to GitHub issue and corresponding commit in your repository, showing code that was changed as a result of a tool's findings.*
*State what the tool's findings were and how it was run; a marker should be able to reproduce them by running the tool on an unfixed commit.*

[Your answer here]

## 5. Security Aspects
*Your management has proposed deploying your parser in the client for Trinity's new game, Brutal Orc Battles In Space a large-scale science fiction MMORPG.* [cite: 130] *In this deployment, the game client would automatically download, from Trinity's servers, BUN files that describe new galaxy regions and player-created content, and pass them to your parser.*
*Discuss:*
* *What security risks does this deployment scenario raise?*
* *Would you recommend any changes to the BUN format or the parser to address them?*
* *If so, describe the changes and explain how they would help.*

[Your answer here]

## 6. Coding Standards
*Describe any coding standards or conventions your group adopted (for example, naming conventions, code formatting, or rules around pointer arithmetic and memory management).*

[Your answer here]

## 7. Challenges
*Describe any challenges - technical or logistical - your group encountered during the project, and how you addressed them (or, if you were unable to, what the impact was).*

[Your answer here]

## 8. Academic Conduct and GenAI Usage
*You may use genAI tools to assist in coming up with ideas, or generating code - but all content must be reviewed by members of the group, and you must note (in your report, or your code) where you have used it.*

[Your answer here]