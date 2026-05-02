// Wasmtime-only Motion playback differential glue.
//
// This file deliberately stays below the engine/platform boundary: it owns the
// exported test ABI, error buffer, framebuffer buffer, and MotionTraceWeb
// linkage symbols. Browser, Cocos, Window, FS, thread, event behavior, and
// differential trace collection must come from the normal engine sources plus
// host-provided env/WASI imports and LLDB guest inspection.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>

#include <emscripten/emscripten.h>

#include "Application.h"
#include "LayerIntf.h"
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
int g_render_draw_id = 0;
constexpr const char *kRenderStageCaptureRoot = "/render_stage_capture";

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

void appendJsonBoolOrNull(std::string &out, bool known, bool value) {
    if(!known) {
        out += "null";
        return;
    }
    out += value ? "true" : "false";
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
    out += item.skipFlag1 ? "0" : "1";
    out += ",\"drawFlag19\":";
    out += item.drawFlag ? "1" : "0";
    out += ",\"layerResolved20\":";
    out += item.rawFlag20 ? "1" : "0";
    out += ",\"clipValid21\":";
    out += item.rawFlag21 ? "1" : "0";
    out += "},\"layerIds\":{\"primary\":";
    out += std::to_string(item.layerId);
    out += ",\"secondary\":";
    out += std::to_string(item.layerId2);
    out += "},\"sortKey64\":";
    out += std::to_string(item.sortKey);
    out += ",\"paintBox\":";
    appendNumberArray(out, item.paintBox);
    out += ",\"clipRect\":";
    appendNumberArray(out, item.clipRect);
    out += ",\"buildClipRect\":";
    appendNumberArray(out, item.clipRect);
    out += ",\"dirtyRect\":";
    appendNumberArray(out, item.dirtyRect);
    out += ",\"viewportRect\":";
    appendNumberArray(out, item.viewport);
    out += ",\"sourceGate232\":";
    out += std::to_string(item.opacity);
    out += ",\"stencilType244\":";
    out += std::to_string(item.stencilComposite);
    out += ",\"parentItemIndex\":";
    out += std::to_string(preparedIndexFor(runtime, item.parentItem));
    out += ",\"childItemCount\":";
    out += std::to_string(item.childItems.size());
    out += ",\"meshType280\":";
    out += std::to_string(item.meshType);
    out += ",\"leafLayerVariantTag\":";
    out += std::to_string(static_cast<int>(item.leafLayer.Type()));
    out += ",\"composedLayerVariantTag\":";
    out += std::to_string(static_cast<int>(item.composedLayer.Type()));
    out += ",\"leafBuilt\":";
    out += item.leafBuilt ? "true" : "false";
    out += ",\"composedBuilt\":";
    out += item.composedBuilt ? "true" : "false";
    out += ",\"executedDirect\":";
    out += item.executedDirect ? "true" : "false";
    out += "}";
}

template <typename Predicate>
void appendPreparedItemList(std::string &out,
                            const motion::detail::PlayerRuntime *runtime,
                            const char *name,
                            Predicate predicate) {
    constexpr size_t kLimit = 256;
    out += ",\"";
    out += name;
    out += "\":[";
    if(runtime) {
        size_t emitted = 0;
        for(size_t i = 0; i < runtime->preparedRenderItems.size(); ++i) {
            const auto &item = runtime->preparedRenderItems[i];
            if(!predicate(item)) {
                continue;
            }
            if(emitted >= kLimit) {
                break;
            }
            appendPreparedItemJson(out, runtime, item, i);
            ++emitted;
        }
    }
    out.push_back(']');
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

void appendRenderItemsPayload(std::string &out,
                              const motion::detail::PlayerRuntime *runtime) {
    const size_t preparedCount =
        runtime ? runtime->preparedRenderItems.size() : 0;
    size_t validDrawableCount = 0;
    size_t leafBuiltCount = 0;
    size_t composedBuiltCount = 0;
    auto hasValidClipRect = [](const std::array<int, 4> &rect) {
        return rect[0] < rect[2] && rect[1] < rect[3];
    };
    auto hasValidPaintOrViewportRect =
        [](const motion::detail::PlayerRuntime::PreparedRenderItem &item) {
        if(item.rawFlag16 || item.skipFlag0 || item.opacity <= 0) return false;
        float left = item.paintBox[0];
        float top = item.paintBox[1];
        float right = item.paintBox[2];
        float bottom = item.paintBox[3];
        if(item.hasViewport && item.viewport[2] >= item.viewport[0] &&
           item.viewport[3] >= item.viewport[1]) {
            left = std::max(left, std::floor(item.viewport[0]));
            top = std::max(top, std::floor(item.viewport[1]));
            right = std::min(right, std::ceil(item.viewport[2]));
            bottom = std::min(bottom, std::ceil(item.viewport[3]));
        }
        return left < right && top < bottom;
    };
    if(runtime) {
        for(const auto &item : runtime->preparedRenderItems) {
            if((item.rawFlag21 && hasValidClipRect(item.clipRect)) ||
               (!item.rawFlag21 && hasValidPaintOrViewportRect(item))) {
                ++validDrawableCount;
            }
            if(item.leafBuilt) ++leafBuiltCount;
            if(item.composedBuilt) ++composedBuiltCount;
        }
    }
    out += "\"preparedItemCount\":";
    out += std::to_string(preparedCount);
    out += ",\"inputItemCount\":";
    out += std::to_string(preparedCount);
    out += ",\"builtItemCount\":";
    out += std::to_string(preparedCount);
    out += ",\"validDrawableItemCount\":";
    out += std::to_string(validDrawableCount);
    out += ",\"leafBuiltCount\":";
    out += std::to_string(leafBuiltCount);
    out += ",\"composedBuiltCount\":";
    out += std::to_string(composedBuiltCount);
    appendPreparedItemList(
        out, runtime, "mainListSemanticItems",
        [](const motion::detail::PlayerRuntime::PreparedRenderItem &item) {
            return item.topLevelList;
        });
    appendPreparedItemList(
        out, runtime, "auxListSemanticItems",
        [](const motion::detail::PlayerRuntime::PreparedRenderItem &item) {
            return item.groupList;
        });
}

std::string playerDiagnostics(motion::Player *player) {
    std::string diag = "{\"player\":";
    diag += ptrHex(player);
    diag += ",\"activeMotion\":";
    appendJsonString(diag, activeMotionPath(player));
    diag += ",\"samplingMode\":\"guest-cpp-probe\"}";
    return diag;
}

bool directoryExists(const std::string &path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string framePath(const char *phase, int frameId) {
    char name[128];
    std::snprintf(name, sizeof(name), "%s/_execute/%s/frame_%04d.png",
                  kRenderStageCaptureRoot, phase ? phase : "unknown",
                  frameId);
    return std::string(name);
}

void appendImageCheckpointEvent(motion::Player *player, const char *phase,
                                const char *samplePoint, bool ok,
                                const std::string &path,
                                const std::string &error) {
    std::string payload = "\"phase\":";
    appendJsonString(payload, phase ? phase : "");
    payload += ",\"ok\":";
    payload += ok ? "true" : "false";
    if(!path.empty()) {
        payload += ",\"guestPath\":";
        appendJsonString(payload, path);
    }
    if(!error.empty()) {
        payload += ",\"error\":";
        appendJsonString(payload, error);
    }
    appendRenderEvent(player, "render_execute", "execute_image_checkpoint",
                      samplePoint ? samplePoint : "execute_image_checkpoint",
                      payload, playerDiagnostics(player));
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
    g_render_draw_id = 0;
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
    _drawId = g_render_draw_id++;
    auto &state = traceState();
    state.inRender = true;
    state.currentRenderFrameId = state.lastCompletedFrameId;
    state.currentRenderPlayer = player;
    std::string payload = "\"drawId\":";
    payload += std::to_string(_drawId);
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
    const char *route = _route ? _route : (_steps.empty() ? "no_target" : "failed");
    std::string payload = "\"route\":";
    appendJsonString(payload, route);
    payload += ",\"drawId\":";
    payload += std::to_string(_drawId);
    payload += ",\"drawPath\":{\"route\":";
    appendJsonString(payload, route);
    payload += ",\"steps\":[";
    for(size_t i = 0; i < _steps.size(); ++i) {
        if(i) payload.push_back(',');
        appendJsonString(payload, _steps[i]);
    }
    payload += "],\"prepareCalled\":";
    payload += _prepareCalled ? "true" : "false";
    payload += ",\"prepareOk\":";
    appendJsonBoolOrNull(payload, _prepareOkKnown, _prepareOk);
    payload += ",\"d3dDrawModeAfterPrepare\":";
    appendJsonBoolOrNull(
        payload, _d3dDrawModeAfterPrepareKnown,
        _d3dDrawModeAfterPrepare);
    payload += ",\"renderToCanvasCalled\":";
    payload += _renderToCanvasCalled ? "true" : "false";
    payload += ",\"updateLayerAfterDrawCalled\":";
    payload += _updateLayerAfterDrawCalled ? "true" : "false";
    payload += ",\"internalAssignRequested\":";
    appendJsonBoolOrNull(
        payload, _internalAssignRequestedKnown,
        _internalAssignRequested);
    payload += ",\"imageChanged\":null}";
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

void MotionTraceRenderDrawScope::emitStep(
    const char *drawStep,
    const char *outcome,
    const char *route,
    const char *extraPayload) {
    if(!drawStep) return;
    if(route) _route = route;
    _steps.emplace_back(drawStep);
    std::string payload = "\"drawId\":";
    payload += std::to_string(_drawId);
    payload += ",\"stepIndex\":";
    payload += std::to_string(_stepIndex++);
    payload += ",\"drawStep\":";
    appendJsonString(payload, drawStep);
    payload += ",\"outcome\":";
    appendJsonString(payload, outcome ? outcome : "");
    if(route) {
        payload += ",\"route\":";
        appendJsonString(payload, route);
    }
    if(extraPayload && extraPayload[0] != '\0') {
        payload.push_back(',');
        payload += extraPayload;
    }
    std::string diagnostics = "{\"argVariant\":";
    diagnostics += ptrHex(_argVariant);
    diagnostics += ",\"targetObject\":";
    diagnostics += ptrHex(_targetObject);
    diagnostics += ",\"sampling\":\"guest-cpp-drawCompat-0x6D5FB8\"}";
    std::string samplePoint = "Player::drawCompat_0x6D5FB8.";
    samplePoint += drawStep;
    appendRenderEvent(_player, "draw_dispatch", "draw_step",
                      samplePoint.c_str(), payload, diagnostics);
}

void MotionTraceRenderDrawScope::recordTargetCheckD3D(bool hit) {
    emitStep("target_check_d3d", hit ? "hit" : "miss",
             hit ? "d3d_adaptor" : nullptr);
}

void MotionTraceRenderDrawScope::recordTargetCheckSLA(bool hit) {
    emitStep("target_check_sla", hit ? "hit" : "miss",
             hit ? "separate_layer_adaptor" : nullptr);
}

void MotionTraceRenderDrawScope::recordPrepareResult(bool ok) {
    _prepareCalled = true;
    _prepareOk = ok;
    _prepareOkKnown = true;
    emitStep("prepare_render_items", ok ? "ok" : "empty",
             ok ? nullptr : "prepare_empty",
             ok ? "\"prepareOk\":true" : "\"prepareOk\":false");
}

void MotionTraceRenderDrawScope::recordBranchAfterPrepare(bool d3dDrawMode) {
    _d3dDrawModeAfterPrepare = d3dDrawMode;
    _d3dDrawModeAfterPrepareKnown = true;
    emitStep(
        "branch_after_prepare",
        d3dDrawMode ? "shared_d3d" : "ordinary",
        d3dDrawMode ? "shared_d3d_after_prepare" : "ordinary_layer",
        d3dDrawMode
            ? "\"d3dDrawModeAfterPrepare\":true"
            : "\"d3dDrawModeAfterPrepare\":false");
}

void MotionTraceRenderDrawScope::recordApplyTranslateOffset() {
    emitStep("apply_translate_offset", "done");
}

void MotionTraceRenderDrawScope::recordRenderToCanvas(bool ok) {
    _renderToCanvasCalled = true;
    emitStep("render_to_canvas", ok ? "done" : "failed", "ordinary_layer");
}

void MotionTraceRenderDrawScope::recordUpdateLayerAfterDraw(
    bool internalAssignRequested, bool ok) {
    _updateLayerAfterDrawCalled = true;
    _internalAssignRequested = internalAssignRequested;
    _internalAssignRequestedKnown = true;
    emitStep(
        "update_layer_after_draw",
        ok ? "done" : "failed",
        "ordinary_layer",
        internalAssignRequested
            ? "\"internalAssignRequested\":true"
            : "\"internalAssignRequested\":false");
}

MotionTraceRenderExecuteScope::MotionTraceRenderExecuteScope(
    Player *player, void *renderLayerObject, bool skipUpdate) :
    _player(player),
    _renderLayerObject(renderLayerObject),
    _skipUpdate(skipUpdate) {
    const auto *runtime = player ? player->runtime() : nullptr;
    std::string payload;
    appendRenderItemsPayload(payload, runtime);
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
    appendRenderItemsPayload(payload, runtime);
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
    appendRenderItemsPayload(payload, runtime);
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

void motionTraceRenderImageCheckpoint(Player *player,
                                      void *renderLayerObject,
                                      const char *phase,
                                      const char *samplePoint) {
    const int frameId = renderFrameIdFor(player);
    if(frameId < 0 || !phase || !renderLayerObject) {
        return;
    }
    const std::string phaseDir =
        std::string(kRenderStageCaptureRoot) + "/_execute/" + phase;
    if(!directoryExists(phaseDir)) {
        return;
    }
    auto *layerObject = static_cast<iTJSDispatch2 *>(renderLayerObject);
    tTJSNI_BaseLayer *layer = nullptr;
    if(TJS_FAILED(layerObject->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
        appendImageCheckpointEvent(
            player, phase, samplePoint, false, {},
            "renderLayerObject did not resolve to Layer native instance");
        return;
    }

    const auto path = framePath(phase, frameId);
    try {
        layer->SaveLayerImage(motion::detail::widen(path), TJS_W("png"));
        appendImageCheckpointEvent(player, phase, samplePoint, true, path, {});
    } catch(const eTJS &e) {
        appendImageCheckpointEvent(
            player, phase, samplePoint, false, path,
            motion::detail::narrow(e.GetMessage()));
    } catch(const std::exception &e) {
        appendImageCheckpointEvent(
            player, phase, samplePoint, false, path, e.what());
    } catch(...) {
        appendImageCheckpointEvent(
            player, phase, samplePoint, false, path,
            "unknown exception while saving Layer image checkpoint");
    }
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
