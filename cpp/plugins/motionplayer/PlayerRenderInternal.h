#pragma once

#include "PlayerInternal.h"
#include "BitmapIntf.h"

namespace motion::internal::render_detail {

#if defined(KRKR2_WASMTIME_HEADLESS)
extern "C" void TVPResetSoftwareAffineDiagnosticsForWasmtime();
#endif

bool getLayerClassDispatchVariantLike_0x5CB08C(tTJSVariant &layerClassVar);
tjs_error callLayerOperateAffineLike_0x6C7440(
    iTJSDispatch2 *layerClassObject,
    iTJSDispatch2 *renderLayerObject,
    const tTVPPointD *points,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity);
// Generalized per-primitive dispatch helpers mirroring sub_6C7440 @ 0x6C7440
// and Player_emitRenderItem_requireLayer @ 0x6C4E28. Copy/setSize/fillRect use
// the source/buffer Layer instance as both receiver and objthis; target
// operate*/setClip calls instead use the Layer class receiver with the target
// Layer as objthis.
//
// buildMeshPointTJSArrayLike_0x6C715C builds a TJS Array of interleaved
// doubles (x,y,x,y,...) translated by (xOffset,yOffset), exactly as
// sub_6C715C @ 0x6C715C does for the mesh/bezier point arrays.
iTJSDispatch2 *buildMeshPointTJSArrayLike_0x6C715C(
    const std::vector<detail::MeshPoint> &points,
    float xOffset, float yOffset);

// sub_6C4E28 @0x6C5C00..0x6C5C34 derives Bezier cell counts from the
// command division and source dimensions. The returned values are cell
// counts; a mesh carrying them therefore owns (divx+1)*(divy+1) points.
std::array<tjs_int, 2> bezierPatchCellDivisionsLike_0x6C5C00(
    tjs_int division, double sourceWidth, double sourceHeight);

// Player_renderToCanvas @0x6C8E5C..0x6C8EEC, Player_renderAccurateSLA
// @0x6CA904..0x6CA97C, and the PrivateMotionGLL path @0x6DED54 use a
// distinct uint32 pipeline. Keep it separate from the 0x6C4E28 FP helper.
std::array<tjs_int, 2> bezierPatchCellDivisionsU32Like_0x6C8E5C(
    tjs_int division, double sourceWidth, double sourceHeight);

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

// Both Player_emitRenderItem_requireLayer @0x6C57B4 and
// Player_renderToCanvas @0x6C7D54 dispatch setSize with two Real Variants.
tjs_error callLayerSetSizeRealLike_0x6C7440(
    iTJSDispatch2 *layerObject,
    tjs_real width,
    tjs_real height);

// Player_renderToCanvas @0x6C83B0 deliberately calls Layer.fillRect with only
// four arguments [Integer 0, Real width, Real height, Integer 0].  Android's
// Layer_fillRect_ncb @0x81D6E0 requires five, returns TJS_E_BADPARAMCOUNT, and
// the caller ignores that result before terminating the ancestor walk.
tjs_error callLayerFillRect4Like_0x6C7440(
    iTJSDispatch2 *layerObject,
    tjs_real width,
    tjs_real height);
// Player_emitRenderItem_requireLayer @0x6C6274 dispatches the normal five-arg
// form [Integer 0, Integer 0, Real width, Real height, Integer 0].
tjs_error callLayerFillRect5Like_0x6C4E28(
    iTJSDispatch2 *layerObject,
    tjs_real width,
    tjs_real height);
tjs_error callLayerOperateMeshLike_0x6C7440(
    iTJSDispatch2 *layerClassObject,
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
    iTJSDispatch2 *layerClassObject,
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
//   through the Layer class accessor with the render-layer as objthis, matching
//   sub_6C7440's L"operateRect" block.
tjs_error callLayerOperateRectLike_0x6C7440(
    iTJSDispatch2 *layerClassObject,
    iTJSDispatch2 *renderLayerObject,
    tjs_real destX,
    tjs_real destY,
    const tTJSVariant &sourceObject,
    tjs_real sourceWidth,
    tjs_real sourceHeight,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity);

// libkrkr2.so sub_6C7440 L"setClip" dispatch points (target work-layer v370):
//   - 0x6c78dc: argc=4 [left, top, width, height]  → set clip rect
//   - 0x6c7620: argc=0                              → reset clip (same method)
tjs_error callLayerSetClipLike_0x6C7440(iTJSDispatch2 *layerClassObject,
                                        iTJSDispatch2 *renderLayerObject,
                                        tjs_real left, tjs_real top,
                                        tjs_real width, tjs_real height);
tjs_error callLayerResetClipLike_0x6C7440(iTJSDispatch2 *layerClassObject,
                                          iTJSDispatch2 *renderLayerObject);
tjs_int callLayerPropGetIntLike_0x6C99B8(iTJSDispatch2 *layerClassObject,
                                         iTJSDispatch2 *layerObject,
                                         const tjs_char *memberName,
                                         tjs_uint32 *memberHint);

std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor);
iTJSDispatch2 *resolvePrimaryLayerObject(iTJSDispatch2 *layerTreeOwnerObject);
iTJSDispatch2 *resolveMainWindowOwnerObject();
iTJSDispatch2 *resolveMainWindowPrimaryLayerObject();
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
    tjs_int completionType);

tTVPBlendOperationMode resolveBlendOperationModeLike_0x6C7440(int rawBlendMode);

std::array<tTVPPointD, 3> buildAffineTrianglePoints(
    const std::array<float, 8> &corners,
    float xOffset,
    float yOffset);
std::vector<tTVPPointD> buildMeshPoints(
    const std::vector<detail::MeshPoint> &points,
    float xOffset,
    float yOffset);

motion::D3DAdaptor *ensureSharedD3DAdaptor(iTJSDispatch2 *targetLayerObject);

struct RenderClipRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
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
