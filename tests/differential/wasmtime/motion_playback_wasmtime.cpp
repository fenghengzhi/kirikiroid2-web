// Wasmtime-only Motion playback differential glue.
//
// This file deliberately stays below the engine/platform boundary: it owns the
// exported test ABI, error/trace buffers, and MotionTraceWeb symbols. Browser,
// Cocos, Window, FS, thread, and event behavior must come from the normal
// engine sources plus host-provided env/WASI imports.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>

#include "Application.h"
#include "MainScene.h"
#include "tjsError.h"
#include "motionplayer/MotionTraceWeb.h"
#include "motionplayer/MotionNode.h"
#include "motionplayer/Player.h"
#include "motionplayer/RuntimeSupport.h"

void setError(const std::string &message);

namespace {

std::string g_trace_json = "[]";
std::string g_error;
std::string g_stage;
std::vector<std::string> g_frame_json;
std::vector<unsigned char> g_framebuffer;
int g_framebuffer_width = 0;
int g_framebuffer_height = 0;
int g_framebuffer_pitch = 0;
int g_framebuffer_format = 0;
int g_framebuffer_frame_no = 0;

std::string jsonEscape(const std::string &in) {
    std::string out;
    out.reserve(in.size() + 2);
    for(char c : in) {
        switch(c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if(static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

void writeJsonString(std::ostringstream &out, const std::string &value) {
    out << '"' << jsonEscape(value) << '"';
}

void writeJsonDouble(std::ostringstream &out, double value) {
    if(std::isnan(value) || std::isinf(value)) {
        out << "null";
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.12g", value);
    out << buf;
}

std::string ptrString(const void *ptr) {
    if(!ptr) return {};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%p", ptr);
    return buf;
}

void appendNodeJson(std::ostringstream &out,
                    const motion::detail::MotionNode &node,
                    int flatIndex) {
    const auto &accum = node.accumulated;
    out << "{";
    out << "\"index\":" << flatIndex;
    out << ",\"label\":";
    writeJsonString(out, node.layerName);
    out << ",\"nodeType\":" << node.nodeType;
    out << ",\"visible\":" << (accum.visible ? "true" : "false");
    out << ",\"active\":" << (accum.active ? "true" : "false");
    out << ",\"flipX\":" << (accum.flipX ? "true" : "false");
    out << ",\"flipY\":" << (accum.flipY ? "true" : "false");
    out << ",\"posX\":";
    writeJsonDouble(out, accum.posX);
    out << ",\"posY\":";
    writeJsonDouble(out, accum.posY);
    out << ",\"posZ\":";
    writeJsonDouble(out, accum.posZ);
    out << ",\"angleDeg\":";
    writeJsonDouble(out, accum.angle);
    out << ",\"scaleX\":";
    writeJsonDouble(out, accum.scaleX);
    out << ",\"scaleY\":";
    writeJsonDouble(out, accum.scaleY);
    out << ",\"slantX\":";
    writeJsonDouble(out, accum.slantX);
    out << ",\"slantY\":";
    writeJsonDouble(out, accum.slantY);
    out << ",\"opacity\":" << accum.opacity;
    out << ",\"blendMode\":" << node.stencilType;
    out << ",\"drawFlag\":" << (node.drawFlag ? "true" : "false");
    out << ",\"drawnThisFrame\":"
        << (node.drawnThisFrame ? "true" : "false");
    out << ",\"currentImage\":";
    writeJsonString(out, node.interpolatedCache.src);
    out << "}";
}

void rebuildTraceJson() {
    std::ostringstream out;
    out << "[";
    for(size_t i = 0; i < g_frame_json.size(); ++i) {
        if(i) out << ",";
        out << g_frame_json[i];
    }
    out << "]";
    g_trace_json = out.str();
}

struct TraceState {
    bool inProgress = false;
    void *objthis = nullptr;
    std::vector<motion::Player *> players;
    int frameCounter = 0;
};

TraceState &traceState() {
    static TraceState state;
    return state;
}

void emitProgressFrame(motion::Player *fallbackPlayer) {
    auto &state = traceState();
    std::ostringstream out;
    out << "{";
    out << "\"frameId\":" << state.frameCounter++;
    out << ",\"objthis\":";
    if(state.objthis) {
        writeJsonString(out, ptrString(state.objthis));
    } else {
        out << "null";
    }
    out << ",\"topPlayer\":";
    motion::Player *topPlayer =
        !state.players.empty() ? state.players.front() : fallbackPlayer;
    if(topPlayer) {
        writeJsonString(out, ptrString(topPlayer));
    } else {
        out << "null";
    }
    out << ",\"playerCount\":" << state.players.size();
    out << ",\"layout\":\"wasmtime-runtime\"";
    out << ",\"layers\":[";

    int flatIndex = 0;
    bool first = true;
    auto appendPlayer = [&](motion::Player *player) {
        if(!player) return;
        const auto *runtime = player->runtime();
        if(!runtime) return;
        for(const auto &node : runtime->nodes) {
            if(!first) out << ",";
            first = false;
            appendNodeJson(out, node, flatIndex++);
        }
    };

    for(size_t i = 1; i < state.players.size(); ++i) {
        appendPlayer(state.players[i]);
    }
    if(!state.players.empty()) {
        appendPlayer(state.players.front());
    } else {
        appendPlayer(fallbackPlayer);
    }

    out << "]}";
    g_frame_json.push_back(out.str());
}

template <typename Fn>
int runWithErrors(Fn &&fn) {
    try {
        fn();
        rebuildTraceJson();
        return 1;
    } catch(const TJS::eTJSScriptError &e) {
        std::ostringstream msg;
        msg << ttstr(e.GetMessage()).AsStdString();
        if(e.GetBlockName()) {
            msg << " at " << ttstr(e.GetBlockName()).AsStdString()
                << ":" << e.GetSourceLine();
        }
        msg << " pos " << e.GetPosition();
        const auto trace = ttstr(e.GetTrace()).AsStdString();
        if(!trace.empty()) msg << "\n" << trace;
        setError(msg.str());
    } catch(const TJS::eTJS &e) {
        setError(ttstr(e.GetMessage()).AsStdString());
    } catch(const std::exception &e) {
        setError(e.what());
    } catch(const tjs_char *e) {
        setError(ttstr(e).AsStdString());
    } catch(const char *e) {
        setError(e);
    } catch(...) {
        setError("unknown C++ exception");
    }
    rebuildTraceJson();
    return 0;
}

} // namespace

void resetState() {
    g_error.clear();
    g_stage.clear();
    g_trace_json = "[]";
    g_frame_json.clear();
    g_framebuffer.clear();
    g_framebuffer_width = 0;
    g_framebuffer_height = 0;
    g_framebuffer_pitch = 0;
    g_framebuffer_format = 0;
    g_framebuffer_frame_no = 0;
    auto &state = traceState();
    state.inProgress = false;
    state.objthis = nullptr;
    state.players.clear();
    state.frameCounter = 0;
}

void setError(const std::string &message) {
    if(g_stage.empty()) {
        g_error = message;
    } else {
        g_error = g_stage + ": " + message;
    }
}

void setStage(const char *stage) {
    g_stage = stage ? stage : "";
}

void setStageString(const std::string &stage) {
    g_stage = stage;
}

void TVPWasmtimeTickMainScene(float) {
    auto *app = cocos2d::Application::getInstance();
    if(app) {
        app->mainLoop();
    }
}

namespace motion::detail {

MotionTraceProgressScope::MotionTraceProgressScope(Player *player,
                                                   void *objthis) :
    _player(player) {
    auto &state = traceState();
    state.inProgress = true;
    state.objthis = objthis;
    state.players.clear();
}

MotionTraceProgressScope::~MotionTraceProgressScope() {
    auto &state = traceState();
    if(!state.inProgress) return;
    emitProgressFrame(_player);
    state.inProgress = false;
    state.objthis = nullptr;
    state.players.clear();
}

void motionTraceRecordUpdatePlayer(Player *player) {
    auto &state = traceState();
    if(!state.inProgress || !player) return;
    state.players.push_back(player);
}

} // namespace motion::detail

extern "C" {

EMSCRIPTEN_KEEPALIVE
int mp_write_file(const char *path, int pathLen, const void *data, int dataLen) {
    if(!path || pathLen <= 0 || dataLen < 0 || (!data && dataLen > 0)) {
        setError("mp_write_file: invalid argument");
        return 0;
    }

    const std::string filePath(path, static_cast<size_t>(pathLen));
    FILE *f = std::fopen(filePath.c_str(), "wb");
    if(!f) {
        setError("mp_write_file: fopen failed for " + filePath);
        return 0;
    }
    const bool ok =
        dataLen == 0 ||
        std::fwrite(data, 1, static_cast<size_t>(dataLen), f) ==
            static_cast<size_t>(dataLen);
    std::fclose(f);
    if(!ok) {
        setError("mp_write_file: fwrite failed for " + filePath);
        return 0;
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int mp_startup_from(const char *path, int len) {
    if(!path || len <= 0) {
        setError("empty xp3 path");
        return 0;
    }

    const std::string xp3Path(path, static_cast<size_t>(len));
    return runWithErrors([&]() {
        setStage("TVPMainScene::startupFrom");
        auto *scene = TVPMainScene::GetInstance();
        if(!scene)
            scene = TVPMainScene::CreateInstance();
        if(!scene)
            throw std::runtime_error("TVPMainScene is unavailable");
        if(!scene->startupFrom(xp3Path)) {
            throw std::runtime_error("TVPMainScene::startupFrom returned false");
        }
        setStage("");
    });
}

EMSCRIPTEN_KEEPALIVE
int mp_get_trace_ptr() {
    return static_cast<int>(reinterpret_cast<uintptr_t>(g_trace_json.c_str()));
}

EMSCRIPTEN_KEEPALIVE
int mp_get_trace_len() {
    return static_cast<int>(g_trace_json.size());
}

EMSCRIPTEN_KEEPALIVE
int mp_get_error_ptr() {
    return static_cast<int>(reinterpret_cast<uintptr_t>(g_error.c_str()));
}

EMSCRIPTEN_KEEPALIVE
int mp_get_error_len() {
    return static_cast<int>(g_error.size());
}

} // extern "C"
