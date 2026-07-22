#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <argparse/argparse.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "BinaryStream.h"
#include "GraphicsLoaderIntf.h"
#include "LayerBitmapIntf.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "SysInitImpl.h"
#include "SysInitIntf.h"
#include "../common/RawPsb.h"

namespace fs = std::filesystem;

namespace {
    std::string normalizePath(const std::string &path) {
        if(path.empty()) return path;
        fs::path result(path);
        if(path[0] == '~') {
#if defined(_WIN32)
            const char *home = std::getenv("USERPROFILE");
#else
            const char *home = std::getenv("HOME");
#endif
            if(home) result = fs::path(home) / path.substr(1);
        }
        try {
            return fs::weakly_canonical(result).string();
        } catch(...) {
            return fs::absolute(result).string();
        }
    }

    int nodeInt(const PSB::PSBRawNode &node, const char *key,
                int fallback = 0) {
        PSB::PSBRawNode value;
        return node.GetDictionaryValue(key, value)
            ? static_cast<int>(value.GetInt()) : fallback;
    }

    double nodeNumber(const PSB::PSBRawNode &node, const char *key,
                      double fallback = 0.0) {
        PSB::PSBRawNode value;
        return node.GetDictionaryValue(key, value)
            ? value.GetDouble() : fallback;
    }

    std::string nodeString(const PSB::PSBRawNode &node, const char *key) {
        PSB::PSBRawNode value;
        if(!node.GetDictionaryValue(key, value)) return {};
        const char *text = value.GetString();
        return text ? text : "";
    }

    std::vector<std::uint8_t> resourceBytes(const PSB::PSBRawNode &node) {
        std::uint32_t size = 0;
        const auto *data = node.GetResource(size);
        if(!data || size == 0) return {};
        return {data, data + size};
    }

    std::vector<std::uint8_t> decompressRL(
        const std::vector<std::uint8_t> &input, std::size_t pixelCount,
        std::size_t bytesPerPixel) {
        std::vector<std::uint8_t> output(pixelCount * bytesPerPixel, 0);
        std::size_t src = 0;
        std::size_t dstPixel = 0;
        while(src < input.size() && dstPixel < pixelCount) {
            const std::uint8_t marker = input[src++];
            if((marker & 0x80u) != 0) {
                const std::size_t count = (marker & 0x7fu) + 3u;
                if(src + bytesPerPixel > input.size()) break;
                for(std::size_t i = 0; i < count && dstPixel < pixelCount;
                    ++i, ++dstPixel) {
                    std::memcpy(output.data() + dstPixel * bytesPerPixel,
                                input.data() + src, bytesPerPixel);
                }
                src += bytesPerPixel;
            } else {
                const std::size_t count = marker + 1u;
                const std::size_t available =
                    (input.size() - src) / bytesPerPixel;
                const std::size_t copyCount = std::min(
                    {count, available, pixelCount - dstPixel});
                std::memcpy(output.data() + dstPixel * bytesPerPixel,
                            input.data() + src,
                            copyCount * bytesPerPixel);
                src += copyCount * bytesPerPixel;
                dstPixel += copyCount;
                if(copyCount != count) break;
            }
        }
        return output;
    }

    bool decodePixels(const PSB::PSBRawNode &container, int width, int height,
                      std::vector<std::uint8_t> &decoded,
                      bool &decodedIsBgra) {
        decoded.clear();
        decodedIsBgra = false;
        if(width <= 0 || height <= 0) return false;

        PSB::PSBRawNode pixelNode;
        if(!container.GetDictionaryValue("pixel", pixelNode)) return false;
        auto pixels = resourceBytes(pixelNode);
        if(pixels.empty()) return false;

        const std::size_t pixelCount = static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height);
        const bool compressed = nodeString(container, "compress") == "RL";
        PSB::PSBRawNode paletteNode;
        if(container.GetDictionaryValue("pal", paletteNode)) {
            const auto palette = resourceBytes(paletteNode);
            if(palette.size() < 4) return false;
            auto indices = compressed
                ? decompressRL(pixels, pixelCount, 1)
                : std::move(pixels);
            indices.resize(pixelCount, 0);
            decoded.assign(pixelCount * 4u, 0);
            const std::size_t paletteCount = palette.size() / 4u;
            for(std::size_t i = 0; i < pixelCount; ++i) {
                const std::size_t entry =
                    std::min<std::size_t>(indices[i], paletteCount - 1u) * 4u;
                decoded[i * 4u + 0u] = palette[entry + 2u];
                decoded[i * 4u + 1u] = palette[entry + 1u];
                decoded[i * 4u + 2u] = palette[entry + 0u];
                decoded[i * 4u + 3u] = palette[entry + 3u];
            }
            decodedIsBgra = true;
            return true;
        }

        decoded = compressed
            ? decompressRL(pixels, pixelCount, 4)
            : std::move(pixels);
        decoded.resize(pixelCount * 4u, 0);
        return true;
    }

    struct SourceImage final {
        int width{};
        int height{};
        double originX{};
        double originY{};
        bool bgra{};
        std::vector<std::uint8_t> pixels;
    };

    bool loadSourceImage(const PSB::PSBRawNode &root,
                         const std::string &source, SourceImage &result) {
        if(source.rfind("src/", 0) != 0) return false;
        const std::string tail = source.substr(4);
        const auto slash = tail.find('/');
        if(slash == std::string::npos) return false;
        const std::string groupName = tail.substr(0, slash);
        const std::string iconName = tail.substr(slash + 1);

        PSB::PSBRawNode sourceNode, groupNode, iconsNode, iconNode;
        if(!root.GetDictionaryValue("source", sourceNode) ||
           !sourceNode.GetDictionaryValue(groupName.c_str(), groupNode) ||
           !groupNode.GetDictionaryValue("icon", iconsNode) ||
           !iconsNode.GetDictionaryValue(iconName.c_str(), iconNode)) {
            return false;
        }

        result.width = nodeInt(iconNode, "width");
        result.height = nodeInt(iconNode, "height");
        if(result.width <= 0)
            result.width = nodeInt(iconNode, "truncated_width");
        if(result.height <= 0)
            result.height = nodeInt(iconNode, "truncated_height");
        result.originX = nodeNumber(iconNode, "originX");
        result.originY = nodeNumber(iconNode, "originY");
        if(result.width <= 0 || result.height <= 0) return false;

        PSB::PSBRawNode textureNode;
        if(groupNode.GetDictionaryValue("texture", textureNode)) {
            int textureWidth = nodeInt(textureNode, "width");
            int textureHeight = nodeInt(textureNode, "height");
            if(textureWidth <= 0)
                textureWidth = nodeInt(textureNode, "truncated_width");
            if(textureHeight <= 0)
                textureHeight = nodeInt(textureNode, "truncated_height");
            const int left = nodeInt(iconNode, "left");
            const int top = nodeInt(iconNode, "top");
            if(textureWidth > 0 && textureHeight > 0 && left >= 0 && top >= 0 &&
               left + result.width <= textureWidth &&
               top + result.height <= textureHeight) {
                std::vector<std::uint8_t> atlas;
                bool atlasBgra = false;
                if(decodePixels(textureNode, textureWidth, textureHeight,
                                atlas, atlasBgra)) {
                    result.pixels.assign(
                        static_cast<std::size_t>(result.width) *
                            static_cast<std::size_t>(result.height) * 4u,
                        0);
                    const std::size_t rowBytes =
                        static_cast<std::size_t>(result.width) * 4u;
                    for(int y = 0; y < result.height; ++y) {
                        const std::size_t sourceOffset =
                            (static_cast<std::size_t>(top + y) *
                                 static_cast<std::size_t>(textureWidth) +
                             static_cast<std::size_t>(left)) * 4u;
                        std::memcpy(result.pixels.data() +
                                        static_cast<std::size_t>(y) * rowBytes,
                                    atlas.data() + sourceOffset, rowBytes);
                    }
                    result.bgra = atlasBgra;
                    return true;
                }
            }
        }

        return decodePixels(iconNode, result.width, result.height,
                            result.pixels, result.bgra);
    }

    std::set<std::string> collectSourceNames(const PSB::PSBRawNode &root) {
        std::set<std::string> result;
        PSB::PSBRawNode sourceNode;
        if(!root.GetDictionaryValue("source", sourceNode)) return result;
        for(const auto &groupName : sourceNode.GetDictionaryKeys()) {
            PSB::PSBRawNode groupNode, iconsNode;
            if(!sourceNode.GetDictionaryValue(groupName.c_str(), groupNode) ||
               !groupNode.GetDictionaryValue("icon", iconsNode)) continue;
            for(const auto &iconName : iconsNode.GetDictionaryKeys()) {
                result.emplace("src/" + groupName + "/" + iconName);
            }
        }
        return result;
    }

    std::shared_ptr<tTVPBaseBitmap> makeBitmap(
        const SourceImage &image) {
        const std::size_t rowBytes = static_cast<std::size_t>(image.width) * 4u;
        const std::size_t required =
            static_cast<std::size_t>(image.height) * rowBytes;
        if(image.pixels.size() < required)
            throw std::runtime_error("decoded pixel buffer is too small");
        auto bitmap = std::make_shared<tTVPBaseBitmap>(
            static_cast<tjs_uint>(image.width),
            static_cast<tjs_uint>(image.height), 32);
        for(int y = 0; y < image.height; ++y) {
            auto *dst = static_cast<std::uint8_t *>(
                bitmap->GetScanLineForWrite(static_cast<tjs_uint>(y)));
            const auto *src = image.pixels.data() +
                static_cast<std::size_t>(y) * rowBytes;
            if(image.bgra) {
                std::memcpy(dst, src, rowBytes);
            } else {
                for(int x = 0; x < image.width; ++x) {
                    dst[x * 4 + 0] = src[x * 4 + 2];
                    dst[x * 4 + 1] = src[x * 4 + 1];
                    dst[x * 4 + 2] = src[x * 4 + 0];
                    dst[x * 4 + 3] = src[x * 4 + 3];
                }
            }
        }
        return bitmap;
    }

    void savePng(const fs::path &path, const tTVPBaseBitmap &bitmap) {
        fs::create_directories(path.parent_path());
        std::unique_ptr<tTJSBinaryStream> stream{
            TVPCreateBinaryStreamForWrite(ttstr(path.string()), TJS_W(""))};
        if(!stream)
            throw std::runtime_error("failed to create " + path.string());
        TVPSaveAsPNG(nullptr, stream.get(), &bitmap, TJS_W("png"), nullptr);
    }

    class ToolRuntimeScope final {
    public:
        ToolRuntimeScope() {
            const auto cwd = fs::current_path().string();
            TVPNativeProjectDir = ttstr(cwd);
            TVPProjectDir = TVPNormalizeStorageName(TVPNativeProjectDir);
            TVPInitScriptEngine();
            TVPInitializeBaseSystems();
            TVPSystemInit();
        }
    };
} // namespace

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program(PROGRAM_NAME, VERSION);
    program.add_argument("files").help("input .mtn/.psb motion file(s)")
        .nargs(argparse::nargs_pattern::at_least_one);
    program.add_argument("-o", "--output").help("output directory")
        .default_value(std::string("./"));
    program.add_argument("-s", "--seed")
        .help("decrypt seed for encrypted PSB files (0 = plain)")
        .default_value(0).scan<'i', int>();
    try {
        program.parse_args(argc, argv);
    } catch(const std::exception &error) {
        std::cerr << error.what() << '\n' << program;
        return 1;
    }

    spdlog::set_default_logger(spdlog::stdout_color_mt("mtndump"));
    spdlog::stdout_color_mt("core");
    spdlog::stdout_color_mt("tjs2");
    spdlog::stdout_color_mt("plugin");
    spdlog::set_pattern("%^%v%$");
    ToolRuntimeScope runtime;

    const fs::path outputRoot(normalizePath(
        program.get<std::string>("--output")));
    const auto seed = static_cast<std::uint32_t>(
        program.get<int>("--seed"));
    int failedFiles = 0;
    for(const auto &input : program.get<std::vector<std::string>>("files")) {
        const fs::path inputPath(normalizePath(input));
        if(!fs::is_regular_file(inputPath)) {
            spdlog::error("Skipping invalid file: {}", input);
            ++failedFiles;
            continue;
        }
        try {
            tools::rawpsb::Document document;
            if(!document.load(ttstr(inputPath.string()), seed)) {
                spdlog::error("Failed to load raw PSB: {}", inputPath.string());
                ++failedFiles;
                continue;
            }
            const auto root = document.file.GetRoot();
            const fs::path outputDir = outputRoot / inputPath.stem();
            fs::create_directories(outputDir);
            std::ofstream manifest(outputDir / "manifest.tsv");
            if(!manifest)
                throw std::runtime_error("failed to open manifest.tsv");
            manifest << "source\tpng\twidth\theight\torigin_x\torigin_y\t"
                        "decoded_bgra\n";

            std::size_t exported = 0;
            std::size_t skipped = 0;
            for(const auto &source : collectSourceNames(root)) {
                SourceImage image;
                if(!loadSourceImage(root, source, image)) {
                    spdlog::warn("  skip {}: raw icon/pixel unavailable", source);
                    ++skipped;
                    continue;
                }
                const fs::path pngPath = outputDir / fs::path(source + ".png");
                savePng(pngPath, *makeBitmap(image));
                manifest << source << '\t'
                         << fs::relative(pngPath, outputDir).generic_string()
                         << '\t' << image.width << '\t' << image.height << '\t'
                         << image.originX << '\t' << image.originY << '\t'
                         << (image.bgra ? 1 : 0) << '\n';
                ++exported;
            }
            spdlog::info("Exported {} source images from {} ({} skipped)",
                         exported, inputPath.filename().string(), skipped);
        } catch(const std::exception &error) {
            spdlog::error("Error processing {}: {}", inputPath.string(),
                          error.what());
            ++failedFiles;
        }
    }
    return failedFiles == 0 ? 0 : 2;
}
