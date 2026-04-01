// ksdec — KiriKiri2 scenario file decryptor
// Decrypts .ks/.tjs files encrypted with the FEFE cipher modes used by
// KiriKiri2/kirikiroid2's TextStream (see cpp/core/base/TextStream.cpp).
//
// Supports:
//   mode 0: XOR cipher  — ch ^= (((ch & 0xFE) << 8) ^ 1)  for ch >= 0x20
//   mode 1: bit swap    — adjacent bit swap on each char16
//   mode 2: zlib compressed UTF-16LE
//   plain:  UTF-16LE with BOM (no encryption)
//
// Output: UTF-8 text to stdout (or file with -o).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

// zlib for mode 2
#include <zlib.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-o output.txt] input.ks [input2.ks ...]\n"
        "\n"
        "Decrypts KiriKiri2 FEFE-encrypted scenario files (.ks/.tjs)\n"
        "and outputs UTF-8 text.\n"
        "\n"
        "Options:\n"
        "  -o FILE   Write output to FILE instead of stdout\n"
        "            (only valid with a single input file)\n"
        "\n"
        "Supported formats:\n"
        "  FEFE mode 0: XOR cipher\n"
        "  FEFE mode 1: adjacent bit swap\n"
        "  FEFE mode 2: zlib compressed\n"
        "  Plain UTF-16LE/BE with BOM\n"
        "  Plain UTF-8 (with or without BOM)\n",
        prog);
}

// Convert a UTF-16LE buffer to UTF-8 string
static std::string utf16le_to_utf8(const char16_t *src, size_t len) {
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        uint32_t cp = src[i];
        // Handle surrogate pairs
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len) {
            uint32_t lo = src[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                i++;
            }
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

static bool decode_file(const char *path, FILE *out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> raw(fsize);
    if (fread(raw.data(), 1, fsize, f) != static_cast<size_t>(fsize)) {
        fprintf(stderr, "Error: cannot read '%s'\n", path);
        fclose(f);
        return false;
    }
    fclose(f);

    size_t size = raw.size();
    std::vector<char16_t> text;

    // Check FEFE encryption header
    if (size >= 3 && raw[0] == 0xFE && raw[1] == 0xFE) {
        uint8_t mode = raw[2];

        if (mode == 0 || mode == 1) {
            // Encrypted UTF-16LE
            // Layout: [FE FE mode] [FF FE bom] [encrypted char16 data...]
            if (size < 5) {
                fprintf(stderr, "Error: '%s' too short for FEFE mode %d\n",
                        path, mode);
                return false;
            }
            const auto *src =
                reinterpret_cast<const char16_t *>(raw.data() + 5);
            size_t len = (size - 5) / 2;
            text.resize(len);
            for (size_t i = 0; i < len; i++) {
                char16_t ch = src[i];
                if (mode == 0) {
                    if (ch >= 0x20)
                        ch ^= static_cast<char16_t>(
                            ((ch & 0xFE) << 8) ^ 1);
                } else {
                    ch = static_cast<char16_t>(
                        ((ch & 0xAAAA) >> 1) | ((ch & 0x5555) << 1));
                }
                text[i] = ch;
            }
            fprintf(stderr, "Decrypted: '%s' (FEFE mode %d, %zu chars)\n",
                    path, mode, len);
        } else if (mode == 2) {
            // Compressed UTF-16LE
            // Layout: [FE FE 02] [FF FE bom] [u64 compSize] [u64 uncompSize]
            //         [compressed data...]
            if (size < 5 + 16) {
                fprintf(stderr,
                        "Error: '%s' too short for FEFE mode 2\n", path);
                return false;
            }
            uint8_t *ptr = raw.data() + 5;
            uint64_t compressedSize, uncompressedSize;
            memcpy(&compressedSize, ptr, 8);
            ptr += 8;
            memcpy(&uncompressedSize, ptr, 8);
            ptr += 8;

            std::vector<uint8_t> uncompBuf(uncompressedSize);
            auto destLen = static_cast<unsigned long>(uncompressedSize);
            int ret = uncompress(uncompBuf.data(), &destLen, ptr,
                                 static_cast<unsigned long>(compressedSize));
            if (ret != Z_OK) {
                fprintf(stderr,
                        "Error: '%s' zlib decompression failed (ret=%d)\n",
                        path, ret);
                return false;
            }

            size_t len = destLen / 2;
            text.assign(reinterpret_cast<char16_t *>(uncompBuf.data()),
                        reinterpret_cast<char16_t *>(uncompBuf.data()) + len);
            fprintf(stderr, "Decompressed: '%s' (FEFE mode 2, %zu chars)\n",
                    path, len);
        } else {
            fprintf(stderr,
                    "Error: '%s' unsupported FEFE mode %d\n", path, mode);
            return false;
        }
    }
    // UTF-16LE BOM
    else if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        size_t len = (size - 2) / 2;
        text.assign(reinterpret_cast<const char16_t *>(raw.data() + 2),
                    reinterpret_cast<const char16_t *>(raw.data() + 2) + len);
        fprintf(stderr, "Plain UTF-16LE: '%s' (%zu chars)\n", path, len);
    }
    // UTF-16BE BOM
    else if (size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        size_t len = (size - 2) / 2;
        const auto *src =
            reinterpret_cast<const char16_t *>(raw.data() + 2);
        text.resize(len);
        for (size_t i = 0; i < len; i++) {
            char16_t ch = src[i];
            text[i] = static_cast<char16_t>((ch >> 8) | (ch << 8));
        }
        fprintf(stderr, "Plain UTF-16BE: '%s' (%zu chars)\n", path, len);
    }
    // UTF-8 BOM or plain text — pass through
    else {
        size_t skip = 0;
        if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF)
            skip = 3;
        fwrite(raw.data() + skip, 1, size - skip, out);
        fprintf(stderr, "Plain text: '%s' (%zu bytes)\n", path, size - skip);
        return true;
    }

    // Remove leading BOM from decoded text if present
    size_t start = 0;
    if (!text.empty() && text[0] == 0xFEFF) start = 1;

    std::string utf8 = utf16le_to_utf8(text.data() + start,
                                        text.size() - start);
    fwrite(utf8.data(), 1, utf8.size(), out);
    return true;
}

int main(int argc, char **argv) {
    const char *outPath = nullptr;
    std::vector<const char *> inputs;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outPath = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            inputs.push_back(argv[i]);
        }
    }

    if (inputs.empty()) {
        usage(argv[0]);
        return 1;
    }

    if (outPath && inputs.size() > 1) {
        fprintf(stderr, "Error: -o can only be used with a single input\n");
        return 1;
    }

    FILE *out = stdout;
    if (outPath) {
        out = fopen(outPath, "wb");
        if (!out) {
            fprintf(stderr, "Error: cannot open output '%s'\n", outPath);
            return 1;
        }
    }

#ifdef _WIN32
    if (!outPath)
        _setmode(_fileno(stdout), _O_BINARY);
#endif

    bool ok = true;
    for (const char *input : inputs) {
        if (!decode_file(input, out))
            ok = false;
    }

    if (outPath)
        fclose(out);

    return ok ? 0 : 1;
}
