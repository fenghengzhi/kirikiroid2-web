#pragma once

#include "PlayerInternal.h"
#include "BitmapIntf.h"

namespace motion::internal::render_detail {

#if defined(KRKR2_WASMTIME_HEADLESS)
extern "C" void TVPResetSoftwareAffineDiagnosticsForWasmtime();
#endif

bool getLayerClassDispatchVariantLike_0x5CB08C(tTJSVariant &layerClassVar);
tjs_error callLayerOperateAffineLike_0x6C7440(
    const tTJSVariant &layerClassObject,
    iTJSDispatch2 *renderLayerObject,
    const tTVPPointD *points,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity,
    tTVPBBStretchType type);
// Generalized per-primitive dispatch helpers mirroring sub_6C7440 @ 0x6C7440
// and Player_emitRenderItem_requireLayer @ 0x6C4E28: each builds the exact
// tTJSVariant arg array the binary packs and routes through
// iTJSDispatch2::FuncCall on the render-layer INSTANCE (objthis = same
// instance), not the Layer class object.
//
// buildMeshPointTJSArrayLike_0x6C715C builds a TJS Array of interleaved
// doubles (x,y,x,y,...) translated by (xOffset,yOffset), exactly as
// sub_6C715C @ 0x6C715C does for the mesh/bezier point arrays.
iTJSDispatch2 *buildMeshPointTJSArrayLike_0x6C715C(
    const std::vector<float> &points, float xOffset, float yOffset);

// affineCopy (argc=14): [src, sx, sy, sw, sh, useMatrix=false,
//   x0, y0, x1, y1, x2, y2, type, clear]. Dispatched on the render-layer
//   instance, matching sub_6C7440's L"affineCopy" block.
tjs_error callLayerAffineCopyLike_0x6C7440(
    iTJSDispatch2 *renderLayerObject,
    const tTVPPointD *points,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    tTVPBBStretchType type,
    bool clear);

tjs_error callLayerMeshCopyLike_0x6C7440(
    iTJSDispatch2 *renderLayerObject,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    iTJSDispatch2 *meshPointArray,
    tjs_int divx,
    tjs_int divy,
    tTVPBBStretchType type,
    bool clear);
tjs_error callLayerBezierPatchCopyLike_0x6C7440(
    iTJSDispatch2 *renderLayerObject,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    iTJSDispatch2 *meshPointArray,
    tjs_int divx,
    tjs_int divy,
    tTVPBBStretchType type,
    bool clear);
tjs_error callLayerOperateMeshLike_0x6C7440(
    iTJSDispatch2 *renderLayerObject,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    iTJSDispatch2 *meshPointArray,
    tjs_int divx,
    tjs_int divy,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity,
    bool clear);
tjs_error callLayerOperateBezierPatchLike_0x6C7440(
    iTJSDispatch2 *renderLayerObject,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    iTJSDispatch2 *meshPointArray,
    tjs_int divx,
    tjs_int divy,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity,
    bool clear);

// operateRect (argc=9): [dx, dy, src, sx, sy, sw, sh, mode, opa]. Dispatched
//   on the render-layer instance, matching sub_6C7440's L"operateRect" block.
tjs_error callLayerOperateRectLike_0x6C7440(
    iTJSDispatch2 *renderLayerObject,
    tjs_int destX,
    tjs_int destY,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity);

std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor);
iTJSDispatch2 *resolvePrimaryLayerObject(iTJSDispatch2 *layerTreeOwnerObject);
iTJSDispatch2 *resolveMainWindowOwnerObject();
iTJSDispatch2 *resolveMainWindowPrimaryLayerObject();
void pushGraphicCandidates(std::vector<ttstr> &candidates, const ttstr &base);
ttstr resolveMotionSourcePath(const motion::detail::MotionSnapshot &snapshot,
                              const std::string &source);
iTJSDispatch2 *createLayerObject(iTJSDispatch2 *layerTreeOwnerObject,
                                 iTJSDispatch2 *parentLayerObject);
bool configureReusableLayerObject(iTJSDispatch2 *layerObject,
                                  iTJSDispatch2 *parentLayerObject,
                                  tTVPLayerType layerType,
                                  bool visible,
                                  bool absoluteOrderMode);
iTJSDispatch2 *ensureReusableLayerObject(tTJSVariant &slot,
                                         iTJSDispatch2 *layerTreeOwnerObject,
                                         iTJSDispatch2 *parentLayerObject,
                                         tTVPLayerType layerType,
                                         bool visible,
                                         bool absoluteOrderMode = false);
tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject);
bool queryLayerCanvasSize(iTJSDispatch2 *layerObject, int &width, int &height);
bool setObjectIntProperty(iTJSDispatch2 *object, const tjs_char *name,
                          tjs_int value);
bool prepareLayerForRender(iTJSDispatch2 *layerObject,
                           int width,
                           int height,
                           tjs_uint32 clearColor);
std::string summarizeLayerChildren(tTJSNI_BaseLayer *layer, int maxChildren = 12);
bool shouldUseDirectRenderPathLike_0x6C7440(
    const motion::detail::PreparedRenderItem &item,
    bool clearEnabled);

tTVPBlendOperationMode resolveBlendOperationModeLike_0x6C7440(int rawBlendMode);

std::array<tTVPPointD, 3> buildAffineTrianglePoints(
    const std::array<float, 8> &corners,
    float xOffset,
    float yOffset);
std::vector<tTVPPointD> buildMeshPoints(
    const std::vector<float> &points,
    float xOffset,
    float yOffset);

motion::D3DAdaptor *ensureSharedD3DAdaptor(iTJSDispatch2 *targetLayerObject);

struct RenderClipRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

bool computeRenderClipRect(
    const motion::detail::PreparedRenderItem &item,
    int renderWidth,
    int renderHeight,
    RenderClipRect &out,
    std::string *failureReason = nullptr);
bool isAccurateSlaRenderEnabled();

tTVPRect localRectFromItem(
    const motion::detail::PreparedRenderItem &item);

void persistNativeRenderItemFieldLifetimeLike_0x6C4E28(
    motion::detail::PreparedRenderItem &item);
bool clearLayerAlphaOutsideRect(tTJSNI_BaseLayer *layer,
                                const tTVPRect &outerRect,
                                const tTVPRect &innerRect);
bool applyMotionAlphaMaskLike_0x6AF104(
    iTJSDispatch2 *dstLayerObject,
    int dstX,
    int dstY,
    iTJSDispatch2 *srcLayerObject,
    int srcX,
    int srcY,
    int width,
    int height,
    int threshold,
    int playerStencilType,
    int itemFlags,
    const std::string &motionPath,
    double frameTime,
    int dstNodeIndex,
    int srcNodeIndex);
void emitDirectExecuteDiagnostics(
    motion::Player *player,
    const char *samplePoint,
    const char *probePhase,
    const char *branch,
    const char *executionMethod,
    const motion::detail::PreparedRenderItem &item,
    tTJSNI_BaseLayer *renderLayer,
    const std::shared_ptr<tTVPBaseBitmap> &srcBmp,
    iTJSDispatch2 *sourceArgObject,
    tTJSNI_BaseLayer *sourceArgLayer,
    const char *sourceArgClass,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity,
    tTVPBBStretchType type);

} // namespace motion::internal::render_detail
