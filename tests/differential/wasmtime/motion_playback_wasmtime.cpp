// Wasmtime-only Motion playback differential glue.
//
// This file deliberately stays below the engine/platform boundary: it owns the
// exported test ABI, error buffer, framebuffer buffer, and MotionTraceWeb
// linkage symbols. Browser, Cocos, Window, FS, thread, event behavior, and
// differential trace collection must come from the normal engine sources plus
// host-provided env/WASI imports and LLDB guest inspection.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>

#include "Application.h"
#include "MainScene.h"
#include "tjsError.h"
#include "motionplayer/MotionNode.h"
#include "motionplayer/MotionTraceWeb.h"
#include "motionplayer/Player.h"
#include "motionplayer/RuntimeSupport.h"

void setError(const std::string &message);

namespace {

std::string g_error;
std::string g_stage;
std::vector<unsigned char> g_framebuffer;
int g_framebuffer_width = 0;
int g_framebuffer_height = 0;
int g_framebuffer_pitch = 0;
int g_framebuffer_format = 0;
int g_framebuffer_frame_no = 0;

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

extern "C" __attribute__((noinline, used))
void krkr2_lldb_motion_frame_begin(std::int32_t frameId,
                                   const void *objthis,
                                   const motion::Player *topPlayer,
                                   std::int32_t playerCount) {
    (void)frameId;
    (void)objthis;
    (void)topPlayer;
    (void)playerCount;
}

extern "C" __attribute__((noinline, used))
void krkr2_lldb_motion_layer_sample(std::int32_t frameId,
                                    std::int32_t index,
                                    std::uint64_t nodeFlags,
                                    std::uint64_t opacityBlend,
                                    double posX,
                                    double posY,
                                    double posZ,
                                    double angleDeg,
                                    double scaleX,
                                    double scaleY,
                                    double slantX,
                                    double slantY) {
    (void)frameId;
    (void)index;
    (void)nodeFlags;
    (void)opacityBlend;
    (void)posX;
    (void)posY;
    (void)posZ;
    (void)angleDeg;
    (void)scaleX;
    (void)scaleY;
    (void)slantX;
    (void)slantY;
}

extern "C" __attribute__((noinline, used))
void krkr2_lldb_motion_frame_end(std::int32_t frameId) {
    (void)frameId;
}

void emitLayerSample(int frameId,
                     int flatIndex,
                     const motion::detail::MotionNode &node) {
    const auto &accum = node.accumulated;
    const std::uint64_t opacityBlend =
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(accum.opacity)) << 32) |
        static_cast<std::uint32_t>(node.stencilType);
    int flags = 0;
    if(accum.visible) flags |= 1 << 0;
    if(accum.active) flags |= 1 << 1;
    if(accum.flipX) flags |= 1 << 2;
    if(accum.flipY) flags |= 1 << 3;
    const std::uint64_t nodeFlags =
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(node.nodeType)) << 32) |
        static_cast<std::uint32_t>(flags);
    krkr2_lldb_motion_layer_sample(
        frameId, flatIndex, nodeFlags, opacityBlend, accum.posX, accum.posY,
        accum.posZ, accum.angle, accum.scaleX, accum.scaleY, accum.slantX,
        accum.slantY);
}

void emitPlayerLayers(int frameId, int &flatIndex, motion::Player *player) {
    if(!player) return;
    const auto *runtime = player->runtime();
    if(!runtime) return;
    for(const auto &node : runtime->nodes) {
        emitLayerSample(frameId, flatIndex++, node);
    }
}

void emitProgressSample(motion::Player *fallbackPlayer) {
    auto &state = traceState();
    const int frameId = state.frameCounter++;
    motion::Player *topPlayer =
        !state.players.empty() ? state.players.front() : fallbackPlayer;
    krkr2_lldb_motion_frame_begin(
        frameId, state.objthis, topPlayer,
        static_cast<std::int32_t>(state.players.size()));

    int flatIndex = 0;
    for(size_t i = 1; i < state.players.size(); ++i) {
        emitPlayerLayers(frameId, flatIndex, state.players[i]);
    }
    if(!state.players.empty()) {
        emitPlayerLayers(frameId, flatIndex, state.players.front());
    } else {
        emitPlayerLayers(frameId, flatIndex, fallbackPlayer);
    }
    krkr2_lldb_motion_frame_end(frameId);
}

template <typename Fn>
int runWithErrors(Fn &&fn) {
    try {
        fn();
        return 1;
    } catch(const TJS::eTJSScriptError &e) {
        std::string msg = ttstr(e.GetMessage()).AsStdString();
        if(e.GetBlockName()) {
            msg += " at ";
            msg += ttstr(e.GetBlockName()).AsStdString();
            msg += ":";
            msg += std::to_string(e.GetSourceLine());
        }
        msg += " pos ";
        msg += std::to_string(e.GetPosition());
        const auto trace = ttstr(e.GetTrace()).AsStdString();
        if(!trace.empty()) {
            msg += "\n";
            msg += trace;
        }
        setError(msg);
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
    return 0;
}

} // namespace

void resetState() {
    g_error.clear();
    g_stage.clear();
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
    emitProgressSample(_player);
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

int wasmtimeStartupFrom(const char *path, int len) {
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

int wasmtimeGetErrorPtr() {
    return static_cast<int>(reinterpret_cast<uintptr_t>(g_error.c_str()));
}

int wasmtimeGetErrorLen() {
    return static_cast<int>(g_error.size());
}
