//
// Internal helpers for motionplayer/emoteplayer runtime state.
//
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>

#include "tjs.h"
#include "MotionNode.h"
#include "internal/ttstr_hash.h"

class iTVPTexture2D;

namespace motion {
    class Player;
    class SourceCache;
}

namespace motion::detail {

    // Runtime-owned parameter entry. Aligned to libkrkr2.so's 56-byte
    // Player+384 parameter table populated inside Player_initNonEmoteMotion
    // (0x6B365C) via sub_6B1718 / sub_6B202C.
    struct MotionParameterEntry {
        ttstr id;
        bool discretization = false;
        double rangeBegin = 0.0;
        double rangeEnd = 0.0;
        double rangeScale = 1.0;
        double value = 0.0;
        int mode = 0;
    };

    // Aligned to libkrkr2.so Player_dispatchEvents (0x6C4490):
    // type=0: onAction(param1, param2), type=1: onSync()
    struct MotionEvent {
        int type = 0;
        tTJSVariant param1;
        tTJSVariant param2;
    };

    // (The former VariableLabelEntry port model of Player+1296 has been
    // replaced by detail::VariableLabelScope / VariableLabelScopeDeque
    // (internal/value_structs.h + player_containers.h) — the byte-verified
    // 160B var-track item with cascadeKey/cursor/value/labelName/slot[2].
    // initVariables now builds the deque directly; see Player_initVariables
    // @0x6CD750 and analysis/Player_4_HashMaps_Container_Mapping.md §四之二.)

    // A5: lifted from PlayerRuntime's inner type to namespace scope so Player
    // can hold the renderLayerStates map without leaking the runtime's nested
    // structure outward.
    struct LayerRenderState {
        tjs_int layerId = 0;
        bool clipEnabled = true;
        bool initialized = false;
        bool isDirty = false;
        tjs_int absolute = 0;
        tjs_int hitThreshold = 256;
        tTJSVariant layerObject;
        tTJSVariant layerGetter;
        std::array<float, 4> clipRect{0.f, 0.f, 0.f, 0.f};
        std::array<float, 4> worldRect{0.f, 0.f, 0.f, 0.f};
        std::array<float, 4> localRect{0.f, 0.f, 0.f, 0.f};
        std::array<std::uint32_t, 4> packedColors{
            0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
        };
    };

    // A9: render-item structs lifted from PlayerRuntime's inner scope to
    // namespace detail:: so Player can hold the corresponding vectors / map
    // without leaking PlayerRuntime's nested type surface.

    // Native render-item fields from the anonymous 0x1B0 item built by
    // libkrkr2.so 0x6C2334 and consumed in-place by 0x6C4E28 / 0x6C7440.
    // These fields intentionally keep the native write lifecycle: +21 and
    // +216..228 are not blanket-cleared every frame.
    struct NativeRenderItemFields {
        bool rawFlag16 = false; // original item +16 = node+201
        bool skipFlag0 = false; // original render item +17 (0x6C2334 / 0x6C7440)
        bool skipFlag1 = false; // original render item +18 (0x6C2334 / 0x6C7440)
        bool drawFlag = false;  // original render item +19
        bool rawFlag20 = false; // original item +20, set by sub_6C4E28 requireLayerId path
        bool rawFlag21 = false; // original item +21, drawable clip valid after sub_6C4E28
        std::uint8_t stencilMaskRef = 0; // original item +22
        std::uint8_t stencilWriteRef = 0; // original item +23
        std::array<float, 4> paintBox{0.f, 0.f, 0.f, 0.f}; // item+184..196
        std::array<float, 4> viewport{1.f, 1.f, -1.f, -1.f}; // item+200..212
        std::array<int, 4> clipRect{0, 0, 0, 0}; // item+216..228
        std::array<int, 4> dirtyRect{0, 0, 0, 0};
        int opacity = 255; // item+232
        // item+244 in libkrkr2.so sub_6C2334 @ 0x6C2A90 — stencil/composite
        // flags copied from node.stencilType; consumed by sub_6C7440 alpha
        // mask path `(item+244 & 4)` / `(item+244 & 3)==1`.
        int stencilComposite = 0;
    };

    struct RenderItemNativeFieldLifetime {
        bool rawFlag20 = false;
        bool rawFlag21 = false;
        std::array<int, 4> clipRect{0, 0, 0, 0};
        std::array<int, 4> dirtyRect{0, 0, 0, 0};
        std::array<float, 8> localCorners{};
        std::vector<MeshPoint> localMeshPoints;
    };

    struct PreparedRenderItem : NativeRenderItemFields {
        int nodeIndex = 0;
        // sub_6C2334 @ 0x6C2334 keeps render items attached to their actual
        // MotionNode (binary node+1904) across recursive child-Player builds.
        // nodeIndex is owner-local and must never be reinterpreted through the
        // parent Player after child items are merged into its render list.
        const MotionNode *nativeNode = nullptr;
        tTJSVariant srcRef;
        std::string sourceKey;
        // Borrowed from the node/source-cache raw resource path.
        iTVPTexture2D *sourceTexture = nullptr;
        std::array<int, 4> sourceRect{0, 0, 0, 0};
        bool hasOwnSource = false;
        bool groupOnly = false;
        bool topLevelList = true;
        bool groupList = false;
        bool selfSeedChildList = false;
        // A9: nativeLifetimeOwner moves from PlayerRuntime* to Player* so the
        // lifetime owner survives the eventual A10 PlayerRuntime removal.
        motion::Player *nativeLifetimeOwner = nullptr;
        int nativeLifetimeKey = 0;
        double sortKey = 0.0;
        // sub_6C2334 @0x6C2334 stores the script command transform separately
        // from item+136 render corners.  getCommandList @0x6D3A4C serializes
        // these exact item+64/+72..112 fields; they must not be reconstructed
        // from already transformed geometry.
        std::array<double, 3> commandCoord{0.0, 0.0, 0.0};
        std::array<double, 4> commandMatrix{1.0, 0.0, 0.0, 1.0};
        int blendMode = 16;
        tTJSVariant contextVariant; // original item +248 (player+1012 copy)
        std::array<float, 8> corners{};
        std::array<float, 8> localCorners{};
        std::array<std::uint32_t, 4> packedColors{
            0xFF808080u, 0xFF808080u, 0xFF808080u, 0xFF808080u
        };
        bool hasViewport = false;
        int coordinateMode = 0;
        int objTriPriority = 0;
        double originX = 0.0;
        double originY = 0.0;
        int visibleAncestorIndex = -1;
        int meshDivX = 0;
        int meshDivY = 0;
        int meshType = 0;
        int commandPatchDivision = 0; // item+368
        std::vector<MeshPoint> commandCompositeMeshPoints; // item+344
        std::vector<MeshPoint> commandBezierPatchPoints;   // item+376
        std::vector<MeshPoint> meshPoints;                  // item+400
        std::vector<MeshPoint> localMeshPoints;
        int layerId = 0;
        int layerId2 = 0;
        PreparedRenderItem *parentItem = nullptr; // semantic mapping of item +264
        std::vector<PreparedRenderItem *> childItems; // semantic mapping of item +24
        tTJSVariant leafLayer;      // item+304 variant
        tTJSVariant composedLayer;  // item+324 variant
        std::array<int, 4> builtRect{0, 0, 0, 0};
        bool leafBuilt = false;
        bool composedBuilt = false;
        bool executedDirect = false;
    };

    // Faithful 1:1 element of libkrkr2.so player+936/944's
    // std::vector<DeadChildMotionRenderItem> (44-byte stride element).
    //
    // Binary layout (sub_6F363C @0x6F363C, the vector::_M_range_insert that
    // operates on this element type, copies each element as):
    //   elem+0  : int32              (*v6 = *(_DWORD *)v5)
    //   elem+4  : tTJSVariant        (sub_A0FB64(v6+1,  v5+4),  20 bytes)
    //   elem+24 : tTJSVariant        (sub_A0FB64(v6+6,  v5+24), 20 bytes)
    // total 44 bytes. Destroy path (sub_6BE2C0 / 0x6C1A00 / sub_6F363C
    // shrink) destroys the two variants via sub_A0F778(elem+24)+sub_A0F778(elem+4).
    //
    // This is the DEAD residual render-item buffer: in this libkrkr2.so build
    // it has NO producer (no leaf item is ever pushed in; sub_6C2334 writes
    // caller-stack temporaries instead) and NO consumer (nothing reads it).
    // It is only ever fed by aggregating child players' equally-empty +936
    // buffers (Player_updateLayers_childMotionPass @0x6BE2C0 and
    // Player_particleStepChildren @0x6C1A00), so it stays empty -> empty and
    // is observably inert. It is reproduced here purely for 1:1 structural
    // fidelity (the live draw-time render list is the SEPARATE
    // _preparedRenderItems, which corresponds to the binary's per-draw temp
    // vector built by sub_6C2334, NOT this buffer).
    //
    // Plain C++ POD with two tTJSVariant fields: a default std::vector of this
    // type gives the binary's ctor (player+936 zero-init at 0x6CEF1C, OWORD
    // store = empty vector) and dtor (per-element variant destroy + free)
    // for free.
    struct DeadChildMotionRenderItem {
        int kind = 0;          // elem+0
        tTJSVariant a;         // elem+4
        tTJSVariant b;         // elem+24
    };

    struct PerNodeEvalData {
        double padding[5] = {};   // offsets 0-39 (unused in our current scope)
        double evalTime = 0.0;
        int dirtyFlag = 0;
    };

    // A10: PlayerRuntime struct deleted after Phase A1-A9 hoisted every field
    // onto motion::Player. Forward declarations of the legacy type may still
    // appear in unrelated headers but the type no longer has any members
    // and is not instantiated anywhere.
    void ensureRootNodeLike_0x6CED30(motion::Player &player);
    void resetNodeTreeKeepRootLike_0x6B56F8(motion::Player &player);

    // Aligned with libkrkr2.so Player_buildNodePathKey @0x6B5C1C.
    // Walks the parentIndex chain from `nodeIndex` toward the synthetic root
    // (index 0), accumulating each node's "label" (node+0) as a "/"-prefixed
    // segment. Ancestors are prepended, so the result is the slash-joined
    // hierarchical path "/topLevelLabel/.../selfLabel". The loop terminates
    // when a node's parentIndex reaches 0 (the synthetic root is NOT emitted),
    // matching the binary `while ( a2 )` on `*(node+36)` (parentIndex).
    // This path is the key for HM3 (Player+1184, _perNodeLayerStateMap) ONLY —
    // it is the path builder's sole consumer (xrefs_to(0x6B5C1C) = 2 callers,
    // both HM3). The Player+24 node-index map (_nodeLabelMap) is keyed by the
    // RAW label, a separate key space — do NOT use this for that map.
    ttstr buildNodePathKeyLike_0x6B5C1C(
        const std::deque<motion::detail::MotionNode> &nodes, int nodeIndex);

    std::string narrow(const ttstr &value);
    ttstr widen(const std::string &value);

    tTJSVariant makeArray(const std::vector<tTJSVariant> &items);
    tTJSVariant makeDictionary(
        const std::vector<std::pair<std::string, tTJSVariant>> &entries);
    std::vector<tTJSVariant> stringsToVariants(
        const std::vector<std::string> &values);

    bool logoChainTraceEnabled();
    bool logoChainTraceEnabledForPath(const std::string &motionPath);
    bool logoSnapshotMarkEnabled();
    bool logoSnapshotMarkEnabledForPath(const std::string &motionPath);
    void resetLogoChainTraceSession(const std::string &motionPath);
    void logoChainTraceLog(const std::string &motionPath,
                           const char *stage,
                           const char *func,
                           double frameTime,
                           const std::string &message);
    void logoChainTraceCheck(const std::string &motionPath,
                             const char *stage,
                             const char *func,
                             double frameTime,
                             const std::string &expected,
                             const std::string &actual,
                             bool ok,
                             const std::string &likelyRootCause = {});
    void logoChainTraceSummary(const std::string &motionPath,
                               const char *func,
                               double frameTime,
                               const std::string &note = {});

    template <typename... Args>
    inline void logoChainTraceLogf(const std::string &motionPath,
                                   const char *stage,
                                   const char *func,
                                   double frameTime,
                                   fmt::format_string<Args...> format,
                                   Args &&...args) {
        if(!logoChainTraceEnabledForPath(motionPath)) {
            return;
        }
        logoChainTraceLog(motionPath, stage, func, frameTime,
                          fmt::format(format, std::forward<Args>(args)...));
    }

} // namespace motion::detail
