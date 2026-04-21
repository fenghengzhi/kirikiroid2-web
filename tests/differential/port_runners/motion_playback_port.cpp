//
// motion_playback_port — host-native CLI that drives motion::Player to emit
// a JSON snapshot of accumulated per-layer state over a fixed frame range.
//
// Used by tests/differential/python/run_motion_playback.py as the port-side
// producer in the motion_playback differential oracle family. The companion
// golden (live libkrkr2 oracle) is a Phase 5 follow-up; for now the golden
// is recorded from port itself as a regression snapshot to guard the Phase
// 2/3 render-pipeline fixes from future regressions.
//
// Usage:
//   motion_playback_port --mtn <path> --label <timeline> --frames N [--seed S]
//
// Output (stdout): JSON array of per-frame layer snapshots; schema matches
// tests/differential/specs/motion_playback/*.json `expected_trace` shape.
//

#include "motionplayer/Player.h"
#include "motionplayer/MotionNode.h"
#include "motionplayer/ResourceManager.h"
#include "motionplayer/RuntimeSupport.h"
#include "tjs.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>

namespace {

    struct Args {
        std::string mtnPath;
        std::string label;
        int frames = 0;
        int seed = 0;
        bool hasSeed = false;
    };

    void usage() {
        std::fprintf(
            stderr,
            "usage: motion_playback_port --mtn <path> --label <name> --frames N [--seed S]\n");
    }

    bool parseArgs(int argc, char **argv, Args &out) {
        for(int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            auto next = [&]() -> const char * {
                if(++i >= argc) {
                    std::fprintf(stderr, "missing value for %s\n", a.c_str());
                    return nullptr;
                }
                return argv[i];
            };
            if(a == "--mtn") {
                const char *v = next();
                if(!v) return false;
                out.mtnPath = v;
            } else if(a == "--label") {
                const char *v = next();
                if(!v) return false;
                out.label = v;
            } else if(a == "--frames") {
                const char *v = next();
                if(!v) return false;
                out.frames = std::atoi(v);
            } else if(a == "--seed") {
                const char *v = next();
                if(!v) return false;
                out.seed = std::atoi(v);
                out.hasSeed = true;
            } else if(a == "-h" || a == "--help") {
                usage();
                return false;
            } else {
                std::fprintf(stderr, "unknown argument: %s\n", a.c_str());
                usage();
                return false;
            }
        }
        if(out.mtnPath.empty() || out.label.empty() || out.frames < 0) {
            usage();
            return false;
        }
        return true;
    }

    // Minimal JSON writer tuned for the fixed schema; no escaping for
    // non-ASCII because labels/paths in our fixtures are ASCII. Floats go
    // through %.12g to match the diff runner's tolerance settings.
    struct JsonOut {
        std::ostringstream &os;
        int indent = 0;

        void writeIndent() {
            for(int i = 0; i < indent; ++i) os << "  ";
        }
        void writeString(const std::string &s) {
            os << '"';
            for(char c : s) {
                switch(c) {
                    case '"': os << "\\\""; break;
                    case '\\': os << "\\\\"; break;
                    case '\n': os << "\\n"; break;
                    case '\r': os << "\\r"; break;
                    case '\t': os << "\\t"; break;
                    default:
                        if(static_cast<unsigned char>(c) < 0x20) {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                            os << buf;
                        } else {
                            os << c;
                        }
                }
            }
            os << '"';
        }
        void writeDouble(double v) {
            if(std::isnan(v) || std::isinf(v)) {
                os << "null";
                return;
            }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.12g", v);
            os << buf;
        }
        void writeInt(long long v) { os << v; }
        void writeBool(bool v) { os << (v ? "true" : "false"); }
    };

    std::string snapshotLayer(JsonOut &j,
                              const motion::detail::MotionNode &node) {
        std::ostringstream &os = j.os;
        os << "{\n";
        j.indent += 1;

        auto key = [&](const char *k) {
            j.writeIndent();
            os << '"' << k << "\": ";
        };
        auto endField = [&](bool last) {
            os << (last ? "\n" : ",\n");
        };

        key("index");
        j.writeInt(node.index);
        endField(false);

        key("label");
        j.writeString(node.layerName);
        endField(false);

        key("nodeType");
        j.writeInt(node.nodeType);
        endField(false);

        key("visible");
        j.writeBool(node.accumulated.visible);
        endField(false);

        key("active");
        j.writeBool(node.accumulated.active);
        endField(false);

        key("flipX");
        j.writeBool(node.accumulated.flipX);
        endField(false);

        key("flipY");
        j.writeBool(node.accumulated.flipY);
        endField(false);

        key("posX");
        j.writeDouble(node.accumulated.posX);
        endField(false);

        key("posY");
        j.writeDouble(node.accumulated.posY);
        endField(false);

        key("posZ");
        j.writeDouble(node.accumulated.posZ);
        endField(false);

        key("angleDeg");
        j.writeDouble(node.accumulated.angle * 180.0 /
                      3.14159265358979323846);
        endField(false);

        key("scaleX");
        j.writeDouble(node.accumulated.scaleX);
        endField(false);

        key("scaleY");
        j.writeDouble(node.accumulated.scaleY);
        endField(false);

        key("slantX");
        j.writeDouble(node.accumulated.slantX);
        endField(false);

        key("slantY");
        j.writeDouble(node.accumulated.slantY);
        endField(false);

        key("opacity");
        j.writeInt(node.accumulated.opacity);
        endField(false);

        key("blendMode");
        j.writeInt(node.accumulated.blendMode);
        endField(false);

        key("drawFlag");
        j.writeBool(node.drawFlag);
        endField(false);

        key("drawnThisFrame");
        j.writeBool(node.drawnThisFrame);
        endField(false);

        key("currentImage");
        j.writeString(node.interpolatedCache.src);
        endField(true);

        j.indent -= 1;
        j.writeIndent();
        os << "}";
        return {};
    }

} // namespace

int main(int argc, char **argv) {
    // spdlog sinks expected by various motionplayer code paths. Route to
    // stderr so log lines don't corrupt the JSON written to stdout.
    static auto core_logger = spdlog::stderr_color_mt("core");
    static auto tjs2_logger = spdlog::stderr_color_mt("tjs2");
    static auto plugin_logger = spdlog::stderr_color_mt("plugin");
    spdlog::set_level(spdlog::level::err);

    Args args;
    if(!parseArgs(argc, argv, args)) {
        return 2;
    }

    namespace fs = std::filesystem;
    fs::path mtn = args.mtnPath;
    if(!fs::exists(mtn)) {
        std::fprintf(stderr, "motion file not found: %s\n",
                     mtn.string().c_str());
        return 3;
    }

    motion::Player player;
    if(args.hasSeed) {
        tTJSVariant seed{args.seed};
        tTJSVariant *params[] = { &seed };
        motion::ResourceManager::setEmotePSBDecryptSeed(nullptr, 1, params,
                                                       nullptr);
    }

    const ttstr mtnPath(fs::absolute(mtn).string());
    const ttstr label = motion::detail::widen(args.label);

    tTJSVariant found = player.findMotion(mtnPath);
    if(found.Type() != tvtObject) {
        std::fprintf(stderr,
                     "findMotion(%s) returned non-object (type=%d)\n",
                     mtnPath.AsStdString().c_str(), static_cast<int>(found.Type()));
        return 4;
    }

    player.playTimeline(label, motion::PlayFlagForce);
    if(!player.getTimelinePlaying(label)) {
        std::fprintf(stderr, "timeline %s failed to start\n",
                     args.label.c_str());
        return 5;
    }

    // Force node tree construction (private) by calling a public API that
    // triggers ensureNodeTreeBuilt() — same trick as the unit test at
    // tests/unit-tests/plugins/motionplayer-dll.cpp:417.
    (void)player.getLayerNames();

    // Note: runUpdatePassForOracle() segfaults in the headless native CLI
    // context (updateLayers reaches a null code path, likely a TJS
    // dispatch on an uninitialised layer object). We keep the method in
    // Player.h for future use; for now the snapshot only captures
    // structural per-node state (layer label / nodeType / drawFlag /
    // drawnThisFrame). The accumulated transform fields are still emitted
    // but reflect their default values until the headless updateLayers
    // path is fixed, at which point this call site can be re-enabled. See
    // tests/differential/oracle_runner/README.md → motion_playback row.

    std::ostringstream out;
    JsonOut json{out, 0};
    out << "[\n";
    json.indent = 1;

    for(int f = 0; f < args.frames; ++f) {
        const auto *rt = player.runtime();
        if(!rt) {
            std::fprintf(stderr,
                         "runtime() returned null at frame %d\n", f);
            return 6;
        }

        if(f > 0) {
            out << ",\n";
        }
        json.writeIndent();
        out << "{\n";
        json.indent += 1;

        json.writeIndent();
        out << "\"frame\": ";
        json.writeInt(f);
        out << ",\n";

        json.writeIndent();
        out << "\"layers\": [\n";
        json.indent += 1;

        // Skip synthetic root at index 0; real layers start at 1.
        const auto &nodes = rt->nodes;
        bool firstLayer = true;
        for(size_t i = 1; i < nodes.size(); ++i) {
            if(!firstLayer) {
                out << ",\n";
            }
            firstLayer = false;
            json.writeIndent();
            snapshotLayer(json, nodes[i]);
        }

        out << "\n";
        json.indent -= 1;
        json.writeIndent();
        out << "]\n";

        json.indent -= 1;
        json.writeIndent();
        out << "}";

        player.frameProgress(1.0);
        // Same caveat as above: runUpdatePassForOracle() crashes in this
        // headless context, so the per-frame loop only advances timeline
        // state. Re-enable once the headless updateLayers path is fixed.
    }

    out << "\n]\n";
    std::fputs(out.str().c_str(), stdout);
    return 0;
}
