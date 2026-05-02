// Wasmtime-only Motion playback differential glue.
//
// This file deliberately stays below the engine/platform boundary: it owns the
// exported test ABI, error buffer, framebuffer buffer, and MotionTraceWeb
// linkage symbols. Browser, Cocos, Window, FS, thread, event behavior, and
// differential trace collection must come from the normal engine sources plus
// host-provided env/WASI imports and LLDB guest inspection.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <iomanip>
#include <sstream>
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
std::string g_render_probe_jsonl;
int g_render_probe_seq = 0;

struct TraceState {
    bool inProgress = false;
    bool inRender = false;
    void *objthis = nullptr;
    std::vector<motion::Player *> players;
    int frameCounter = 0;
    int lastCompletedFrameId = -1;
    motion::Player *lastCompletedTopPlayer = nullptr;
    int currentRenderFrameId = -1;
    motion::Player *currentRenderPlayer = nullptr;
};

TraceState &traceState() {
    static TraceState state;
    return state;
}

std::string ptrHex(const void *ptr) {
    if(!ptr) return "null";
    std::ostringstream os;
    os << "\"0x" << std::hex
       << static_cast<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(ptr))
       << "\"";
    return os.str();
}

void appendJsonString(std::string &out, const std::string &value) {
    out.push_back('"');
    for(char ch : value) {
        switch(ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if(static_cast<unsigned char>(ch) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(ch));
                    out += buf;
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    out.push_back('"');
}

template <typename T, size_t N>
void appendNumberArray(std::string &out, const std::array<T, N> &values) {
    out.push_back('[');
    for(size_t i = 0; i < N; ++i) {
        if(i) out.push_back(',');
        std::ostringstream os;
        os << std::setprecision(9) << values[i];
        out += os.str();
    }
    out.push_back(']');
}

int renderFrameIdFor(motion::Player *player) {
    auto &state = traceState();
    if(state.inRender && state.currentRenderFrameId >= 0) {
        return state.currentRenderFrameId;
    }
    if(state.lastCompletedFrameId < 0) {
        return -1;
    }
    if(!state.lastCompletedTopPlayer || !player ||
       state.lastCompletedTopPlayer == player) {
        return state.lastCompletedFrameId;
    }
    return -1;
}

motion::Player *renderPlayerFor(motion::Player *player) {
    auto &state = traceState();
    if(player) return player;
    if(state.currentRenderPlayer) return state.currentRenderPlayer;
    return state.lastCompletedTopPlayer;
}

void appendRenderEvent(motion::Player *player,
                       const char *stage,
                       const char *kind,
                       const char *samplePoint,
                       const std::string &payload,
                       const std::string &diagnostics) {
    const int frameId = renderFrameIdFor(player);
    if(frameId < 0) return;
    motion::Player *eventPlayer = renderPlayerFor(player);
    std::string ev;
    ev.reserve(payload.size() + diagnostics.size() + 256);
    ev += "{\"schema\":\"motion-render-stage-wasmtime-v1-event\"";
    ev += ",\"source\":\"wasmtime-port-render-stage\"";
    ev += ",\"stage\":\"";
    ev += stage;
    ev += "\",\"kind\":\"";
    ev += kind;
    ev += "\",\"samplePoint\":\"";
    ev += samplePoint;
    ev += "\",\"frameId\":";
    ev += std::to_string(frameId);
    ev += ",\"player\":";
    ev += ptrHex(eventPlayer);
    ev += ",\"seq\":";
    ev += std::to_string(g_render_probe_seq++);
    if(!payload.empty()) {
        ev.push_back(',');
        ev += payload;
    }
    ev += ",\"diagnostics\":";
    ev += diagnostics.empty() ? "{}" : diagnostics;
    ev += "}\n";
    g_render_probe_jsonl += ev;
}

std::string activeMotionPath(const motion::Player *player) {
    if(!player || !player->runtime() || !player->runtime()->activeMotion) {
        return {};
    }
    return player->runtime()->activeMotion->path;
}

int preparedIndexFor(const motion::detail::PlayerRuntime *runtime,
                     const motion::detail::PlayerRuntime::PreparedRenderItem *item) {
    if(!runtime || !item || runtime->preparedRenderItems.empty()) return -1;
    const auto *base = runtime->preparedRenderItems.data();
    const auto *end = base + runtime->preparedRenderItems.size();
    if(item < base || item >= end) return -1;
    return static_cast<int>(item - base);
}

int commandIndexFor(const motion::detail::PlayerRuntime *runtime,
                    const motion::detail::PlayerRuntime::RenderCommand *command) {
    if(!runtime || !command || runtime->renderCommands.empty()) return -1;
    const auto *base = runtime->renderCommands.data();
    const auto *end = base + runtime->renderCommands.size();
    if(command < base || command >= end) return -1;
    return static_cast<int>(command - base);
}

void appendPreparedItemJson(
    std::string &out,
    const motion::detail::PlayerRuntime *runtime,
    const motion::detail::PlayerRuntime::PreparedRenderItem &item,
    size_t index) {
    if(index) out.push_back(',');
    out += "{\"index\":";
    out += std::to_string(index);
    out += ",\"nodeIndex\":";
    out += std::to_string(item.nodeIndex);
    out += ",\"sourceKey\":";
    appendJsonString(out, item.sourceKey);
    out += ",\"flags\":{\"flag16\":";
    out += item.rawFlag16 ? "1" : "0";
    out += ",\"flag17\":";
    out += item.skipFlag0 ? "1" : "0";
    out += ",\"flag18\":";
    out += item.skipFlag1 ? "1" : "0";
    out += ",\"drawFlag19\":";
    out += item.drawFlag ? "1" : "0";
    out += ",\"layerResolved20\":";
    out += item.clipFlag ? "1" : "0";
    out += ",\"clipValid21\":";
    out += item.hasViewport ? "1" : "0";
    out += "},\"layerIds\":{\"primary\":";
    out += std::to_string(item.layerId);
    out += ",\"secondary\":";
    out += std::to_string(item.layerId2);
    out += "},\"paintBox\":";
    appendNumberArray(out, item.paintBox);
    out += ",\"viewportRect\":";
    appendNumberArray(out, item.viewport);
    out += ",\"sourceGate232\":";
    out += item.hasOwnSource ? "1" : "0";
    out += ",\"stencilType244\":";
    out += std::to_string(item.stencilComposite);
    out += ",\"parentItemIndex\":";
    out += std::to_string(preparedIndexFor(runtime, item.parentItem));
    out += ",\"childItemCount\":";
    out += std::to_string(item.childItems.size());
    out += ",\"meshType280\":";
    out += std::to_string(item.meshType);
    out += "}";
}

void appendPreparedItemsPayload(std::string &out,
                                const motion::detail::PlayerRuntime *runtime) {
    constexpr size_t kLimit = 256;
    const size_t count = runtime ? runtime->preparedRenderItems.size() : 0;
    out += "\"preparedItemCount\":";
    out += std::to_string(count);
    out += ",\"preparedItems\":[";
    const size_t n = std::min(count, kLimit);
    for(size_t i = 0; i < n; ++i) {
        appendPreparedItemJson(out, runtime, runtime->preparedRenderItems[i], i);
    }
    out.push_back(']');
    if(count > n) {
        out += ",\"preparedItemsTruncated\":";
        out += std::to_string(count - n);
    }
}

void appendRenderCommandJson(
    std::string &out,
    const motion::detail::PlayerRuntime *runtime,
    const motion::detail::PlayerRuntime::RenderCommand &command,
    size_t index) {
    if(index) out.push_back(',');
    out += "{\"index\":";
    out += std::to_string(index);
    out += ",\"nodeIndex\":";
    out += std::to_string(command.nodeIndex);
    out += ",\"sourceKey\":";
    appendJsonString(out, command.sourceKey);
    out += ",\"flags\":{\"flag16\":";
    out += command.rawFlag16 ? "1" : "0";
    out += ",\"flag17\":";
    out += command.rawFlag17 ? "1" : "0";
    out += ",\"flag18\":";
    out += command.rawFlag18 ? "1" : "0";
    out += ",\"drawFlag19\":";
    out += command.rawFlag19 ? "1" : "0";
    out += ",\"layerResolved20\":";
    out += command.rawFlag20 ? "1" : "0";
    out += ",\"clipValid21\":";
    out += command.rawFlag21 ? "1" : "0";
    out += "},\"layerIds\":{\"primary\":";
    out += std::to_string(command.layerId);
    out += ",\"secondary\":";
    out += std::to_string(command.layerId2);
    out += "},\"clipRect\":";
    appendNumberArray(out, command.clipRect);
    out += ",\"dirtyRect\":";
    appendNumberArray(out, command.dirtyRect);
    out += ",\"sourceGate232\":";
    out += command.hasOwnSource ? "1" : "0";
    out += ",\"stencilType244\":";
    out += std::to_string(command.itemFlags);
    out += ",\"parentItemIndex\":";
    out += std::to_string(preparedIndexFor(runtime, command.preparedItem));
    out += ",\"parentCommandIndex\":";
    out += std::to_string(commandIndexFor(runtime, command.parentCommand));
    out += ",\"childCommandCount\":";
    out += std::to_string(command.childCommandPtrs.size());
    out += ",\"meshType280\":";
    out += std::to_string(command.meshType);
    out += ",\"leafLayerVariantTag\":";
    out += std::to_string(static_cast<int>(command.leafLayer.Type()));
    out += ",\"composedLayerVariantTag\":";
    out += std::to_string(static_cast<int>(command.composedLayer.Type()));
    out += ",\"leafBuilt\":";
    out += command.leafBuilt ? "true" : "false";
    out += ",\"composedBuilt\":";
    out += command.composedBuilt ? "true" : "false";
    out += ",\"executedDirect\":";
    out += command.executedDirect ? "true" : "false";
    out += "}";
}

void appendRenderCommandsPayload(std::string &out,
                                 const motion::detail::PlayerRuntime *runtime) {
    constexpr size_t kLimit = 256;
    const size_t count = runtime ? runtime->renderCommands.size() : 0;
    out += "\"renderCommandCount\":";
    out += std::to_string(count);
    out += ",\"topLevelCommandCount\":";
    out += std::to_string(runtime ? runtime->renderCommandsTopLevel.size() : 0);
    out += ",\"groupCommandCount\":";
    out += std::to_string(runtime ? runtime->renderCommandsGroup.size() : 0);
    out += ",\"renderCommands\":[";
    const size_t n = std::min(count, kLimit);
    for(size_t i = 0; i < n; ++i) {
        appendRenderCommandJson(out, runtime, runtime->renderCommands[i], i);
    }
    out.push_back(']');
    if(count > n) {
        out += ",\"renderCommandsTruncated\":";
        out += std::to_string(count - n);
    }
}

std::string playerDiagnostics(motion::Player *player) {
    std::string diag = "{\"player\":";
    diag += ptrHex(player);
    diag += ",\"activeMotion\":";
    appendJsonString(diag, activeMotionPath(player));
    diag += ",\"samplingMode\":\"guest-cpp-probe\"}";
    return diag;
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
    state.lastCompletedFrameId = frameId;
    state.lastCompletedTopPlayer = topPlayer;
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
    g_render_probe_jsonl.clear();
    g_render_probe_seq = 0;
    auto &state = traceState();
    state.inProgress = false;
    state.inRender = false;
    state.objthis = nullptr;
    state.players.clear();
    state.frameCounter = 0;
    state.lastCompletedFrameId = -1;
    state.lastCompletedTopPlayer = nullptr;
    state.currentRenderFrameId = -1;
    state.currentRenderPlayer = nullptr;
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

MotionTraceRenderDrawScope::MotionTraceRenderDrawScope(
    Player *player, void *argVariant, void *targetObject) :
    _player(player),
    _argVariant(argVariant),
    _targetObject(targetObject) {
    auto &state = traceState();
    state.inRender = true;
    state.currentRenderFrameId = state.lastCompletedFrameId;
    state.currentRenderPlayer = player;
    std::string payload = "\"targetObject\":";
    payload += ptrHex(targetObject);
    std::string diagnostics = "{\"argVariant\":";
    diagnostics += ptrHex(argVariant);
    diagnostics += ",\"targetObject\":";
    diagnostics += ptrHex(targetObject);
    diagnostics += ",\"sampling\":\"guest-cpp-drawCompat-0x6D5FB8\"}";
    appendRenderEvent(player, "draw_dispatch", "draw_enter",
                      "Player::drawCompat_0x6D5FB8.enter", payload,
                      diagnostics);
}

MotionTraceRenderDrawScope::~MotionTraceRenderDrawScope() {
    std::string payload = "\"route\":";
    appendJsonString(payload, _route ? _route : "unknown");
    payload += ",\"targetObject\":";
    payload += ptrHex(_targetObject);
    std::string diagnostics = "{\"argVariant\":";
    diagnostics += ptrHex(_argVariant);
    diagnostics += ",\"targetObject\":";
    diagnostics += ptrHex(_targetObject);
    diagnostics += ",\"sampling\":\"guest-cpp-drawCompat-0x6D5FB8\"}";
    appendRenderEvent(_player, "draw_dispatch", "draw_leave",
                      "Player::drawCompat_0x6D5FB8.leave", payload,
                      diagnostics);
    auto &state = traceState();
    state.inRender = false;
    state.currentRenderFrameId = -1;
    state.currentRenderPlayer = nullptr;
}

void MotionTraceRenderDrawScope::setRoute(const char *route) {
    _route = route;
}

MotionTraceRenderExecuteScope::MotionTraceRenderExecuteScope(
    Player *player, void *renderLayerObject, bool skipUpdate) :
    _player(player),
    _renderLayerObject(renderLayerObject),
    _skipUpdate(skipUpdate) {
    const auto *runtime = player ? player->runtime() : nullptr;
    std::string payload;
    appendRenderCommandsPayload(payload, runtime);
    payload += ",\"renderLayerObject\":";
    payload += ptrHex(renderLayerObject);
    payload += ",\"skipUpdate\":";
    payload += skipUpdate ? "true" : "false";
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_execute", "execute_enter",
                      "Player::executeLayerRenderCommands.enter",
                      payload, diagnostics);
}

MotionTraceRenderExecuteScope::~MotionTraceRenderExecuteScope() {
    const auto *runtime = _player ? _player->runtime() : nullptr;
    std::string payload;
    appendRenderCommandsPayload(payload, runtime);
    payload += ",\"renderLayerObject\":";
    payload += ptrHex(_renderLayerObject);
    payload += ",\"skipUpdate\":";
    payload += _skipUpdate ? "true" : "false";
    payload += ",\"ok\":";
    payload += _ok ? "true" : "false";
    std::string diagnostics = playerDiagnostics(_player);
    appendRenderEvent(_player, "render_execute", "execute_leave",
                      "Player::executeLayerRenderCommands.leave",
                      payload, diagnostics);
}

void MotionTraceRenderExecuteScope::setResult(bool ok) {
    _ok = ok;
}

void motionTraceRenderPreparedItems(Player *player, const char *kind,
                                    const char *samplePoint) {
    const auto *runtime = player ? player->runtime() : nullptr;
    std::string payload;
    appendPreparedItemsPayload(payload, runtime);
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_prepare",
                      kind ? kind : "prepared_items",
                      samplePoint ? samplePoint : "Player::prepareRenderItems",
                      payload, diagnostics);
}

void motionTraceRenderCommands(Player *player, const char *kind,
                               const char *samplePoint,
                               int canvasWidth, int canvasHeight) {
    const auto *runtime = player ? player->runtime() : nullptr;
    std::string payload;
    appendRenderCommandsPayload(payload, runtime);
    payload += ",\"canvas\":{\"width\":";
    payload += std::to_string(canvasWidth);
    payload += ",\"height\":";
    payload += std::to_string(canvasHeight);
    payload += "}";
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_commands",
                      kind ? kind : "render_commands_ready",
                      samplePoint ? samplePoint : "Player::buildRenderCommands.leave",
                      payload, diagnostics);
}

} // namespace motion::detail

extern "C" {

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_render_probe_ptr() {
    if(g_render_probe_jsonl.empty())
        return 0;
    return static_cast<int>(
        reinterpret_cast<std::uintptr_t>(g_render_probe_jsonl.data()));
}

EMSCRIPTEN_KEEPALIVE
int krkr2_wasm_get_render_probe_len() {
    return static_cast<int>(g_render_probe_jsonl.size());
}

EMSCRIPTEN_KEEPALIVE
void krkr2_wasm_clear_render_probe() {
    g_render_probe_jsonl.clear();
    g_render_probe_seq = 0;
}

} // extern "C"

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
