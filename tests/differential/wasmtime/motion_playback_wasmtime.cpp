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
#include <cstring>
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
bool g_record_layer_raw_probes = false;
bool g_record_save_layer_visual_readback_probes = false;
int g_save_layer_visual_readback_frame_start = 0;
int g_save_layer_visual_readback_frame_count = 1;
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

std::uint32_t rotr32(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

class Sha256 {
public:
    void update(const unsigned char *data, std::size_t len) {
        _totalBytes += len;
        if(_bufferSize != 0) {
            const auto take = std::min<std::size_t>(64 - _bufferSize, len);
            std::memcpy(_buffer + _bufferSize, data, take);
            _bufferSize += take;
            data += take;
            len -= take;
            if(_bufferSize == 64) {
                transform(_buffer);
                _bufferSize = 0;
            }
        }
        while(len >= 64) {
            transform(data);
            data += 64;
            len -= 64;
        }
        if(len > 0) {
            std::memcpy(_buffer, data, len);
            _bufferSize = len;
        }
    }

    std::string finalHex() {
        const std::uint64_t bitLen = _totalBytes * 8u;
        _buffer[_bufferSize++] = 0x80u;
        if(_bufferSize > 56) {
            while(_bufferSize < 64) _buffer[_bufferSize++] = 0;
            transform(_buffer);
            _bufferSize = 0;
        }
        while(_bufferSize < 56) _buffer[_bufferSize++] = 0;
        for(int i = 7; i >= 0; --i) {
            _buffer[_bufferSize++] =
                static_cast<unsigned char>((bitLen >> (i * 8)) & 0xffu);
        }
        transform(_buffer);

        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for(std::uint32_t word : _state) {
            for(int shift = 24; shift >= 0; shift -= 8) {
                const auto byte =
                    static_cast<unsigned char>((word >> shift) & 0xffu);
                out.push_back(kHex[byte >> 4]);
                out.push_back(kHex[byte & 0x0f]);
            }
        }
        return out;
    }

private:
    void transform(const unsigned char block[64]) {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };

        std::uint32_t m[64];
        for(int i = 0; i < 16; ++i) {
            m[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for(int i = 16; i < 64; ++i) {
            const auto s0 = rotr32(m[i - 15], 7) ^ rotr32(m[i - 15], 18) ^
                (m[i - 15] >> 3);
            const auto s1 = rotr32(m[i - 2], 17) ^ rotr32(m[i - 2], 19) ^
                (m[i - 2] >> 10);
            m[i] = m[i - 16] + s0 + m[i - 7] + s1;
        }

        std::uint32_t a = _state[0], b = _state[1], c = _state[2],
            d = _state[3], e = _state[4], f = _state[5],
            g = _state[6], h = _state[7];
        for(int i = 0; i < 64; ++i) {
            const auto s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const auto ch = (e & f) ^ ((~e) & g);
            const auto temp1 = h + s1 + ch + k[i] + m[i];
            const auto s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const auto maj = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        _state[0] += a;
        _state[1] += b;
        _state[2] += c;
        _state[3] += d;
        _state[4] += e;
        _state[5] += f;
        _state[6] += g;
        _state[7] += h;
    }

    std::uint32_t _state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    unsigned char _buffer[64] = {};
    std::size_t _bufferSize = 0;
    std::uint64_t _totalBytes = 0;
};

std::string rgbaSha256FromBgraRows(const unsigned char *pixels, int width,
                                   int height, int pitch) {
    Sha256 sha;
    std::vector<unsigned char> row(static_cast<std::size_t>(width) * 4u);
    for(int y = 0; y < height; ++y) {
        const auto *src = pixels + static_cast<std::size_t>(y) *
            static_cast<std::size_t>(pitch);
        for(int x = 0; x < width; ++x) {
            const auto dst = static_cast<std::size_t>(x) * 4u;
            row[dst] = src[x * 4 + 2];
            row[dst + 1u] = src[x * 4 + 1];
            row[dst + 2u] = src[x * 4];
            row[dst + 3u] = src[x * 4 + 3];
        }
        sha.update(row.data(), row.size());
    }
    return sha.finalHex();
}

std::string sha256Bytes(const unsigned char *data, std::size_t len) {
    Sha256 sha;
    sha.update(data, len);
    return sha.finalHex();
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

bool isCurrentRenderPlayer(motion::Player *player) {
    auto &state = traceState();
    return state.inRender && player && state.currentRenderPlayer == player;
}

void appendRenderEvent(motion::Player *player,
                       const char *stage,
                       const char *kind,
                       const char *samplePoint,
                       const std::string &payload,
                       const std::string &diagnostics) {
    const int frameId = renderFrameIdFor(player);
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
    size_t index,
    bool prependComma) {
    if(prependComma) out.push_back(',');
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
    out += ",\"packedColors\":[";
    for(size_t ci = 0; ci < item.packedColors.size(); ++ci) {
        if(ci) out.push_back(',');
        out += std::to_string(item.packedColors[ci]);
    }
    out += "]";
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
            appendPreparedItemJson(out, runtime, item, i, emitted > 0);
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
        appendPreparedItemJson(out, runtime, runtime->preparedRenderItems[i],
                               i, i > 0);
    }
    out.push_back(']');
    if(count > n) {
        out += ",\"preparedItemsTruncated\":";
        out += std::to_string(count - n);
    }
}

void appendRenderListHeader(std::string &out,
                            const void *vectorPtr,
                            const void *beginPtr,
                            const void *endPtr,
                            size_t count) {
    out += "{\"vector\":";
    out += ptrHex(vectorPtr);
    out += ",\"begin\":";
    out += ptrHex(beginPtr);
    out += ",\"end\":";
    out += ptrHex(endPtr);
    out += ",\"count\":";
    out += std::to_string(count);
    out += ",\"items\":[";
}

void appendEmptyRenderList(std::string &out) {
    appendRenderListHeader(out, nullptr, nullptr, nullptr, 0);
    out += "]}";
}

void appendPreparedVectorRenderList(
    std::string &out,
    const motion::detail::PlayerRuntime *runtime) {
    constexpr size_t kLimit = 256;
    const auto *items = runtime ? &runtime->preparedRenderItems : nullptr;
    const size_t count = items ? items->size() : 0;
    const void *beginPtr = count ? static_cast<const void *>(items->data()) : nullptr;
    const void *endPtr = count
        ? static_cast<const void *>(items->data() + items->size())
        : nullptr;
    appendRenderListHeader(out, items, beginPtr, endPtr, count);
    if(items) {
        const size_t n = std::min(count, kLimit);
        for(size_t i = 0; i < n; ++i) {
            appendPreparedItemJson(out, runtime, (*items)[i], i, i > 0);
        }
        out.push_back(']');
        if(count > n) {
            out += ",\"itemsTruncated\":";
            out += std::to_string(count - n);
        }
        out.push_back('}');
        return;
    }
    out += "]}";
}

void appendPreparedPointerRenderList(
    std::string &out,
    const motion::detail::PlayerRuntime *runtime,
    const std::vector<motion::detail::PlayerRuntime::PreparedRenderItem *> *items) {
    constexpr size_t kLimit = 256;
    const size_t count = items ? items->size() : 0;
    const void *beginPtr = count ? static_cast<const void *>(items->data()) : nullptr;
    const void *endPtr = count
        ? static_cast<const void *>(items->data() + items->size())
        : nullptr;
    appendRenderListHeader(out, items, beginPtr, endPtr, count);
    if(items) {
        const size_t n = std::min(count, kLimit);
        size_t emitted = 0;
        for(size_t i = 0; i < n; ++i) {
            const auto *item = (*items)[i];
            if(!item) continue;
            const int preparedIndex = preparedIndexFor(runtime, item);
            appendPreparedItemJson(
                out, runtime, *item,
                preparedIndex >= 0 ? static_cast<size_t>(preparedIndex) : i,
                emitted > 0);
            ++emitted;
        }
        out.push_back(']');
        if(count > n) {
            out += ",\"itemsTruncated\":";
            out += std::to_string(count - n);
        }
        out.push_back('}');
        return;
    }
    out += "]}";
}

void appendPreparedRenderListsPayload(
    std::string &out,
    const motion::detail::PlayerRuntime *runtime) {
    out += "\"renderLists\":{\"mainList\":";
    appendPreparedVectorRenderList(out, runtime);
    out += ",\"auxList\":";
    appendEmptyRenderList(out);
    out.push_back('}');
}

void appendCommandRenderListsPayload(
    std::string &out,
    const motion::detail::PlayerRuntime *runtime) {
    out += "\"renderLists\":{\"mainList\":";
    appendPreparedPointerRenderList(
        out, runtime,
        runtime ? &runtime->preparedRenderItemsTopLevel : nullptr);
    out += ",\"auxList\":";
    appendPreparedPointerRenderList(
        out, runtime,
        runtime ? &runtime->preparedRenderItemsGroup : nullptr);
    out.push_back('}');
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
    std::snprintf(name, sizeof(name), "%s/_execute/%s/frame_%04d.bgra",
                  kRenderStageCaptureRoot, phase ? phase : "unknown",
                  frameId);
    return std::string(name);
}

void appendImageCheckpointEvent(motion::Player *player, const char *phase,
                                const char *samplePoint, bool ok,
                                const std::string &path,
                                const std::string &error,
                                int width = 0,
                                int height = 0,
                                int pitch = 0,
                                const std::string &diagnostics = {}) {
    std::string payload = "\"phase\":";
    appendJsonString(payload, phase ? phase : "");
    payload += ",\"ok\":";
    payload += ok ? "true" : "false";
    if(width > 0) {
        payload += ",\"width\":";
        payload += std::to_string(width);
    }
    if(height > 0) {
        payload += ",\"height\":";
        payload += std::to_string(height);
    }
    if(pitch > 0) {
        payload += ",\"pitch\":";
        payload += std::to_string(pitch);
    }
    payload += ",\"pixelFormat\":\"bgra32\"";
    if(!path.empty()) {
        payload += ",\"guestPath\":";
        appendJsonString(payload, path);
    }
    if(!error.empty()) {
        payload += ",\"error\":";
        appendJsonString(payload, error);
    }
    appendRenderEvent(
        player, "render_execute", "execute_image_checkpoint",
        samplePoint ? samplePoint : "execute_image_checkpoint",
        payload, diagnostics.empty() ? playerDiagnostics(player) : diagnostics);
}

std::string checkpointDiagnostics(motion::Player *player,
                                  const char *captureMethod,
                                  const void *mainImage,
                                  int rowBytes,
                                  int failedRow = -1) {
    std::string diag = playerDiagnostics(player);
    if(!diag.empty() && diag.back() == '}') {
        diag.pop_back();
    }
    diag += ",\"captureMethod\":";
    appendJsonString(diag, captureMethod ? captureMethod : "");
    diag += ",\"mainImage\":";
    diag += ptrHex(mainImage);
    if(rowBytes > 0) {
        diag += ",\"rowBytes\":";
        diag += std::to_string(rowBytes);
    }
    if(failedRow >= 0) {
        diag += ",\"failedRow\":";
        diag += std::to_string(failedRow);
    }
    diag.push_back('}');
    return diag;
}

bool writePackedBgraRowsFromScanLines(const std::string &path,
                                      const tTVPBaseTexture *mainImage,
                                      int width,
                                      int height,
                                      int *failedRow) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if(!file) return false;
    bool ok = true;
    const auto rowBytes = static_cast<std::size_t>(width) * 4u;
    for(int y = 0; y < height; ++y) {
        const auto *row = static_cast<const unsigned char *>(
            mainImage->GetScanLine(static_cast<tjs_uint>(y)));
        if(!row) {
            if(failedRow) *failedRow = y;
            ok = false;
            break;
        }
        if(std::fwrite(row, 1, rowBytes, file) != rowBytes) {
            if(failedRow) *failedRow = y;
            ok = false;
            break;
        }
    }
    if(std::fclose(file) != 0) ok = false;
    return ok;
}

void appendLayerRawProbeEvent(motion::Player *player,
                              const char *samplePoint,
                              const tTJSNI_BaseLayer *layer,
                              bool ok,
                              const std::string &error,
                              const std::string &rgbaSha256,
                              int width,
                              int height,
                              int pitch,
                              const void *mainImage,
                              const void *bitmapImpl,
                              const void *buffer) {
    const int frameId = renderFrameIdFor(player);
    if(frameId < 0) return;
    motion::Player *eventPlayer = renderPlayerFor(player);
    const int seq = g_render_probe_seq++;
    std::string ev;
    ev += "{\"schema\":\"motion-render-stage-wasmtime-v1-event\"";
    ev += ",\"source\":\"wasmtime-port-layer-main-image-raw-probe\"";
    ev += ",\"stage\":\"layer_raw_probe\"";
    ev += ",\"kind\":\"raw_probe\"";
    ev += ",\"samplePoint\":";
    appendJsonString(ev, samplePoint ? samplePoint : "layer_raw_probe");
    ev += ",\"frameId\":";
    if(frameId >= 0) {
        ev += std::to_string(frameId);
    } else {
        ev += "null";
    }
    ev += ",\"player\":";
    ev += ptrHex(eventPlayer);
    ev += ",\"seq\":";
    ev += std::to_string(seq);
    ev += ",\"ok\":";
    ev += ok ? "true" : "false";
    if(width > 0) {
        ev += ",\"width\":";
        ev += std::to_string(width);
    }
    if(height > 0) {
        ev += ",\"height\":";
        ev += std::to_string(height);
    }
    if(pitch > 0) {
        ev += ",\"pitch\":";
        ev += std::to_string(pitch);
    }
    ev += ",\"pixelFormat\":\"bgra32\"";
    if(!rgbaSha256.empty()) {
        ev += ",\"rgbaSha256\":";
        appendJsonString(ev, rgbaSha256);
    }
    if(!error.empty()) {
        ev += ",\"error\":";
        appendJsonString(ev, error);
    }
    ev += ",\"nativeLayer\":";
    ev += ptrHex(layer);
    ev += ",\"mainImage\":";
    ev += ptrHex(mainImage);
    ev += ",\"bitmapImpl\":";
    ev += ptrHex(bitmapImpl);
    ev += ",\"buffer\":";
    ev += ptrHex(buffer);
    ev += ",\"diagnostics\":{\"samplingMode\":\"guest-cpp-raw-no-sync\"";
    ev += ",\"nativeLayer\":";
    ev += ptrHex(layer);
    ev += ",\"mainImage\":";
    ev += ptrHex(mainImage);
    ev += ",\"bitmapImpl\":";
    ev += ptrHex(bitmapImpl);
    ev += ",\"buffer\":";
    ev += ptrHex(buffer);
    ev += "}}\n";
    g_render_probe_jsonl += ev;
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
    g_record_layer_raw_probes = false;
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
    motionTraceLayerRawProbe(
        player, targetObject, "Player_drawCompat_0x6D5FB8.enter");
}

MotionTraceRenderDrawScope::~MotionTraceRenderDrawScope() {
    motionTraceLayerRawProbe(
        _player, _targetObject, "Player_drawCompat_0x6D5FB8.leave");
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
    payload += "}";
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
    motionTraceRenderImageCheckpoint(
        player, renderLayerObject, "execute_pre",
        "Player::executeLayerRenderCommands.enter.after-target-resolve");
    motionTraceLayerRawProbe(
        player, renderLayerObject, "sub_6C7440.enter");
    appendRenderEvent(player, "render_execute", "execute_enter",
                      "Player::executeLayerRenderCommands.enter",
                      payload, diagnostics);
}

MotionTraceRenderExecuteScope::~MotionTraceRenderExecuteScope() {
    motionTraceLayerRawProbe(
        _player, _renderLayerObject, "sub_6C7440.leave");
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
    motionTraceRenderImageCheckpoint(
        _player, _renderLayerObject, "execute_post",
        "Player::executeLayerRenderCommands.leave.before-return");
    appendRenderEvent(_player, "render_execute", "execute_leave",
                      "Player::executeLayerRenderCommands.leave",
                      payload, diagnostics);
}

void MotionTraceRenderExecuteScope::setResult(bool ok) {
    _ok = ok;
}

void motionTraceRenderPrepareEnter(Player *player) {
    if(!isCurrentRenderPlayer(player)) return;
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_prepare", "prepare_enter",
                      "Player::prepareRenderItems.enter", "", diagnostics);
}

void motionTraceRenderPrepareLeave(Player *player, bool ok) {
    if(!isCurrentRenderPlayer(player)) return;
    const auto *runtime = player ? player->runtime() : nullptr;
    std::string payload;
    payload += "\"ok\":";
    payload += ok ? "1" : "0";
    payload.push_back(',');
    appendPreparedRenderListsPayload(payload, runtime);
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_prepare", "prepare_leave",
                      "Player::prepareRenderItems.leave", payload,
                      diagnostics);
}

void motionTraceRenderApplyTranslateEnter(Player *player) {
    if(!isCurrentRenderPlayer(player)) return;
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_prepare", "apply_translate_enter",
                      "Player::applyPreparedRenderItemTranslateOffsets.enter",
                      "", diagnostics);
}

void motionTraceRenderApplyTranslateLeave(Player *player) {
    if(!isCurrentRenderPlayer(player)) return;
    const auto *runtime = player ? player->runtime() : nullptr;
    std::string payload;
    appendPreparedRenderListsPayload(payload, runtime);
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_prepare", "apply_translate_leave",
                      "Player::applyPreparedRenderItemTranslateOffsets.leave",
                      payload, diagnostics);
}

void motionTraceRenderBuildItemsEnter(Player *player) {
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_commands", "build_items_enter",
                      "Player::buildPreparedRenderItems.enter", "",
                      diagnostics);
}

void motionTraceRenderBuildItemsLeave(Player *player) {
    const auto *runtime = player ? player->runtime() : nullptr;
    std::string payload;
    appendPreparedRenderListsPayload(payload, runtime);
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_commands", "build_items_leave",
                      "Player::buildPreparedRenderItems.leave", payload,
                      diagnostics);
}

void motionTraceRenderBuildCommandsEnter(Player *player,
                                         int canvasWidth,
                                         int canvasHeight) {
    if(!isCurrentRenderPlayer(player)) return;
    const auto *runtime = player ? player->runtime() : nullptr;
    std::string payload;
    appendPreparedRenderListsPayload(payload, runtime);
    payload += ",\"canvas\":{\"width\":";
    payload += std::to_string(canvasWidth);
    payload += ",\"height\":";
    payload += std::to_string(canvasHeight);
    payload += "}";
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_commands", "build_commands_enter",
                      "Player::buildRenderCommands.enter", payload,
                      diagnostics);
}

void motionTraceRenderBuildCommandsLeave(Player *player,
                                         int canvasWidth,
                                         int canvasHeight) {
    if(!isCurrentRenderPlayer(player)) return;
    const auto *runtime = player ? player->runtime() : nullptr;
    std::string payload;
    appendCommandRenderListsPayload(payload, runtime);
    payload.push_back(',');
    appendRenderItemsPayload(payload, runtime);
    payload += ",\"canvas\":{\"width\":";
    payload += std::to_string(canvasWidth);
    payload += ",\"height\":";
    payload += std::to_string(canvasHeight);
    payload += "}";
    std::string diagnostics = playerDiagnostics(player);
    appendRenderEvent(player, "render_commands", "build_commands_leave",
                      "Player::buildRenderCommands.leave", payload,
                      diagnostics);
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
            "renderLayerObject did not resolve to Layer native instance",
            0, 0, 0,
            checkpointDiagnostics(
                player, "main-image-get-scanline", nullptr, 0));
        return;
    }

    const auto path = framePath(phase, frameId);
    const int width = static_cast<int>(layer->GetWidth());
    const int height = static_cast<int>(layer->GetHeight());
    auto *mainImage = layer->GetMainImage();
    const int rowBytes = width * 4;
    const auto diagnostics = [&](int failedRow = -1) {
        return checkpointDiagnostics(
            player, "main-image-get-scanline", mainImage, rowBytes, failedRow);
    };
    if(width <= 0 || height <= 0) {
        appendImageCheckpointEvent(
            player, phase, samplePoint, false, path,
            "Layer main image has invalid dimensions", width, height, rowBytes,
            diagnostics());
        return;
    }
    if(!mainImage) {
        appendImageCheckpointEvent(
            player, phase, samplePoint, false, path,
            "Layer main image is null", width, height, rowBytes, diagnostics());
        return;
    }
    if(static_cast<int>(mainImage->GetWidth()) != width ||
       static_cast<int>(mainImage->GetHeight()) != height) {
        appendImageCheckpointEvent(
            player, phase, samplePoint, false, path,
            "Layer main image dimensions differ from layer dimensions",
            width, height, rowBytes, diagnostics());
        return;
    }

    int failedRow = -1;
    bool ok = writePackedBgraRowsFromScanLines(
        path, mainImage, width, height, &failedRow);
    appendImageCheckpointEvent(
        player, phase, samplePoint, ok, path,
        ok ? std::string()
           : std::string("failed to write scanline BGRA checkpoint"),
        width, height, rowBytes, diagnostics(failedRow));
}

void motionTraceRenderDirectExecuteProbe(Player *player,
                                         const char *samplePoint,
                                         const char *payload) {
    if(!isCurrentRenderPlayer(player)) return;
    appendRenderEvent(
        player, "render_execute", "direct_execute_probe",
        samplePoint ? samplePoint : "Player::executeLayerRenderCommands.direct",
        payload ? payload : "", playerDiagnostics(player));
}

void motionTraceLayerRawProbeNative(Player *player, const void *nativeLayer,
                                    const char *samplePoint) {
    if(!g_record_layer_raw_probes || !nativeLayer) return;

    const auto *layer =
        static_cast<const tTJSNI_BaseLayer *>(nativeLayer);
    const auto *mainImage = layer->GetMainImageRawBackingNoSync();
    const auto *bitmapImpl = mainImage ? mainImage->GetTexture() : nullptr;
    const int width = mainImage ? static_cast<int>(mainImage->GetWidth()) : 0;
    const int height = mainImage ? static_cast<int>(mainImage->GetHeight()) : 0;
    const int pitch = static_cast<int>(
        layer->GetMainImageRawPixelBufferPitchNoSync());
    const auto *pixels = static_cast<const unsigned char *>(
        layer->GetMainImageRawPixelBufferNoSync());
    if(!mainImage || width <= 0 || height <= 0) {
        appendLayerRawProbeEvent(
            player, samplePoint, layer, false,
            "Layer has no raw main image", {}, width, height, pitch,
            mainImage, bitmapImpl, pixels);
        return;
    }
    if(pitch < width * 4) {
        appendLayerRawProbeEvent(
            player, samplePoint, layer, false,
            "Layer raw main image has invalid pitch", {}, width, height, pitch,
            mainImage, bitmapImpl, pixels);
        return;
    }
    if(!pixels) {
        appendLayerRawProbeEvent(
            player, samplePoint, layer, false,
            "Layer raw main image pixel buffer is null", {}, width, height,
            pitch,
            mainImage, bitmapImpl, pixels);
        return;
    }
    appendLayerRawProbeEvent(
        player, samplePoint, layer, true, {},
        rgbaSha256FromBgraRows(pixels, width, height, pitch),
        width, height, pitch, mainImage, bitmapImpl, pixels);
}

void motionTraceLayerRawProbe(Player *player, void *renderLayerObject,
                              const char *samplePoint) {
    if(!g_record_layer_raw_probes || !renderLayerObject) return;
    auto *layerObject = static_cast<iTJSDispatch2 *>(renderLayerObject);
    tTJSNI_BaseLayer *layer = nullptr;
    if(TJS_FAILED(layerObject->NativeInstanceSupport(
           TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
           reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
        return;
    }
    motionTraceLayerRawProbeNative(player, layer, samplePoint);
}

bool saveLayerVisualReadbackFrameEnabled(int frameId) {
    if(!g_record_save_layer_visual_readback_probes || frameId < 0) {
        return false;
    }
    const int start = std::max(0, g_save_layer_visual_readback_frame_start);
    const int count = g_save_layer_visual_readback_frame_count;
    if(frameId < start) return false;
    return count < 0 || frameId < start + count;
}

void motionTraceSaveLayerVisualReadbackRow(const void *image,
                                           int y,
                                           const void *row,
                                           int rowBytes,
                                           int width,
                                           int height,
                                           int bpp) {
    motion::Player *player = renderPlayerFor(nullptr);
    const int frameId = renderFrameIdFor(player);
    if(!saveLayerVisualReadbackFrameEnabled(frameId)) return;

    std::string payload;
    payload += "\"sourceDetail\":\"wasmtime-port-saveLayerImage-visual-readback\"";
    payload += ",\"image\":";
    payload += ptrHex(image);
    payload += ",\"row\":";
    payload += std::to_string(y);
    payload += ",\"width\":";
    payload += std::to_string(width);
    payload += ",\"height\":";
    payload += std::to_string(height);
    payload += ",\"rowBytes\":";
    payload += std::to_string(rowBytes);
    payload += ",\"bpp\":";
    payload += std::to_string(bpp);
    payload += ",\"pixelFormat\":\"rgba32-source-row\"";
    payload += ",\"rowPtr\":";
    payload += ptrHex(row);
    if(!row || rowBytes <= 0 || width <= 0 || height <= 0 ||
       y < 0 || y >= height) {
        payload += ",\"ok\":false";
        payload += ",\"error\":\"invalid visual readback row\"";
    } else {
        const auto *bytes = static_cast<const unsigned char *>(row);
        payload += ",\"ok\":true";
        payload += ",\"rowSha256\":";
        appendJsonString(
            payload,
            sha256Bytes(bytes, static_cast<std::size_t>(rowBytes)));
    }
    appendRenderEvent(player, "layer_visual_readback",
                      "save_layer_visual_readback_row",
                      "saveLayerImage_0x80963C.visual_readback_row",
                      payload, playerDiagnostics(player));
}

} // namespace motion::detail

extern "C" void krkr2_wasm_motion_trace_layer_raw_probe_native(
    const char *samplePoint, const void *nativeLayer) {
    motion::detail::motionTraceLayerRawProbeNative(
        nullptr, nativeLayer, samplePoint);
}

extern "C" void krkr2_wasm_motion_trace_save_layer_visual_readback_row(
    const void *image, int y, const void *row, int rowBytes, int width,
    int height, int bpp) {
    motion::detail::motionTraceSaveLayerVisualReadbackRow(
        image, y, row, rowBytes, width, height, bpp);
}

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

EMSCRIPTEN_KEEPALIVE
void krkr2_wasm_set_record_layer_raw_probes(int enabled) {
    g_record_layer_raw_probes = enabled != 0;
}

EMSCRIPTEN_KEEPALIVE
void krkr2_wasm_set_record_save_layer_visual_readback_probes(
    int enabled, int frame_start, int frame_count) {
    g_record_save_layer_visual_readback_probes = enabled != 0;
    g_save_layer_visual_readback_frame_start = frame_start;
    g_save_layer_visual_readback_frame_count = frame_count;
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
