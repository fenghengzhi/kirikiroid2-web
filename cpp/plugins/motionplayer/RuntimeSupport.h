//
// Internal helpers for motionplayer/emoteplayer runtime state.
//
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
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

    // Runtime-owned parameter entry. The 64-bit references store this as a
    // 56-byte record. Exact per-binary addresses and offsets live in
    // analysis/; `division` is the source field, not a pre-divided scale.
    struct MotionParameterEntry {
        ttstr id;
        bool discretization = false;
        double rangeBegin = 0.0;
        double rangeEnd = 0.0;
        double division = 1.0;
        double value = 0.0;
        int mode = 0;
    };

    // One persistent Player event record. All four current references use this
    // same vector both for local pending callbacks and for child-to-parent
    // transfer: type=0 dispatches onAction(param1, param2), type=1 onSync().
    struct MotionEvent {
        int type = 0;
        tTJSVariant param1;
        tTJSVariant param2;
    };

    // Variable tracks now use detail::VariableLabelScopeDeque directly. The
    // four references agree on its source-level order and append-first build
    // boundary; ABI-specific Player offsets and 160/128-byte element sizes are
    // evidence coordinates recorded only under analysis/.

#if defined(KRKR2_WASMTIME_HEADLESS)
    // Test-side state for the Wasmtime headless submit reconstruction. This
    // type and its owning Player map do not participate in ordinary plugin
    // object layout or lifecycle.
    struct LayerRenderState {
        bool initialized = false;
        tjs_int absolute = 0;
        tTJSVariant layerObject;
    };
#endif

    struct PreparedRenderItem;

    // Source-level native core reconstructed from the current recursive
    // prepared-item builder, selective lazy constructor, and item destructor.
    // Declaration order is intentional: ordinary C++ reverse destruction
    // yields mesh vectors -> variants -> command key -> child vector -> command
    // source -> owner label. The native constructor initializes only the three
    // owning-string backings, four vectors, three Variant tags,
    // rawFlag16/drawFlag/rawFlag20, stencilComposite and
    // commandPatchDivision. Every other trivial field remains dormant until
    // its admitting builder/render path writes it. Numeric target offsets are
    // evidence coordinates only and live in analysis/, not in this type.
    struct NativePreparedRenderItemState {
        ttstr ownerLabel;
        ttstr commandSrc;

        bool rawFlag16 = false;
        bool skipFlag0;
        bool skipFlag1;
        bool drawFlag = false;
        bool rawFlag20 = false;
        bool rawFlag21;
        std::uint8_t stencilMaskRef;
        std::uint8_t stencilWriteRef;
        // All four constructors place the trivial flag group before this
        // owning pointer vector. Its elements are borrowed.
        std::vector<PreparedRenderItem *> childItems;
        int blendMode;
        int layerId1;
        int layerId2;
        // The physical Z/sort value is shared. Native getCommandList reads
        // this same field as coord[2]; only X/Y occupy the adjacent pair after
        // the four-double matrix.
        double sortKey;
        std::array<double, 4> commandMatrix;
        std::array<double, 2> commandCoord;
        double originX;
        double originY;
        std::array<float, 8> corners;
        std::array<std::uint32_t, 4> packedColors;
        std::array<float, 4> paintBox;
        std::array<float, 4> viewport;
        std::array<float, 4> clipRect;
        int opacity;
        int coordinateMode;
        int objTriPriority;
        int stencilComposite = 0;

        ttstr commandKey;
        // The builder stores a direct pointer to the node's persistent source
        // descriptor. The render-time texture getter mutates it and the next
        // rect read uses the same alias.
        MotionNode::SourceState *sourceState;
        PreparedRenderItem *parentItem;
        int meshDivX;
        int meshDivY;
        int meshType;
        tTJSVariant commandVariant;
        tTJSVariant leafLayer;
        tTJSVariant composedLayer;
        std::vector<MeshPoint> commandCompositeMeshPoints;
        int commandPatchDivision = 0;
        std::vector<MeshPoint> commandBezierPatchPoints;
        std::vector<MeshPoint> meshPoints;
        int renderLayerId;
    };

    // Web rendering needs extra portable observation/cache state not present
    // in the Android object. Keeping it in the derived layer makes all
    // platform owners die before the uninterrupted native RAII chain above.
    struct PreparedRenderItem final : NativePreparedRenderItemState {
        int nodeIndex = 0;
        bool hasOwnSource = false;
        bool hasViewport = false;
        // Explicit Web observation/cache sidecar. Native code keeps only
        // clipRect and the borrowed parentItem pointer.
        std::array<int, 4> dirtyRect{0, 0, 0, 0};

        // User-provided on purpose: every call site uses `new T()`, and an
        // implicitly/defaulted constructor would make that value-initialize
        // (zero) the complete object before applying the selective native
        // defaults above.
        PreparedRenderItem() {}
        PreparedRenderItem(const PreparedRenderItem &) = delete;
        PreparedRenderItem &operator=(const PreparedRenderItem &) = delete;
        PreparedRenderItem(PreparedRenderItem &&) = delete;
        PreparedRenderItem &operator=(PreparedRenderItem &&) = delete;
    };

    // Prepared-render callers construct two independent vectors on their own
    // stack and thread them through the build/render chain. They borrow
    // the persistent items owned by MotionNode; Player does not retain them.
    using PreparedRenderItemList = std::vector<PreparedRenderItem *>;

    // Child stepping transfers the child's pending callbacks to the beginning
    // of the parent's same event vector, then destroys the child elements while
    // retaining the child's allocation. Exact per-target layouts live in
    // analysis/.
    inline void prependAndClearChildPendingEvents_guess(
        std::vector<MotionEvent> &parent,
        std::vector<MotionEvent> &child) {
        parent.insert(parent.begin(), child.begin(), child.end());
        child.clear();
    }

    // Root-node ownership lives directly on motion::Player; there is no
    // intermediate runtime payload.
    void ensureRootNode_guess(motion::Player &player);

    // Generic node-order visitor used by old-tree teardown. Type-4 traversal
    // deliberately reads array element zero once per reported element count;
    // false from the callback stops the complete walk.
    bool visitNodeOwnedPlayerVariants_guess(
        const std::deque<MotionNode> &nodes,
        const std::function<bool(const tTJSVariant &)> &visitor);

    void eraseNonRootNodesAndClearLabelMap_guess(motion::Player &player);

    // Four-reference path builder shared only by the HM3 producer/consumer.
    // It walks parentIndex from a non-root node toward index zero and prepends
    // every "/" + label segment, yielding "/top/.../self". Empty labels emit
    // a bare slash; index zero returns the empty string. Native deque access is
    // unchecked, so invalid indices/parent chains remain undefined behavior.
    // The raw-label node-index map is an independent key space.
    ttstr buildNodePathKey_guess(
        const std::deque<motion::detail::MotionNode> &nodes, int nodeIndex);

    // Shared native Layer factory used by SourceCache, SLA layer resolution,
    // render-command composition, and Player's internal render workspace.
    // Its deliberately sharp failure and exception boundaries are part of the
    // reference behavior; callers must not add HRESULT/null recovery around it.
    tTJSVariant createLayerVariant_guess(
        const tTJSVariant &owner, const tTJSVariant &parent);

    std::string narrow(const ttstr &value);
    ttstr widen(const std::string &value);

    struct TJSArrayWithItems_guess {
        // Owning object closure: Object and ObjThis both retain the fresh
        // Array dispatch.  It is the sole lifetime owner for items below.
        tTJSVariant value;

        // Borrowed pointer into tTJSArrayNI.  It is non-null only when
        // NativeInstanceSupport returns exactly TJS_S_OK and remains valid
        // only while value keeps the Array/native instance alive.
        std::deque<tTJSVariant> *items;
    };

    [[nodiscard]] TJSArrayWithItems_guess
    createTJSArrayWithItems_guess();

    tTJSVariant makeArray(const std::vector<tTJSVariant> &items);
    tTJSVariant makeDictionary(
        const std::vector<std::pair<std::string, tTJSVariant>> &entries);

    bool logoChainTraceEnabled();
    bool logoChainTraceEnabledForPath(const std::string &motionPath);
    bool logoSnapshotMarkEnabled();
    bool logoSnapshotMarkEnabledForPath(const std::string &motionPath);
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
