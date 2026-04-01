---
name: ksdec
description: Decrypt KiriKiri2 encrypted scenario files (.ks/.tjs) using the ksdec tool. Use this skill whenever you need to read, inspect, or analyze the contents of .ks or .tjs files that are FEFE-encrypted (mode 0/1/2), or when working with KAG scenario scripts from KiriKiri2/kirikiroid2 games. Also use when the user asks to decode, decrypt, or dump scenario files, or when you encounter binary .ks files that aren't plain text.
---

# KiriKiri2 Scenario Decryptor (ksdec)

## Tool Location

```
tools/bin/mac/rel/ksdec
```

## What It Does

Decrypts KiriKiri2 FEFE-encrypted scenario files and outputs UTF-8 text. Handles all encryption modes used by KiriKiri2's TextStream:

| Format | Header | Description |
|--------|--------|-------------|
| FEFE mode 0 | `FE FE 00` | XOR cipher on UTF-16LE |
| FEFE mode 1 | `FE FE 01` | Adjacent bit swap on UTF-16LE |
| FEFE mode 2 | `FE FE 02` | zlib-compressed UTF-16LE |
| Plain UTF-16LE | `FF FE` | BOM-marked, no encryption |
| Plain UTF-16BE | `FE FF` | BOM-marked, byte-swapped |
| Plain UTF-8 | any | Passed through as-is |

## Usage

```bash
# Decrypt to stdout
tools/bin/mac/rel/ksdec input.ks

# Decrypt to file
tools/bin/mac/rel/ksdec -o output.txt input.ks

# Batch decrypt multiple files
tools/bin/mac/rel/ksdec file1.ks file2.ks file3.tjs

# Decrypt all .ks in a directory
find /tmp/gamedata -name "*.ks" -exec tools/bin/mac/rel/ksdec {} \;
```

Diagnostic info (format detected, char count) goes to stderr. Decrypted text goes to stdout.

## Typical Workflow

1. Extract game archives with xp3 tool
2. Decrypt scenario files with ksdec
3. Analyze KAG script logic (tags, labels, macros)

```bash
# Full pipeline example
tools/bin/mac/rel/xp3 -o /tmp/gamedata game.xp3
tools/bin/mac/rel/ksdec /tmp/gamedata/data/sysscn/first.ks
```

## Building From Source

```bash
# Standalone (no cmake needed)
c++ -std=c++17 -O2 -lz -o tools/bin/mac/rel/ksdec tools/ksdec/main.cpp

# Via cmake (builds alongside other tools)
cmake --preset "MacOS Release Config" -DBUILD_TOOLS=ON
cmake --build out/macos/release --target ksdec
```

## Technical Details

The encryption/decryption logic mirrors `cpp/core/base/TextStream.cpp` (tTVPTextReadStream constructor, line 157-210). The tool is a standalone C++ binary with only zlib as a dependency — no TJS2 engine required.
