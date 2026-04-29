// Standalone WASM harness for PSB RL decompression.
// Function copied from motionplayer/PlayerInternal.h (lines 65-98).
// Aligned to libkrkr2.so sub_695DE8 (0x695DE8).
//
// @exports: _run_psb_rl_decompress,_get_compressed_ptr
// @requires-lldb

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// Verbatim copy of motion::internal::decompressPsbRL from PlayerInternal.h.
// PSB RL decompression — two variants based on libkrkr2.so sub_695DE8:
//
// align=1 (with palette): single-byte RLE, used with 8-bit indexed data
//   RLE run:  count = (marker & 0x7F) + 3, repeat 1 byte
//   Literal:  count = marker + 1, copy count bytes
//
// align=4 (no palette, RGBA8): 4-byte RLE, used with 32-bit pixel data
//   RLE run:  count = (marker & 0x7F) + 3, repeat 4 bytes
//   Literal:  count = marker + 1, copy count*4 bytes
//   (0x696D00-0x696D98 in libkrkr2.so)
std::vector<std::uint8_t> decompressPsbRL(
    const std::vector<std::uint8_t> &compressed,
    size_t elementCount, int align = 4) {
    const size_t outputSize = elementCount * static_cast<size_t>(align);
    std::vector<std::uint8_t> output(outputSize, 0);

    const auto *src = compressed.data();
    const auto *srcEnd = src + compressed.size();
    auto *dst = output.data();
    const auto *dstEnd = dst + outputSize;

    while(src < srcEnd && dst < dstEnd) {
        const auto marker = *src++;
        if(marker & 0x80) {
            // RLE run: repeat `align` bytes (count) times
            const size_t count = (marker & 0x7F) + 3;
            if(src + align > srcEnd) break;
            for(size_t i = 0; i < count && dst + align <= dstEnd; i++) {
                std::memcpy(dst, src, align);
                dst += align;
            }
            src += align;
        } else {
            // Literal: copy (marker+1)*align bytes verbatim
            const size_t count = (marker + 1) * static_cast<size_t>(align);
            if(src + count > srcEnd) break;
            const size_t n = std::min(count,
                static_cast<size_t>(dstEnd - dst));
            std::memcpy(dst, src, n);
            src += count;
            dst += n;
        }
    }
    return output;
}

} // namespace

static std::uint8_t g_compressed[4096];
static std::uint8_t g_decompressed[16384];
static std::int32_t g_decompressed_size;
static std::int32_t g_call_index;

std::uint64_t packBytes64(const std::uint8_t *data,
                          size_t offset,
                          size_t size) {
    std::uint64_t out = 0;
    for(size_t i = 0; i < 8 && offset + i < size; ++i) {
        out |= static_cast<std::uint64_t>(data[offset + i]) << (i * 8);
    }
    return out;
}

extern "C" __attribute__((noinline, used))
void krkr2_lldb_psb_rl_decompress_sample(std::int32_t call_index,
                                         std::int32_t output_size,
                                         std::uint64_t bytes0,
                                         std::uint64_t bytes1) {
    asm volatile(
        ""
        :
        : "r"(call_index), "r"(output_size), "r"(bytes0), "r"(bytes1)
        : "memory");
}

extern "C" {

std::uint8_t *get_compressed_ptr() { return g_compressed; }

void run_psb_rl_decompress(std::int32_t compressed_len,
                           std::int32_t element_count,
                           std::int32_t align) {
    std::vector<std::uint8_t> input(g_compressed,
                                    g_compressed + compressed_len);
    auto output = decompressPsbRL(input,
                                  static_cast<size_t>(element_count), align);
    g_decompressed_size = static_cast<std::int32_t>(output.size());
    const size_t copy_n = std::min(output.size(), sizeof(g_decompressed));
    std::memcpy(g_decompressed, output.data(), copy_n);
    krkr2_lldb_psb_rl_decompress_sample(
        g_call_index++, g_decompressed_size,
        packBytes64(g_decompressed, 0, copy_n),
        packBytes64(g_decompressed, 8, copy_n));
}

} // extern "C"
