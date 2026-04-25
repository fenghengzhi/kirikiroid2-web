#include "MotionTraceWeb.h"

#include "Player.h"
#include "RuntimeSupport.h"

#include <cstdio>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace motion::detail {
namespace {

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

    std::string ptrString(const void *ptr) {
        if(!ptr) {
            return {};
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%p", ptr);
        return buf;
    }

#ifdef __EMSCRIPTEN__
    bool traceRequested() {
        static int enabled = -1;
        if(enabled < 0) {
            enabled = EM_ASM_INT({
                try {
                    if(typeof window === 'undefined') return 0;
                    const p = new URLSearchParams(window.location.search);
                    const v = p.get('motionTrace');
                    if(v === '1' || v === 'true') {
                        window.__krkr2MotionTrace = [];
                        return 1;
                    }
                } catch(e) {}
                return 0;
            });
        }
        return enabled != 0;
    }

    void emitFrameJson(const std::string &json) {
        EM_ASM({
            try {
                if(typeof window === 'undefined') return;
                if(!window.__krkr2MotionTrace) window.__krkr2MotionTrace = [];
                window.__krkr2MotionTrace.push(JSON.parse(UTF8ToString($0)));
            } catch(e) {
                console.error('[motionTrace] emit failed', e);
            }
        }, json.c_str());
    }
#else
    bool traceRequested() { return false; }
    void emitFrameJson(const std::string &) {}
#endif

    struct TraceState {
        bool inProgress = false;
        void *objthis = nullptr;
        std::vector<Player *> players;
        int frameCounter = 0;
    };

    TraceState &traceState() {
        static TraceState state;
        return state;
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

    void appendNodeJson(std::ostringstream &out,
                        const MotionNode &node,
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
        // Frida oracle reads node+52 and names it blendMode; locally this is
        // the persistent stencilType field, not accumulated.blendMode.
        out << ",\"blendMode\":" << node.stencilType;
        out << ",\"drawFlag\":" << (node.drawFlag ? "true" : "false");
        out << ",\"drawnThisFrame\":"
            << (node.drawnThisFrame ? "true" : "false");
        out << ",\"currentImage\":";
        writeJsonString(out, node.interpolatedCache.src);
        out << "}";
    }

    void emitProgressFrame(Player *fallbackPlayer) {
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
        Player *topPlayer =
            !state.players.empty() ? state.players.front() : fallbackPlayer;
        if(topPlayer) {
            writeJsonString(out, ptrString(topPlayer));
        } else {
            out << "null";
        }
        out << ",\"playerCount\":" << state.players.size();
        out << ",\"layout\":\"web-runtime\"";
        out << ",\"layers\":[";

        int flatIndex = 0;
        bool first = true;
        for(Player *player : state.players) {
            if(!player) {
                continue;
            }
            const auto *runtime = player->runtime();
            if(!runtime) {
                continue;
            }
            for(const auto &node : runtime->nodes) {
                if(!first) {
                    out << ",";
                }
                first = false;
                appendNodeJson(out, node, flatIndex++);
            }
        }

        out << "]}";
        emitFrameJson(out.str());
    }

} // namespace

MotionTraceProgressScope::MotionTraceProgressScope(Player *player,
                                                   void *objthis) :
    _player(player) {
    if(!traceRequested()) {
        return;
    }
    auto &state = traceState();
    state.inProgress = true;
    state.objthis = objthis;
    state.players.clear();
}

MotionTraceProgressScope::~MotionTraceProgressScope() {
    if(!traceRequested()) {
        return;
    }
    auto &state = traceState();
    if(!state.inProgress) {
        return;
    }
    emitProgressFrame(_player);
    state.inProgress = false;
    state.objthis = nullptr;
    state.players.clear();
}

void motionTraceRecordUpdatePlayer(Player *player) {
    if(!traceRequested()) {
        return;
    }
    auto &state = traceState();
    if(!state.inProgress || !player) {
        return;
    }
    state.players.push_back(player);
}

} // namespace motion::detail
