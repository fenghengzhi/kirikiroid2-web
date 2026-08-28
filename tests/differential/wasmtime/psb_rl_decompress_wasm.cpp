// Standalone WASM harness for PSB RL decompression.
// Four-binary evidence covers two consumers: atlas materialization at
// 6931C8/570F54/1000F4098/F0BE4 and ObjSource lazy materialization at
// 6D7834/599A34/10012686C/125D4C. Android arm64 inlines both loops in each
// consumer; Android armv7 inlines RL8 and shares 571DA4 for RL32; iOS shares
// 1000F5510/F1F6A for RL8 and 1000F5474/F1F10 for RL32.
//
// @exports: _run_psb_rl_decompress,_get_compressed_ptr,_get_decompressed_ptr,_get_decompressed_size,_run_psb_rl_decompress_direct

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// The reference source has two concrete loops selected by palette presence;
// it does not have a generic `align` loop. Both loops stop only when the
// compressed input pointer reaches its end. They deliberately have no output
// bound checks and assume a valid stream that exactly fills the allocation.
//
// PSB RL decompression:
//
// align=1 (with palette): single-byte RLE, used with 8-bit indexed data
//   RLE run:  count = (marker & 0x7F) + 3, repeat 1 byte
//   Literal:  count = marker + 1, copy count bytes
//
// align=4 (no palette, RGBA8): 4-byte RLE, used with 32-bit pixel data
//   RLE run:  count = (marker & 0x7F) + 3, repeat 4 bytes
//   Literal:  count = marker + 1, copy count*4 bytes
void decodePsbRL8_guess(std::uint8_t *dst, const std::uint8_t *src,
                        std::int32_t compressedSize) {
    if(compressedSize < 1) return;

    const auto *srcEnd = src + compressedSize;
    do {
        const std::uint8_t marker = *src++;
        if(marker & 0x80) {
            const size_t count = (marker & 0x7F) + 3;
            std::memset(dst, *src++, count);
            dst += count;
        } else {
            const size_t count = static_cast<size_t>(marker) + 1;
            std::memcpy(dst, src, count);
            src += count;
            dst += count;
        }
    } while(src < srcEnd);
}

void decodePsbRL32_guess(std::uint32_t *dst, const std::uint8_t *src,
                         std::int32_t compressedSize) {
    if(compressedSize < 1) return;

    const auto *srcEnd = src + compressedSize;
    do {
        const std::uint8_t marker = *src++;
        if(marker & 0x80) {
            size_t count = (marker & 0x7F) + 3;
            std::uint32_t pixel;
            std::memcpy(&pixel, src, sizeof(pixel));
            src += sizeof(pixel);
            std::fill_n(dst, count, pixel);
            dst += count;
        } else {
            const size_t count = static_cast<size_t>(marker) + 1;
            const size_t bytes = count * sizeof(*dst);
            std::memcpy(dst, src, bytes);
            src += bytes;
            dst += count;
        }
    } while(src < srcEnd);
}

} // namespace

static std::uint8_t g_compressed[4096];
static std::uint8_t g_decompressed[16384];
static std::int32_t g_decompressed_size;

extern "C" {

std::uint8_t *get_compressed_ptr() { return g_compressed; }
std::uint8_t *get_decompressed_ptr() { return g_decompressed; }
std::int32_t get_decompressed_size() { return g_decompressed_size; }

void run_psb_rl_decompress(std::int32_t compressed_len,
                           std::int32_t element_count,
                           std::int32_t align) {
    const size_t outputSize = static_cast<size_t>(element_count) *
                              static_cast<size_t>(align);
    std::vector<std::uint8_t> output(outputSize);
    if(align == 1) {
        decodePsbRL8_guess(output.data(), g_compressed, compressed_len);
    } else {
        decodePsbRL32_guess(
            reinterpret_cast<std::uint32_t *>(output.data()),
            g_compressed, compressed_len);
    }
    g_decompressed_size = static_cast<std::int32_t>(output.size());
    const size_t copy_n = std::min(output.size(), sizeof(g_decompressed));
    std::memcpy(g_decompressed, output.data(), copy_n);
}

std::int32_t run_psb_rl_decompress_direct(
    std::int32_t compressed_len,
    std::int32_t element_count,
    std::int32_t align,
    std::uint64_t bytes0,
    std::uint64_t bytes1,
    std::int32_t output_index) {
    for(size_t i = 0; i < 8; ++i) {
        g_compressed[i] = static_cast<std::uint8_t>(bytes0 >> (i * 8));
        g_compressed[8 + i] =
            static_cast<std::uint8_t>(bytes1 >> (i * 8));
    }
    run_psb_rl_decompress(compressed_len, element_count, align);
    if(output_index < 0) return g_decompressed_size;
    if(output_index >= g_decompressed_size) return -1;
    return g_decompressed[static_cast<size_t>(output_index)];
}

} // extern "C"
