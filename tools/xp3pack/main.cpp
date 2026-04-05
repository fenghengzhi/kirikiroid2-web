// xp3pack — Create XP3 archives from a list of files.
// Aligned to the XP3 format used by KiKiRi2 engine.
//
// Usage:
//   xp3pack -o output.xp3 file1 [file2 ...]
//   xp3pack -o output.xp3 -r directory/
//   xp3pack -o output.xp3 -m arcpath1=localpath1 [arcpath2=localpath2 ...]
//
// Files are stored with zlib compression. Archive paths default to the
// filename relative to cwd, or can be explicitly mapped with -m.

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <zlib.h>

namespace fs = std::filesystem;

// XP3 header: "XP3\r\n \n\x1a" + "\x8bg\x01"
static const uint8_t kXP3Header[] = {
    0x58, 0x50, 0x33, 0x0D, 0x0A, 0x20, 0x0A, 0x1A, 0x8B, 0x67, 0x01
};

struct FileEntry {
    std::string arcPath;   // path inside XP3
    std::string localPath; // path on disk
};

static std::vector<uint8_t> compressData(const std::vector<uint8_t> &data) {
    uLongf compLen = compressBound(static_cast<uLong>(data.size()));
    std::vector<uint8_t> comp(compLen);
    int ret = compress2(comp.data(), &compLen, data.data(),
                        static_cast<uLong>(data.size()), Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {
        throw std::runtime_error("zlib compress failed: " + std::to_string(ret));
    }
    comp.resize(compLen);
    return comp;
}

static std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) throw std::runtime_error("Cannot open: " + path);
    auto size = ifs.tellg();
    ifs.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char *>(data.data()), size);
    return data;
}

template <typename T>
static void writeLE(std::ostream &os, T value) {
    os.write(reinterpret_cast<const char *>(&value), sizeof(T));
}

static void writeBytes(std::ostream &os, const void *data, size_t len) {
    os.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(len));
}

static std::u16string toUTF16LE(const std::string &utf8) {
    // Simple UTF-8 to UTF-16LE conversion
    std::u16string result;
    size_t i = 0;
    while (i < utf8.size()) {
        uint32_t cp = 0;
        uint8_t b = static_cast<uint8_t>(utf8[i]);
        if (b < 0x80) {
            cp = b; i += 1;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            if (i + 1 < utf8.size()) cp = (cp << 6) | (utf8[i+1] & 0x3F);
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0F;
            if (i + 1 < utf8.size()) cp = (cp << 6) | (utf8[i+1] & 0x3F);
            if (i + 2 < utf8.size()) cp = (cp << 6) | (utf8[i+2] & 0x3F);
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07;
            if (i + 1 < utf8.size()) cp = (cp << 6) | (utf8[i+1] & 0x3F);
            if (i + 2 < utf8.size()) cp = (cp << 6) | (utf8[i+2] & 0x3F);
            if (i + 3 < utf8.size()) cp = (cp << 6) | (utf8[i+3] & 0x3F);
            i += 4;
        } else {
            i += 1; continue;
        }
        if (cp <= 0xFFFF) {
            result.push_back(static_cast<char16_t>(cp));
        } else {
            // Surrogate pair
            cp -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 | (cp >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 | (cp & 0x3FF)));
        }
    }
    return result;
}

void packXP3(const std::string &outputPath,
             const std::vector<FileEntry> &entries) {
    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs) throw std::runtime_error("Cannot create: " + outputPath);

    // 1. Write XP3 header (11 bytes)
    writeBytes(ofs, kXP3Header, sizeof(kXP3Header));

    // 2. Placeholder for index offset (8 bytes)
    auto indexOffsetPos = ofs.tellp();
    writeLE<uint64_t>(ofs, 0);

    // 3. Write file data segments, collect metadata
    struct SegmentInfo {
        std::string arcPath;
        uint64_t rawSize;
        uint64_t storedSize;
        uint64_t offset;
        bool compressed;
        uint32_t hash;
    };
    std::vector<SegmentInfo> segments;

    for (const auto &entry : entries) {
        auto raw = readFile(entry.localPath);
        auto comp = compressData(raw);
        bool useComp = comp.size() < raw.size();

        SegmentInfo seg;
        seg.arcPath = entry.arcPath;
        seg.rawSize = raw.size();
        seg.offset = static_cast<uint64_t>(ofs.tellp());
        seg.hash = static_cast<uint32_t>(adler32(1, raw.data(),
                                                  static_cast<uInt>(raw.size())));

        if (useComp) {
            writeBytes(ofs, comp.data(), comp.size());
            seg.storedSize = comp.size();
            seg.compressed = true;
        } else {
            writeBytes(ofs, raw.data(), raw.size());
            seg.storedSize = raw.size();
            seg.compressed = false;
        }
        segments.push_back(seg);
    }

    // 4. Build index data (chunk-based)
    std::vector<uint8_t> indexBuf;
    auto appendToIndex = [&](const void *data, size_t len) {
        const auto *p = reinterpret_cast<const uint8_t *>(data);
        indexBuf.insert(indexBuf.end(), p, p + len);
    };
    auto appendLE = [&]<typename T>(T value) {
        appendToIndex(&value, sizeof(T));
    };

    for (const auto &seg : segments) {
        // Build File chunk content (info + segm + adlr)
        std::vector<uint8_t> fileChunk;
        auto appendToFile = [&](const void *data, size_t len) {
            const auto *p = reinterpret_cast<const uint8_t *>(data);
            fileChunk.insert(fileChunk.end(), p, p + len);
        };
        auto appendFileLE = [&]<typename T>(T value) {
            appendToFile(&value, sizeof(T));
        };

        // Convert path to UTF-16LE with backslashes
        std::string arcPathBS = seg.arcPath;
        std::replace(arcPathBS.begin(), arcPathBS.end(), '/', '\\');
        auto name16 = toUTF16LE(arcPathBS);

        // info sub-chunk
        std::vector<uint8_t> infoBuf;
        auto appendInfo = [&](const void *data, size_t len) {
            const auto *p = reinterpret_cast<const uint8_t *>(data);
            infoBuf.insert(infoBuf.end(), p, p + len);
        };
        auto appendInfoLE = [&]<typename T>(T value) {
            appendInfo(&value, sizeof(T));
        };
        appendInfoLE(uint32_t(0));                 // flags
        appendInfoLE(uint64_t(seg.rawSize));       // original size
        appendInfoLE(uint64_t(seg.storedSize));    // stored size
        appendInfoLE(uint16_t(name16.size()));     // name length (chars)
        appendInfo(name16.data(), name16.size() * 2); // name UTF-16LE

        appendToFile("info", 4);
        appendFileLE(uint64_t(infoBuf.size()));
        appendToFile(infoBuf.data(), infoBuf.size());

        // segm sub-chunk (1 segment per file)
        std::vector<uint8_t> segmBuf;
        auto appendSegm = [&](const void *data, size_t len) {
            const auto *p = reinterpret_cast<const uint8_t *>(data);
            segmBuf.insert(segmBuf.end(), p, p + len);
        };
        auto appendSegmLE = [&]<typename T>(T value) {
            appendSegm(&value, sizeof(T));
        };
        appendSegmLE(uint32_t(seg.compressed ? 1 : 0)); // flags
        appendSegmLE(uint64_t(seg.offset));              // offset in archive
        appendSegmLE(uint64_t(seg.rawSize));             // original size
        appendSegmLE(uint64_t(seg.storedSize));          // stored size

        appendToFile("segm", 4);
        appendFileLE(uint64_t(segmBuf.size()));
        appendToFile(segmBuf.data(), segmBuf.size());

        // adlr sub-chunk
        appendToFile("adlr", 4);
        appendFileLE(uint64_t(4));
        appendFileLE(uint32_t(seg.hash));

        // File chunk wrapper
        appendToIndex("File", 4);
        appendLE(uint64_t(fileChunk.size()));
        appendToIndex(fileChunk.data(), fileChunk.size());
    }

    // 5. Compress and write index
    auto compressedIndex = compressData(indexBuf);
    uint64_t indexOffset = static_cast<uint64_t>(ofs.tellp());

    writeLE<uint8_t>(ofs, 1);  // flags: zlib compressed, no continue
    writeLE<uint64_t>(ofs, compressedIndex.size());
    writeLE<uint64_t>(ofs, indexBuf.size());
    writeBytes(ofs, compressedIndex.data(), compressedIndex.size());

    // 6. Write index offset at header position
    ofs.seekp(indexOffsetPos);
    writeLE<uint64_t>(ofs, indexOffset);

    ofs.close();
    spdlog::info("Created {}: {} file(s), {} bytes",
                 outputPath, entries.size(), fs::file_size(outputPath));
}

void collectFiles(const std::string &dir, const std::string &prefix,
                  std::vector<FileEntry> &entries) {
    for (const auto &entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string rel = fs::relative(entry.path(), dir).string();
        // Normalize to forward slashes
        std::replace(rel.begin(), rel.end(), '\\', '/');
        std::string arcPath = prefix.empty() ? rel : prefix + "/" + rel;
        entries.push_back({arcPath, entry.path().string()});
    }
}

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program(PROGRAM_NAME, VERSION);

    program.add_argument("-o", "--output")
        .help("output XP3 file path")
        .required();
    program.add_argument("-r", "--recursive")
        .help("add all files from directory recursively")
        .nargs(argparse::nargs_pattern::any);
    program.add_argument("-m", "--map")
        .help("map arcpath=localpath (explicit path mapping)")
        .nargs(argparse::nargs_pattern::any);
    program.add_argument("files")
        .help("input files (arcpath = relative to cwd)")
        .nargs(argparse::nargs_pattern::any);

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    spdlog::set_level(spdlog::level::info);
    static auto logger = spdlog::stdout_color_mt("core");
    spdlog::set_pattern("%^%v%$");
    spdlog::set_default_logger(logger);

    std::string output = program.get<std::string>("-o");
    std::vector<FileEntry> entries;

    // -r: recursive directory scan
    if (program.is_used("-r")) {
        for (const auto &dir : program.get<std::vector<std::string>>("-r")) {
            if (!fs::is_directory(dir)) {
                std::cerr << "Not a directory: " << dir << std::endl;
                return 1;
            }
            collectFiles(dir, "", entries);
        }
    }

    // -m: explicit mappings
    if (program.is_used("-m")) {
        for (const auto &mapping : program.get<std::vector<std::string>>("-m")) {
            auto eq = mapping.find('=');
            if (eq == std::string::npos) {
                std::cerr << "Invalid mapping (use arcpath=localpath): "
                          << mapping << std::endl;
                return 1;
            }
            entries.push_back({mapping.substr(0, eq), mapping.substr(eq + 1)});
        }
    }

    // positional: files relative to cwd
    if (program.is_used("files")) {
        for (const auto &file : program.get<std::vector<std::string>>("files")) {
            if (!fs::exists(file) || !fs::is_regular_file(file)) {
                std::cerr << "File not found: " << file << std::endl;
                return 1;
            }
            entries.push_back({fs::path(file).filename().string(), file});
        }
    }

    if (entries.empty()) {
        std::cerr << "No input files specified." << std::endl;
        std::cerr << program;
        return 1;
    }

    try {
        packXP3(output, entries);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
