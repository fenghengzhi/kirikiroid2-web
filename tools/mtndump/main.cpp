#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <argparse/argparse.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "BinaryStream.h"
#include "StorageIntf.h"
#include "SysInitImpl.h"
#include "SysInitIntf.h"
#include "GraphicsLoaderIntf.h"
#include "motionplayer/OfflineMotionSnapshot.h"

namespace fs = std::filesystem;

namespace {

    std::string normalizePath(const std::string &path) {
        if(path.empty()) {
            return path;
        }

        fs::path p(path);
        if(path[0] == '~') {
#if defined(_WIN32)
            const char *home = std::getenv("USERPROFILE");
#else
            const char *home = std::getenv("HOME");
#endif
            if(home) {
                p = fs::path(home) / path.substr(1);
            }
        }

        try {
            return fs::weakly_canonical(p).string();
        } catch(...) {
            return fs::absolute(p).string();
        }
    }

    bool startsWith(const std::string &value, const char *prefix) {
        return value.rfind(prefix, 0) == 0;
    }

    std::set<std::string> collectSourceNames(
        const motion::detail::MotionSnapshot &snapshot) {
        std::set<std::string> sources;
        constexpr const char *sourcePrefix = "source/";
        constexpr const char *iconMarker = "/icon/";
        constexpr const char *pixelSuffix = "/pixel";

        for(const auto &[resourcePath, resource] : snapshot.resourcesByPath) {
            if(!resource || !startsWith(resourcePath, sourcePrefix) ||
               resourcePath.size() <= std::strlen(pixelSuffix) ||
               resourcePath.compare(resourcePath.size() -
                                        std::strlen(pixelSuffix),
                                    std::strlen(pixelSuffix), pixelSuffix) != 0) {
                continue;
            }

            const auto iconPos = resourcePath.find(iconMarker,
                                                    std::strlen(sourcePrefix));
            if(iconPos == std::string::npos) {
                continue;
            }
            const auto group = resourcePath.substr(
                std::strlen(sourcePrefix),
                iconPos - std::strlen(sourcePrefix));
            const auto nameBegin = iconPos + std::strlen(iconMarker);
            const auto name = resourcePath.substr(
                nameBegin,
                resourcePath.size() - std::strlen(pixelSuffix) - nameBegin);
            if(!group.empty() && !name.empty()) {
                sources.emplace("src/" + group + "/" + name);
            }
        }
        return sources;
    }

    std::shared_ptr<tTVPBaseBitmap> makeBitmapFromDecodedRgba(
        const std::vector<std::uint8_t> &pixelData, int width, int height,
        bool decodedPixelsAreBgra) {
        const size_t rowBytes = static_cast<size_t>(width) * 4u;
        const size_t requiredSize = static_cast<size_t>(height) * rowBytes;
        if(pixelData.size() < requiredSize) {
            throw std::runtime_error(
                "decoded pixel buffer too small: got " +
                std::to_string(pixelData.size()) + " bytes, expected " +
                std::to_string(requiredSize));
        }

        auto bmp = std::make_shared<tTVPBaseBitmap>(
            static_cast<tjs_uint>(width),
            static_cast<tjs_uint>(height), 32);

        for(int y = 0; y < height; ++y) {
            auto *row = static_cast<std::uint8_t *>(
                bmp->GetScanLineForWrite(static_cast<tjs_uint>(y)));
            const auto *src =
                pixelData.data() + static_cast<size_t>(y) * rowBytes;
            if(decodedPixelsAreBgra) {
                std::memcpy(row, src, rowBytes);
            } else {
                for(int x = 0; x < width; ++x) {
                    const auto *s = src + static_cast<size_t>(x) * 4u;
                    auto *dst = row + static_cast<size_t>(x) * 4u;
                    dst[0] = s[2];
                    dst[1] = s[1];
                    dst[2] = s[0];
                    dst[3] = s[3];
                }
            }
        }

        return bmp;
    }

    void saveBitmapAsPng(const fs::path &path, const tTVPBaseBitmap &bitmap) {
        fs::create_directories(path.parent_path());
        std::unique_ptr<tTJSBinaryStream> stream{
            TVPCreateBinaryStreamForWrite(ttstr(path.string()), TJS_W(""))
        };
        if(!stream) {
            throw std::runtime_error("failed to create output stream: " +
                                     path.string());
        }
        TVPSaveAsPNG(nullptr, stream.get(), &bitmap, TJS_W("png"), nullptr);
    }

    class ToolRuntimeScope {
    public:
        ToolRuntimeScope() {
            const auto cwd = fs::current_path().string();
            TVPNativeProjectDir = ttstr(cwd);
            TVPProjectDir = TVPNormalizeStorageName(TVPNativeProjectDir);
            TVPInitScriptEngine();
            TVPInitializeBaseSystems();
            TVPSystemInit();
        }

        ~ToolRuntimeScope() = default;
    };

} // namespace

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program(PROGRAM_NAME, VERSION);
    program.add_argument("files")
        .help("input .mtn/.psb motion file(s)")
        .nargs(argparse::nargs_pattern::at_least_one);
    program.add_argument("-o", "--output")
        .help("output directory")
        .default_value(std::string("./"));
    program.add_argument("-s", "--seed")
        .help("decrypt seed for encrypted PSB files (0 = plain, default: 0)")
        .default_value(0)
        .scan<'i', int>();

    try {
        program.parse_args(argc, argv);
    } catch(const std::exception &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    auto logger = spdlog::stdout_color_mt("mtndump");
    spdlog::set_default_logger(logger);
    spdlog::stdout_color_mt("core");
    spdlog::stdout_color_mt("tjs2");
    spdlog::stdout_color_mt("plugin");
    spdlog::set_pattern("%^%v%$");

    ToolRuntimeScope runtimeScope;

    const fs::path outputRoot =
        fs::path(normalizePath(program.get<std::string>("--output")));
    const auto inputFiles = program.get<std::vector<std::string>>("files");
    const tjs_int decryptSeed =
        static_cast<tjs_int>(program.get<int>("--seed"));

    int failedFiles = 0;
    for(const auto &rawInput : inputFiles) {
        const fs::path inputPath(normalizePath(rawInput));
        if(!fs::exists(inputPath) || fs::is_directory(inputPath)) {
            spdlog::error("Skipping invalid file: {}", rawInput);
            ++failedFiles;
            continue;
        }

        try {
            auto snapshot = motion::detail::loadMotionSnapshot(
                ttstr(inputPath.string()), decryptSeed);
            if(!snapshot) {
                spdlog::error("Failed to load motion snapshot: {} "
                              "(wrong seed for encrypted file?)",
                              inputPath.string());
                ++failedFiles;
                continue;
            }

            const fs::path motionOutDir = outputRoot / inputPath.stem();
            fs::create_directories(motionOutDir);
            std::ofstream manifest(motionOutDir / "manifest.tsv");
            if(!manifest.is_open()) {
                spdlog::error("Failed to open manifest for write: {}",
                              (motionOutDir / "manifest.tsv").string());
                ++failedFiles;
                continue;
            }
            manifest << "source\tpng\twidth\theight\torigin_x\torigin_y\t"
                        "decoded_bgra\n";

            const auto uniqueSources = collectSourceNames(*snapshot);
            std::size_t exported = 0;
            std::size_t skipped = 0;
            for(const auto &source : uniqueSources) {
                if(!startsWith(source, "src/")) {
                    continue;
                }

                int width = 0;
                int height = 0;
                double originX = 0.0;
                double originY = 0.0;
                bool decodedIsBgra = false;
                std::vector<std::uint8_t> decodedPixels;
                const auto *resource =
                    motion::internal::findPSBResourceBySourceName(
                        *snapshot, source, width, height, decodedPixels,
                        originX, originY, &decodedIsBgra);
                if(!resource) {
                    spdlog::warn("  skip {}: icon node not found in PSB tree",
                                 source);
                    ++skipped;
                    continue;
                }
                if(width <= 0 || height <= 0) {
                    spdlog::warn("  skip {}: invalid dimensions {}x{}",
                                 source, width, height);
                    ++skipped;
                    continue;
                }
                if(decodedPixels.empty()) {
                    spdlog::warn("  skip {}: decoded pixel buffer empty",
                                 source);
                    ++skipped;
                    continue;
                }

                try {
                    auto bitmap = makeBitmapFromDecodedRgba(
                        decodedPixels, width, height, decodedIsBgra);
                    fs::path pngPath =
                        motionOutDir / fs::path(source + ".png");
                    saveBitmapAsPng(pngPath, *bitmap);
                    manifest << source << '\t'
                             << fs::relative(pngPath, motionOutDir)
                                    .generic_string()
                             << '\t' << width << '\t' << height << '\t'
                             << originX << '\t' << originY << '\t'
                             << (decodedIsBgra ? 1 : 0) << '\n';
                    ++exported;
                } catch(const std::exception &err) {
                    spdlog::warn("  skip {}: {}", source, err.what());
                    ++skipped;
                }
            }

            spdlog::info("Exported {} source images from {} ({} skipped)",
                         exported, inputPath.filename().string(), skipped);
        } catch(const std::exception &err) {
            spdlog::error("Error processing {}: {}", inputPath.string(),
                          err.what());
            ++failedFiles;
        } catch(...) {
            spdlog::error("Unknown error processing {}", inputPath.string());
            ++failedFiles;
        }
    }

    return failedFiles == 0 ? 0 : 2;
}
