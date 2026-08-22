#pragma once

#include "PlayerInternal.h"
#include "BitmapIntf.h"

namespace motion::internal::render_detail {

// Render-coordinate narrowing in all four reference builds rounds finite
// values toward zero and saturates invalid/out-of-range inputs. Keep that
// target-instruction boundary explicit so the WebAssembly port never reaches
// C++'s undefined floating-to-integer conversion cases.
tjs_int floatToSignedIntTowardZeroSaturated_guess(float value);

// Accurate-SLA phase owners are acquired from a call-local Variant CopyRef.
// AsObject retains only Object; the temporary Variant then dies immediately,
// releasing its Object/ObjThis pair before the returned raw owner crosses any
// script callback.
iTJSDispatch2 *retainObjectFromVariantCopy_guess(
    const tTJSVariant &value);

// Numeric core of Player's post-prepare pass. The native member walks only
// the sorted main pointer-vector; auxiliary items are deliberately excluded.
// Exposed here so the float-ordering and malformed-value boundaries can be
// tested without constructing a render target.
void applyPreparedRenderItemProjectionCore_guess(
    detail::PreparedRenderItemList &mainList,
    float cameraOffsetX,
    float cameraOffsetY,
    bool stereovisionActive,
    double stereovisionCameraX,
    double stereovisionCameraY,
    double stereovisionCameraZ);

#if defined(KRKR2_WASMTIME_HEADLESS)
extern "C" void TVPResetSoftwareAffineDiagnosticsForWasmtime();
#endif

bool getLayerClassDispatchVariant_guess(tTJSVariant &layerClassVar);
tjs_error callLayerOperateAffine_guess(
    iTJSDispatch2 *layerClassObject,
    iTJSDispatch2 *renderLayerObject,
    const tTVPPointD *points,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity);
// Source-level extractions of the primitive-dispatch blocks repeated in the
// four current canvas and accurate-SLA renderers. They are not standalone
// native function boundaries. Copy/setSize/fillRect use the source/buffer
// Layer instance as both receiver and objthis; target operate*/setClip calls
// instead use the Layer class receiver with the target Layer as objthis.
//
// Shared native helper used by all four renderer families. It returns an
// owning Array Variant and appends interleaved Real x/y values directly to the
// native Array Items deque; there is no per-index script PropSet boundary.
tTJSVariant buildMeshPointTJSArrayVariant_guess(
    const std::vector<detail::MeshPoint> &points,
    float xOffset, float yOffset);

// Submit the optional per-item geometry frame after the image primitive. Both
// style values remain Variants: Void participates in the native either-value
// gate and is still forwarded as an argument when the other style is set. The
// ordinary canvas callers use the default half-pixel offset; accurate SLA adds
// the negative clip origin as well.
void drawRenderItemFrame_guess(
    iTJSDispatch2 *layerClassObject,
    iTJSDispatch2 *renderLayerObject,
    const detail::PreparedRenderItem &item,
    const tTJSVariant &outline,
    const tTJSVariant &meshline,
    float xOffset = -0.5f,
    float yOffset = -0.5f);

// All four native render families derive Bezier cell counts with the same
// uint32 pipeline: source extents saturate to words before the wrapping
// multiply/add/divide sequence. The returned words are then interpreted as
// signed cell counts by the render API.
std::array<tjs_int, 2> renderBezierPatchCellDivisions_guess(
    tjs_int division, double sourceWidth, double sourceHeight);

// affineCopy (argc=14): [src, sx, sy, sw, sh, useMatrix=false,
//   x0, y0, x1, y1, x2, y2, type, clear]. Dispatched on the render-layer
//   instance in all four current renderers.
tjs_error callLayerAffineCopy_guess(
    iTJSDispatch2 *renderLayerObject,
    const tTVPPointD *points,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    tTVPBBStretchType type,
    bool clear);

tjs_error callLayerMeshCopy_guess(
    iTJSDispatch2 *renderLayerObject,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    const tTJSVariant &meshPointArray,
    tjs_int divx,
    tjs_int divy,
    tTVPBBStretchType type,
    bool clear);
tjs_error callLayerBezierPatchCopy_guess(
    iTJSDispatch2 *renderLayerObject,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    const tTJSVariant &meshPointArray,
    tjs_int divx,
    tjs_int divy,
    tTVPBBStretchType type,
    bool clear);

// Both the item-layer materialization and canvas paths dispatch setSize with
// two Real Variants.
tjs_error callLayerSetSizeReal_guess(
    iTJSDispatch2 *layerObject,
    tjs_real width,
    tjs_real height);

// The canvas ancestor tail deliberately calls Layer.fillRect with only four
// arguments [Integer 0, Real width, Real height, Integer 0]. The registered
// Layer method requires five, returns TJS_E_BADPARAMCOUNT, and the caller
// ignores that result before terminating the ancestor walk.
tjs_error callLayerFillRect4_guess(
    iTJSDispatch2 *layerObject,
    tjs_real width,
    tjs_real height);
// Item-layer materialization dispatches the normal five-argument form
// [Integer 0, Integer 0, Real width, Real height, Integer 0].
tjs_error callLayerFillRect5_guess(
    iTJSDispatch2 *layerObject,
    tjs_real width,
    tjs_real height);
tjs_error callLayerOperateMesh_guess(
    iTJSDispatch2 *layerClassObject,
    iTJSDispatch2 *renderLayerObject,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    const tTJSVariant &meshPointArray,
    tjs_int divx,
    tjs_int divy,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity,
    bool clear);
tjs_error callLayerOperateBezierPatch_guess(
    iTJSDispatch2 *layerClassObject,
    iTJSDispatch2 *renderLayerObject,
    const tTJSVariant &sourceObject,
    const tTVPRect &sourceRect,
    const tTJSVariant &meshPointArray,
    tjs_int divx,
    tjs_int divy,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity,
    bool clear);

// operateRect (argc=9): [dx, dy, src, sx, sy, sw, sh, mode, opa]. Dispatched
// through the Layer class accessor with the render-layer as objthis.
tjs_error callLayerOperateRect_guess(
    iTJSDispatch2 *layerClassObject,
    iTJSDispatch2 *renderLayerObject,
    tjs_real destX,
    tjs_real destY,
    const tTJSVariant &sourceObject,
    tjs_real sourceWidth,
    tjs_real sourceHeight,
    tTVPBlendOperationMode blendMode,
    tjs_int opacity);

// The same Layer.setClip member has two native call shapes: argc=4 with
// [left, top, width, height] sets the clip, while argc=0 resets it.
tjs_error callLayerSetClip_guess(iTJSDispatch2 *layerClassObject,
                                 iTJSDispatch2 *renderLayerObject,
                                 tjs_real left, tjs_real top,
                                 tjs_real width, tjs_real height);
tjs_error callLayerResetClip_guess(iTJSDispatch2 *layerClassObject,
                                   iTJSDispatch2 *renderLayerObject);
tjs_int callLayerPropGetInt_guess(iTJSDispatch2 *layerClassObject,
                                  iTJSDispatch2 *layerObject,
                                  const tjs_char *memberName,
                                  tjs_uint32 *memberHint);

// The workspace/post-draw dimension path operates on the caller's retained
// ncbPropAccessor. It first probes with MEMBERMUSTEXIST and the shared member
// hint, then performs a second ordinary GetValue through the same accessor.
tjs_int getInternalWorkspaceDimension_guess(
    ncbPropAccessor &object,
    const tjs_char *member,
    tjs_uint32 *hint);

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
bool setObjectIntProperty(iTJSDispatch2 *object, const tjs_char *name,
                          tjs_int value);
bool prepareLayerForRender(iTJSDispatch2 *layerObject,
                           int width,
                           int height,
                           tjs_uint32 clearColor);
bool shouldUseDirectRenderPath_guess(
    const motion::detail::PreparedRenderItem &item,
    tjs_int completionType);

tTVPBlendOperationMode resolveBlendOperationMode_guess(int rawBlendMode);

std::array<tTVPPointD, 3> buildAffineTrianglePoints(
    const std::array<float, 8> &corners,
    float xOffset,
    float yOffset);

motion::D3DAdaptor *ensureSharedD3DAdaptor();

struct RenderClipRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// Test-only entry into the private-GLL/D3D clip block. It exercises the same
// TU-local production helper and is not registered as a script member.
bool computeD3DClipForDifferentialTest_guess(
    const motion::detail::PreparedRenderItem &item,
    int canvasWidth,
    int canvasHeight,
    std::array<float, 4> &out);

bool computeRenderClipRect(
    const motion::detail::PreparedRenderItem &item,
    const std::array<float, 4> &targetClip,
    RenderClipRect &out,
    std::string *failureReason = nullptr);
bool computeRenderClipRect(
    const motion::detail::PreparedRenderItem &item,
    int renderWidth,
    int renderHeight,
    RenderClipRect &out,
    std::string *failureReason = nullptr);
bool isAccurateSlaRenderEnabled();

void clearLayerAlphaOutsideRect(iTJSDispatch2 *layerObject,
                                const tTVPRect &outerRect,
                                const tTVPRect &innerRect,
                                tTJSVariant &dispatchResult);
void applyMotionAlphaMask_guess(
    tTJSVariant dstLayerVariant,
    int dstX,
    int dstY,
    tTJSVariant srcLayerVariant,
    int srcX,
    int srcY,
    int width,
    int height,
    int threshold,
    int playerStencilType,
    int itemFlags);
// Core entered after a native caller has already constructed the two owning
// by-value Variant arguments. This split lets callers preserve CopyRef/reentry
// order without introducing a second pair of Variant copies.
void applyMotionAlphaMaskOwnedVariants_guess(
    const tTJSVariant &dstLayerVariant,
    int dstX,
    int dstY,
    const tTJSVariant &srcLayerVariant,
    int srcX,
    int srcY,
    int width,
    int height,
    int threshold,
    int playerStencilType,
    int itemFlags);
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
