# PSB RL Decompression — libkrkr2.so Reverse Engineering Analysis

## Source Function

**Address**: `sub_695DE8` (libkrkr2.so)
**Purpose**: Decode PSB image resources into BGRA pixel buffers for texture creation.
**Decompiled output**: 53KB, ~1500 lines — this is a large monolithic function handling the entire PSB resource decode pipeline.

## Overview

The function processes each image resource entry in a PSB file. For each entry, it:

1. Reads `width` and `height` from the entry struct (offsets +40, +44)
2. Allocates a pixel buffer: `4 * width * height` bytes, 4-byte aligned (`sub_A0DE48`)
3. Checks for `"pal"` (palette) key in the entry dict (`sub_5995D8`)
4. Checks for `"compress"` key and whether its value is `"RL"`
5. Dispatches to one of 3 decode paths based on these flags
6. After decoding, checks if all alpha bytes are zero (fully transparent) — if so, frees the pixel buffer

## Decision Tree

```
has_pal = entry.has("pal")
has_compress_rl = entry.has("compress") && entry["compress"] == "RL"

if (has_pal):
    if (has_compress_rl):
        → Path A: RL decompress with align=1, then palette expand
    else:
        → Path B: Raw memcpy of indexed pixels, then palette expand
else:  // no palette
    if (!has_compress_rl):
        → Path C: Raw pixel copy with TVPReverseRGB
    else:
        → Path D: RL decompress with align=4, then TVPReverseRGB
```

## Path A: RL + Palette (align=1)

**Address range**: `0x696E40–0x696E9C` (RL loop), `0x6971D0–0x697204` (palette expand)

### RL Decompression (align=1)

Decompresses into a temporary 1-byte-per-pixel index buffer of size `width * height`.

```c
// v184 = compressed data pointer (from PSB resource "pixel")
// v187 = compressed data end pointer (v184 + compressed_size)
// v211 = output buffer (1 byte per pixel)
// v188 = output write pointer

do {
    marker = *v184;
    v184++;
    
    if (marker & 0x80) {
        // RLE run: repeat next byte (marker & 0x7F) + 3 times
        count = (marker & 0x7F) + 3;           // 0x696E7C
        memset(v188, v184[0], count);           // 0x696E88  (1 byte value)
        v188 += count;                          // 0x696E90
        v184++;                                 // 0x696E94  (skip 1 value byte)
    } else {
        // Literal: copy (marker + 1) bytes verbatim
        count = marker + 1;                     // 0x696E58
        memcpy(v188, v184, count);              // 0x696E68
        v188 += count;                          // 0x696E6C
        v184 += count;                          // 0x696E70
    }
} while (v184 < v187);
```

### Palette Expansion

After RL decompression:

```c
// Load palette data from "pal" resource
pal_data = PSB_getResourceData(&p, v269);           // 0x6970D4
pal_size = v269[0];                                  // 0x6970D8

// Allocate palette array (pal_size / 4 entries)
palette = std::vector<uint32_t>(pal_size / 4);       // 0x697100

// Reverse RGB in palette entries (RGBA → BGRA)
TVPReverseRGB(palette, pal_data, pal_size / 4);      // 0x6971D0

// Expand 8-bit indices to 32-bit pixels using palette
TVPBLExpand8BitTo32BitPal(output_buf, index_buf, pixel_count, palette);  // 0x6971F0

free(index_buf);                                      // 0x697204
```

**Key**: `TVPReverseRGB` swaps byte 0 (R) and byte 2 (B) in each 4-byte group. The palette entries in PSB are stored as RGBA; after `TVPReverseRGB`, they become BGRA (KiKiRi2's internal pixel format).

## Path B: Raw Palette (no compression)

**Address range**: `0x697060–0x6970A8` (raw copy), then same palette expand as Path A.

```c
raw_data = PSB_getResourceData(v262, v269);          // 0x697070
memcpy(index_buf, raw_data, compressed_size);        // 0x6970A8
// Then identical palette expansion as Path A
```

## Path C: Raw Pixels (no compression, no palette)

**Address range**: `0x697150–0x6971A4`

```c
raw_data = PSB_getResourceData(v262, v269);          // 0x697160
TVPReverseRGB(output_buf, raw_data, pixel_count);    // 0x6971A4
```

Direct copy with R↔B swap. Input is RGBA pixels, output is BGRA.

## Path D: RL + No Palette (align=4) — MOST COMMON FOR MOTION FILES

**Address range**: `0x696D00–0x696DC0` (RL loop), `0x696DC4–0x696DE0` (TVPReverseRGB)

This is the code path used by `.mtn` motion files (YuzuSoft logo, character animations, etc.) where pixel data is stored as compressed RGBA without a palette.

### RL Decompression (align=4)

Decompresses directly into the output 32-bit pixel buffer.

```c
// v165 = compressed data pointer (from PSB resource "pixel")
// v169 = compressed data end (v165 + compressed_size)
// v168 = output buffer pointer (uint32_t* / int32x4_t*, 4-byte aligned)

while (v165 < v169) {
    marker = *v165;
    v165++;
    
    if (!(marker & 0x80)) {
        // Literal: copy (marker + 1) * 4 bytes verbatim
        count = marker + 1;                         // 0x696D14
        byte_count = 4 * count;                     // 0x696D18
        memcpy(v168, v165, byte_count);             // 0x696D28
        v168 += count;        // advance by count uint32s  // 0x696D2C
        v165 += byte_count;   // advance by byte_count     // 0x696D30
    } else {
        // RLE run: repeat next 4-byte value (marker & 0x7F) + 3 times
        pixel_value = *(uint32_t*)v165;             // 0x696D40  (read 4 bytes)
        total_count = (marker & 0x7F) + 3;          // 0x696D48
        
        // NEON-optimized fill (vdupq_n_s32):
        // Processes 8 pixels at a time using 128-bit SIMD
        neon_count = total_count & ~7;              // 0x696D64  (round down to 8)
        if (neon_count > 0) {
            vec = vdupq_n_s32(pixel_value);         // 0x696D74
            for (i = 0; i < neon_count; i += 8) {
                // Store 2x int32x4_t = 8 pixels per iteration
                vst1q_s32(dst, vec);                // 0x696D80
                vst1q_s32(dst+4, vec);              // 0x696D80
            }
        }
        // Scalar tail for remaining pixels
        remainder = total_count - neon_count;       // 0x696D9C
        for (i = 0; i < remainder; i++) {
            *dst++ = pixel_value;                   // 0x696DA4
        }
        
        v168 += total_count;                        // 0x696DAC  (4*count + 12 = 4*(count+3))
        v165 += 5;            // 1 marker + 4 value bytes  // 0x696DB8
    }
}
```

### Post-Processing

After RL decompression:

```c
TVPReverseRGB(output_buf, output_buf, pixel_count);  // 0x696DE0
```

In-place R↔B swap on the decompressed buffer (RGBA → BGRA).

## RL Format Summary

| Field | align=1 (palette) | align=4 (no palette) |
|-------|-------------------|---------------------|
| **Marker byte** | 1 byte | 1 byte |
| **RLE condition** | `marker & 0x80` | `marker & 0x80` |
| **RLE count** | `(marker & 0x7F) + 3` | `(marker & 0x7F) + 3` |
| **RLE value size** | 1 byte | 4 bytes (uint32) |
| **RLE src advance** | 2 bytes (marker + value) | 5 bytes (marker + 4-byte value) |
| **Literal count** | `marker + 1` bytes | `(marker + 1) * 4` bytes |
| **Literal src advance** | 1 + count bytes | 1 + count*4 bytes |
| **Output element** | 1 byte (palette index) | 4 bytes (RGBA pixel) |
| **Post-process** | palette expand + TVPReverseRGB | TVPReverseRGB (in-place) |

## NEON Optimization Detail (align=4 RLE path)

The align=4 RLE fill uses ARM NEON SIMD for performance:

```
vdupq_n_s32(pixel_value)    → Duplicate pixel across 128-bit register (4 pixels)
Store 2 × int32x4_t         → 8 pixels per loop iteration
Scalar loop for remainder   → Handles count % 8 leftover pixels
```

This is visible at `0x696D64–0x696DAC`. The C++ equivalent (without NEON) is simply:
```c
for (i = 0; i < count; i++)
    *dst++ = pixel_value;
```

## Alpha Check (all paths)

After decoding, the function checks if all pixels are fully transparent (alpha=0):

```c
// 0x697210–0x697248
if (pixel_count > 0) {
    for (i = 0; i < pixel_count; i++) {
        if (pixels[i * 4 + 3] != 0)  // check alpha byte
            break;
    }
    if (i >= pixel_count) {
        // All alpha=0 → free pixel buffer, set to null, mark as 2x2
        free(pixels);
        entry->pixels = nullptr;
        entry->size = {2, 2};  // sentinel value
    }
}
```

## Texture Creation (after decode)

After all resource entries are decoded, the function creates GPU textures:

```c
// 0x6968D4–0x6968F8
texture = Motion_createTextureFromPixels(renderer);
texture->init(
    0,                    // internal format
    4 * width,            // row stride
    width,                // width
    height,               // height
    4,                    // bytes per pixel
    1                     // mipmap levels
);
```

## Comparison with Our Implementation

Our `decompressPsbRL()` in `Player.cpp` implements Path D (align=4, no palette):

| Aspect | libkrkr2.so | Our implementation |
|--------|-------------|-------------------|
| RLE count | `(marker & 0x7F) + 3` | `(marker & 0x7F) + 3` |
| Literal count | `(marker + 1) * 4` bytes | `(marker + 1) * align` bytes |
| Value size | 4 bytes (hardcoded) | `align` bytes (parameterized) |
| NEON optimization | Yes (vdupq_n_s32) | No (memcpy loop) |
| R↔B swap | TVPReverseRGB after decompress | Manual swap during pixel copy in renderToLayer |
| Palette path | TVPBLExpand8BitTo32BitPal | Not implemented (align=1 parameter available) |

**Our implementation matches libkrkr2.so's logic for the align=4 path.** The NEON optimization is a performance detail that doesn't affect correctness. The R↔B swap is done at a different stage (during copy to Layer bitmap rather than in-place after decompress) but produces the same result.

## Historical Bug

The original implementation used a **channel-plane** decomposition model (4 separate channels, each RL-compressed independently with align=1 and count+1). This was incorrect — PSB RL for non-palette images uses **interleaved** 4-byte pixel groups with count+3. The fix was confirmed by decompiling this exact function.
