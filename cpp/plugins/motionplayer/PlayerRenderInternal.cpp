// PlayerRenderInternal.cpp — shared render helpers moved from PlayerRender.cpp
// Split from PlayerRender.cpp for maintainability.
//
#include "PlayerRenderInternal.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "MotionTraceWeb.h"
#include "RenderManager.h"
#include "tjsArray.h"

#include <cstdint>
#include <cstring>

using namespace motion::internal;

#if defined(KRKR2_WASMTIME_HEADLESS)
extern "C" const char *TVPGetSoftwareAffinePathForWasmtime();
extern "C" const char *TVPGetSoftwareAffineRendererForWasmtime();
extern "C" int TVPGetSoftwareAffineAlphaBlendDReadyForWasmtime();
extern "C" int TVPGetSoftwareAffineTempFirstPixelValidForWasmtime();
extern "C" unsigned int TVPGetSoftwareAffineTempFirstPixelForWasmtime();
extern "C" int TVPGetSoftwareAffineTargetFirstPixelBeforeValidForWasmtime();
extern "C" unsigned int TVPGetSoftwareAffineTargetFirstPixelBeforeForWasmtime();
extern "C" int TVPGetSoftwareAffineTargetFirstPixelAfterValidForWasmtime();
extern "C" unsigned int TVPGetSoftwareAffineTargetFirstPixelAfterForWasmtime();
extern "C" int TVPGetSoftwareAffineAlphaBlendDProbeValidForWasmtime();
extern "C" unsigned int TVPGetSoftwareAffineAlphaBlendDProbePixelForWasmtime();
extern "C" int TVPGetSoftwareAffineAlphaBlendDCProbeValidForWasmtime();
extern "C" unsigned int TVPGetSoftwareAffineAlphaBlendDCProbePixelForWasmtime();
extern "C" int TVPGetSoftwareAffineAlphaBlendDPointsToCForWasmtime();
extern "C" int TVPGetSoftwareAffineRenderMethodOpacityForWasmtime();
extern "C" const char *TVPGetSoftwareAffineRenderMethodBranchForWasmtime();
#endif

namespace motion::internal::render_detail {

    namespace {
        motion::D3DAdaptor *g_sharedD3DAdaptor_guess = nullptr;

        void callLayerFillRectInteger_guess(
            iTJSDispatch2 *layerObject,
            const tTVPRect &rect,
            tTJSVariant &result) {
            tTJSVariant left(static_cast<tjs_int>(rect.left));
            tTJSVariant top(static_cast<tjs_int>(rect.top));
            tTJSVariant width(static_cast<tjs_int>(rect.get_width()));
            tTJSVariant height(static_cast<tjs_int>(rect.get_height()));
            tTJSVariant color(static_cast<tjs_int>(0));
            tTJSVariant *arguments[] = {
                &left, &top, &width, &height, &color,
            };
            (void)layerObject->FuncCall(
                0, TJS_W("fillRect"),
                &motion::detail::fillRectMemberHint_guess, &result, 5,
                arguments, layerObject);
        }

        void callLayerUpdateInteger_guess(
            iTJSDispatch2 *layerObject,
            const tTVPRect &rect,
            tTJSVariant &result) {
            tTJSVariant left(static_cast<tjs_int>(rect.left));
            tTJSVariant top(static_cast<tjs_int>(rect.top));
            tTJSVariant width(static_cast<tjs_int>(rect.get_width()));
            tTJSVariant height(static_cast<tjs_int>(rect.get_height()));
            tTJSVariant *arguments[] = {&left, &top, &width, &height};
            (void)layerObject->FuncCall(
                0, TJS_W("update"),
                &motion::detail::updateMemberHint_guess, &result, 4,
                arguments, layerObject);
        }
    }

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject);

    iTJSDispatch2 *retainObjectFromVariantCopy_guess(
        const tTJSVariant &value) {
        tTJSVariant valueCopy(value);
        return valueCopy.AsObject();
    }

    bool getLayerClassDispatchVariant_guess(tTJSVariant &layerClassVar) {
        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global) {
            return false;
        }
        const bool ok = TJS_SUCCEEDED(global->PropGet(
            0, TJS_W("Layer"), nullptr, &layerClassVar, global)) &&
            layerClassVar.Type() == tvtObject &&
            layerClassVar.AsObjectNoAddRef();
        global->Release();
        return ok;
    }

    tjs_error callLayerOperateAffine_guess(
        iTJSDispatch2 *layerClassObject,
        iTJSDispatch2 *renderLayerObject,
        const tTVPPointD *points,
        const tTJSVariant &sourceObject,
        const tTVPRect &sourceRect,
        tTVPBlendOperationMode blendMode,
        tjs_int opacity) {
        // This portable helper extracts a trusted inline renderer block. The
        // native block has no receiver/point/source admission branch: it copies
        // the source Variant as-is, materializes argv, and dispatches. Its
        // surrounding renderer has already established all pointer owners.
        tTJSVariant sourceArg(sourceObject);
        tTJSVariant srcLeft(sourceRect.left);
        tTJSVariant srcTop(sourceRect.top);
        tTJSVariant srcWidth(sourceRect.get_width());
        tTJSVariant srcHeight(sourceRect.get_height());
        tTJSVariant useAffineMatrix(false);
        tTJSVariant x0(points[0].x);
        tTJSVariant y0(points[0].y);
        tTJSVariant x1(points[1].x);
        tTJSVariant y1(points[1].y);
        tTJSVariant x2(points[2].x);
        tTJSVariant y2(points[2].y);
        tTJSVariant mode(static_cast<tjs_int32>(blendMode));
        tTJSVariant opa(static_cast<tjs_int32>(opacity));
        // The four current direct branches materialize this final type argument
        // as a literal Integer 0; completionType only participates in the gate.
        tTJSVariant stretchType(static_cast<tjs_int>(0));

        tTJSVariant *args[] = {
            &sourceArg, &srcLeft, &srcTop, &srcWidth, &srcHeight,
            &useAffineMatrix, &x0, &y0, &x1, &y1, &x2, &y2,
            &mode, &opa, &stretchType,
        };

        // The native dispatch uses the Layer class as receiver and passes the
        // render layer only as objthis.
        return layerClassObject->FuncCall(
            0, TJS_W("operateAffine"),
            &motion::detail::operateAffineMemberHint_guess, nullptr, 15,
            args, renderLayerObject);
    }

    tjs_error callLayerAffineCopy_guess(
        iTJSDispatch2 *renderLayerObject,
        const tTVPPointD *points,
        const tTJSVariant &sourceObject,
        const tTVPRect &sourceRect,
        tTVPBBStretchType type,
        bool clear) {
        // Four-reference affineCopy block (argc=14, points mode):
        // [src, sx, sy, sw, sh, useMatrix=false, x0, y0, x1, y1, x2, y2,
        //  type, clear]. Dispatched on the render-layer instance.
        tTJSVariant sourceArg(sourceObject);
        tTJSVariant srcLeft(sourceRect.left);
        tTJSVariant srcTop(sourceRect.top);
        tTJSVariant srcWidth(sourceRect.get_width());
        tTJSVariant srcHeight(sourceRect.get_height());
        tTJSVariant useAffineMatrix(false);
        tTJSVariant x0(points[0].x);
        tTJSVariant y0(points[0].y);
        tTJSVariant x1(points[1].x);
        tTJSVariant y1(points[1].y);
        tTJSVariant x2(points[2].x);
        tTJSVariant y2(points[2].y);
        tTJSVariant typeArg(static_cast<tjs_int32>(type));
        tTJSVariant clearArg(static_cast<tjs_int32>(clear ? 1 : 0));
        tTJSVariant *args[] = {
            &sourceArg, &srcLeft, &srcTop, &srcWidth, &srcHeight,
            &useAffineMatrix, &x0, &y0, &x1, &y1, &x2, &y2,
            &typeArg, &clearArg,
        };
        return renderLayerObject->FuncCall(
            0, TJS_W("affineCopy"),
            &motion::detail::affineCopyMemberHint_guess, nullptr, 14, args,
            renderLayerObject);
    }

    tjs_error callLayerOperateRect_guess(
        iTJSDispatch2 *layerClassObject,
        iTJSDispatch2 *renderLayerObject,
        tjs_real destX,
        tjs_real destY,
        const tTJSVariant &sourceObject,
        tjs_real sourceWidth,
        tjs_real sourceHeight,
        tTVPBlendOperationMode blendMode,
        tjs_int opacity) {
        // Four-reference operateRect block (argc=9):
        // [dx, dy, src, sx, sy, sw, sh, mode, opa]. Dispatched on the
        // Layer class accessor with the target render-layer as objthis. The
        // registered Layer.operateRect method resolves the source object's
        // main image internally.
        tTJSVariant dx(destX);
        tTJSVariant dy(destY);
        tTJSVariant sourceArg(sourceObject);
        tTJSVariant srcLeft(static_cast<tjs_int>(0));
        tTJSVariant srcTop(static_cast<tjs_int>(0));
        tTJSVariant srcWidth(sourceWidth);
        tTJSVariant srcHeight(sourceHeight);
        tTJSVariant mode(static_cast<tjs_int32>(blendMode));
        tTJSVariant opa(static_cast<tjs_int32>(opacity));
        tTJSVariant *args[] = {
            &dx, &dy, &sourceArg, &srcLeft, &srcTop, &srcWidth, &srcHeight,
            &mode, &opa,
        };
        return layerClassObject->FuncCall(
            0, TJS_W("operateRect"),
            &motion::detail::operateRectMemberHint_guess, nullptr, 9, args,
            renderLayerObject);
    }

    tjs_error callLayerSetSizeReal_guess(
        iTJSDispatch2 *layerObject,
        tjs_real width,
        tjs_real height) {
        // Like the image-transfer helpers above, these basic Layer calls are
        // extracts of trusted inline blocks. Their enclosing renderer owns the
        // receiver; the reference block does not translate a broken owner into
        // a synthetic HRESULT or zero value.
        tTJSVariant widthArg(width);
        tTJSVariant heightArg(height);
        tTJSVariant *args[] = {&widthArg, &heightArg};
        return layerObject->FuncCall(
            0, TJS_W("setSize"), &motion::detail::setSizeMemberHint_guess,
            nullptr, 2, args, layerObject);
    }

    tjs_error callLayerFillRect4_guess(
        iTJSDispatch2 *layerObject,
        tjs_real width,
        tjs_real height) {
        tTJSVariant zeroX(static_cast<tjs_int>(0));
        tTJSVariant widthArg(width);
        tTJSVariant heightArg(height);
        tTJSVariant zeroColor(static_cast<tjs_int>(0));
        tTJSVariant *args[] = {
            &zeroX, &widthArg, &heightArg, &zeroColor,
        };
        return layerObject->FuncCall(
            0, TJS_W("fillRect"), &motion::detail::fillRectMemberHint_guess,
            nullptr, 4, args, layerObject);
    }

    tjs_error callLayerFillRect5_guess(
        iTJSDispatch2 *layerObject,
        tjs_real width,
        tjs_real height) {
        tTJSVariant zeroX(static_cast<tjs_int>(0));
        tTJSVariant zeroY(static_cast<tjs_int>(0));
        tTJSVariant widthArg(width);
        tTJSVariant heightArg(height);
        tTJSVariant zeroColor(static_cast<tjs_int>(0));
        tTJSVariant *args[] = {
            &zeroX, &zeroY, &widthArg, &heightArg, &zeroColor,
        };
        return layerObject->FuncCall(
            0, TJS_W("fillRect"), &motion::detail::fillRectMemberHint_guess,
            nullptr, 5, args, layerObject);
    }

    tjs_error callLayerSetClip_guess(
        iTJSDispatch2 *layerClassObject,
        iTJSDispatch2 *renderLayerObject,
        tjs_real left,
        tjs_real top,
        tjs_real width,
        tjs_real height) {
        // The viewport branch dispatches Layer.setClip with
        // argv=[left, top, width, height]. All four arguments are type-5 Real
        // Variants; integer conversion belongs to the registered Layer method
        // boundary and must not happen here.
        tTJSVariant l(left);
        tTJSVariant t(top);
        tTJSVariant w(width);
        tTJSVariant h(height);
        tTJSVariant *args[] = {&l, &t, &w, &h};
        return layerClassObject->FuncCall(
            0, TJS_W("setClip"), &motion::detail::setClipMemberHint_guess,
            nullptr, 4, args, renderLayerObject);
    }

    tjs_error callLayerResetClip_guess(
        iTJSDispatch2 *layerClassObject,
        iTJSDispatch2 *renderLayerObject) {
        // The same Layer.setClip member is dispatched with argc=0 here and at
        // the post-walk reset. numparams==0 routes to ResetClip().
        return layerClassObject->FuncCall(
            0, TJS_W("setClip"), &motion::detail::setClipMemberHint_guess,
            nullptr, 0, nullptr, renderLayerObject);
    }

    tjs_int callLayerPropGetInt_guess(
        iTJSDispatch2 *layerClassObject,
        iTJSDispatch2 *layerObject,
        const tjs_char *memberName,
        tjs_uint32 *memberHint) {
        tTJSVariant value;
        (void)layerClassObject->PropGet(
            0, memberName, memberHint, &value, layerObject);
        return static_cast<tjs_int>(value.AsInteger());
    }

    tTJSVariant buildMeshPointTJSArrayVariant_guess(
        const std::vector<detail::MeshPoint> &points,
        float xOffset, float yOffset) {
        // The native helper owns a fresh Array Variant plus a borrowed pointer
        // to tTJSArrayNI::Items. Each coordinate is added in float with the
        // offset as the left operand, promoted to Real, then directly appended.
        // A non-empty input naturally dereferences a missing Items pointer; no
        // script PropSet or friendly NativeInstanceSupport fallback exists.
        auto array = motion::detail::createTJSArrayWithItems_guess();
        for(const auto &point : points) {
            array.items->emplace_back(
                static_cast<tjs_real>(xOffset + point.x));
            array.items->emplace_back(
                static_cast<tjs_real>(yOffset + point.y));
        }
        // Force the native local-owner -> returned-owner copy boundary before
        // the helper-local Array Variant is destroyed.
        return static_cast<const tTJSVariant &>(array.value);
    }

    void drawRenderItemFrame_guess(
        iTJSDispatch2 *layerClassObject,
        iTJSDispatch2 *renderLayerObject,
        const detail::PreparedRenderItem &item,
        const tTJSVariant &outline,
        const tTJSVariant &meshline,
        float xOffset,
        float yOffset) {
        if(!layerClassObject || !renderLayerObject ||
           (outline.Type() == tvtVoid && meshline.Type() == tvtVoid)) {
            return;
        }

        if(item.meshType == 0) {
            for(std::size_t edge = 0; edge < 4; ++edge) {
                const std::size_t next = (edge + 1) & 3;
                tTJSVariant outlineArg(outline);
                tTJSVariant x0(static_cast<tjs_real>(
                    item.corners[edge * 2] + xOffset));
                tTJSVariant y0(static_cast<tjs_real>(
                    item.corners[edge * 2 + 1] + yOffset));
                tTJSVariant x1(static_cast<tjs_real>(
                    item.corners[next * 2] + xOffset));
                tTJSVariant y1(static_cast<tjs_real>(
                    item.corners[next * 2 + 1] + yOffset));
                tTJSVariant *args[] = {
                    &outlineArg, &x0, &y0, &x1, &y1,
                };
                (void)layerClassObject->FuncCall(
                    0, TJS_W("drawLine"),
                    &motion::detail::drawLineMemberHint_guess, nullptr, 5,
                    args, renderLayerObject);
            }
            return;
        }

        const std::vector<detail::MeshPoint> *points = nullptr;
        if(item.meshType == 1) {
            points = &item.meshPoints;
        } else if(item.meshType == 2) {
            points = &item.commandCompositeMeshPoints;
        } else {
            return;
        }

        tTJSVariant pointArrayArg = buildMeshPointTJSArrayVariant_guess(
            *points, xOffset, yOffset);
        tTJSVariant outlineArg(outline);
        tTJSVariant meshlineArg(meshline);

        if(item.meshType == 2) {
            tTJSVariant divX(static_cast<tjs_int>(item.meshDivX));
            tTJSVariant divY(static_cast<tjs_int>(item.meshDivY));
            tTJSVariant *args[] = {
                &outlineArg, &meshlineArg, &pointArrayArg, &divX, &divY,
            };
            (void)layerClassObject->FuncCall(
                0, TJS_W("drawMeshFrame"),
                &motion::detail::drawMeshFrameMemberHint_guess, nullptr, 5,
                args, renderLayerObject);
            return;
        }

        if(meshline.Type() != tvtVoid) {
            const auto cellDivisions =
                renderBezierPatchCellDivisions_guess(
                    item.commandPatchDivision,
                    item.sourceState->width,
                    item.sourceState->height);
            tTJSVariant divX(cellDivisions[0]);
            tTJSVariant divY(cellDivisions[1]);
            tTJSVariant *args[] = {
                &outlineArg, &meshlineArg, &pointArrayArg, &divX, &divY,
            };
            (void)layerClassObject->FuncCall(
                0, TJS_W("drawBezierPatchMeshFrame"),
                &motion::detail::drawBezierPatchMeshFrameMemberHint_guess,
                nullptr, 5, args, renderLayerObject);
            return;
        }

        tTJSVariant *args[] = {
            &outlineArg, &meshlineArg, &pointArrayArg,
        };
        (void)layerClassObject->FuncCall(
            0, TJS_W("drawBezierPatchFrame"),
            &motion::detail::drawBezierPatchFrameMemberHint_guess, nullptr, 3,
            args, renderLayerObject);
    }

    std::array<tjs_int, 2> renderBezierPatchCellDivisions_guess(
        tjs_int division,
        double sourceWidth,
        double sourceHeight) {
        static_assert(sizeof(tjs_int) == sizeof(std::uint32_t));

        const std::uint32_t width =
            doubleToUnsignedIntTowardZeroSaturated_guess(sourceWidth);
        const std::uint32_t height =
            doubleToUnsignedIntTowardZeroSaturated_guess(sourceHeight);
        const std::uint32_t denominator = width + height;
        const std::uint32_t divisionWord =
            static_cast<std::uint32_t>(division);
        const std::uint32_t numerator = divisionWord * width;

        // AArch64 performs this inline and yields zero for a zero divisor.
        // ARMv7 delegates that boundary to an external runtime helper, so the
        // deterministic Web profile follows the behavior contained in both
        // AArch64 reference plugins instead of allowing wasm division to trap.
        const std::uint32_t split = denominator != 0u
            ? numerator / denominator
            : 0u;

        const auto wordToSignedCellCount = [](std::uint32_t word) {
            std::int32_t signedWord;
            std::memcpy(&signedWord, &word, sizeof(signedWord));
            return static_cast<tjs_int>(signedWord);
        };
        return {
            wordToSignedCellCount(split + 1u),
            wordToSignedCellCount(divisionWord - split + 1u),
        };
    }

    // Common packer for the copy-family (meshCopy/bezierPatchCopy, argc=10) and
    // operate-family (operateMesh/operateBezierPatch, argc=11) primitives. The
    // Argument layout matches the four current renderers' packed FuncCall
    // arrays exactly:
    //   copy:    [src, sx=0, sy=0, sw, sh, points, divx, divy, type, clear]
    //   operate: [src, sx=0, sy=0, sw, sh, points, divx, divy, mode, opa, clear]
    static tjs_error callLayerMeshFamily_guess(
        const tjs_char *methodName,
        tjs_uint32 *memberHint,
        iTJSDispatch2 *dispatchObject,
        iTJSDispatch2 *renderLayerObject,
        const tTJSVariant &sourceObject,
        const tTVPRect &sourceRect,
        const tTJSVariant &meshPointArray,
        tjs_int divx,
        tjs_int divy,
        bool isOperate,
        tTVPBlendOperationMode blendMode,
        tjs_int opacity,
        tTVPBBStretchType type,
        bool clear) {
        tTJSVariant sourceArg(sourceObject);
        tTJSVariant srcLeft(sourceRect.left);
        tTJSVariant srcTop(sourceRect.top);
        tTJSVariant srcWidth(sourceRect.get_width());
        tTJSVariant srcHeight(sourceRect.get_height());
        tTJSVariant divxArg(divx);
        tTJSVariant divyArg(divy);

        if(isOperate) {
            // operateMesh / operateBezierPatch: argc=11
            tTJSVariant modeArg(static_cast<tjs_int32>(blendMode));
            tTJSVariant opaArg(static_cast<tjs_int32>(opacity));
            tTJSVariant clearArg(static_cast<tjs_int32>(clear ? 1 : 0));
            tTJSVariant *args[] = {
                &sourceArg, &srcLeft, &srcTop, &srcWidth, &srcHeight,
                const_cast<tTJSVariant *>(&meshPointArray),
                &divxArg, &divyArg, &modeArg, &opaArg, &clearArg,
            };
            return dispatchObject->FuncCall(0, methodName, memberHint, nullptr,
                                            11, args, renderLayerObject);
        }

        // meshCopy / bezierPatchCopy: argc=10
        tTJSVariant typeArg(static_cast<tjs_int32>(type));
        tTJSVariant clearArg(static_cast<tjs_int32>(clear ? 1 : 0));
        tTJSVariant *args[] = {
            &sourceArg, &srcLeft, &srcTop, &srcWidth, &srcHeight,
            const_cast<tTJSVariant *>(&meshPointArray),
            &divxArg, &divyArg, &typeArg, &clearArg,
        };
        return dispatchObject->FuncCall(0, methodName, memberHint, nullptr, 10,
                                        args, renderLayerObject);
    }

    tjs_error callLayerMeshCopy_guess(
        iTJSDispatch2 *renderLayerObject, const tTJSVariant &sourceObject,
        const tTVPRect &sourceRect, const tTJSVariant &meshPointArray, tjs_int divx,
        tjs_int divy, tTVPBBStretchType type, bool clear) {
        return callLayerMeshFamily_guess(
            TJS_W("meshCopy"), &motion::detail::meshCopyMemberHint_guess,
            renderLayerObject, renderLayerObject, sourceObject, sourceRect,
            meshPointArray, divx, divy, false, omAlpha, 255, type, clear);
    }

    tjs_error callLayerBezierPatchCopy_guess(
        iTJSDispatch2 *renderLayerObject, const tTJSVariant &sourceObject,
        const tTVPRect &sourceRect, const tTJSVariant &meshPointArray, tjs_int divx,
        tjs_int divy, tTVPBBStretchType type, bool clear) {
        return callLayerMeshFamily_guess(
            TJS_W("bezierPatchCopy"),
            &motion::detail::bezierPatchCopyMemberHint_guess,
            renderLayerObject, renderLayerObject, sourceObject, sourceRect,
            meshPointArray, divx, divy, false, omAlpha, 255, type, clear);
    }

    tjs_error callLayerOperateMesh_guess(
        iTJSDispatch2 *layerClassObject,
        iTJSDispatch2 *renderLayerObject, const tTJSVariant &sourceObject,
        const tTVPRect &sourceRect, const tTJSVariant &meshPointArray, tjs_int divx,
        tjs_int divy, tTVPBlendOperationMode blendMode, tjs_int opacity,
        bool clear) {
        return callLayerMeshFamily_guess(
            TJS_W("operateMesh"),
            &motion::detail::operateMeshMemberHint_guess, layerClassObject,
            renderLayerObject, sourceObject, sourceRect, meshPointArray,
            divx, divy, true, blendMode, opacity, stNearest, clear);
    }

    tjs_error callLayerOperateBezierPatch_guess(
        iTJSDispatch2 *layerClassObject,
        iTJSDispatch2 *renderLayerObject, const tTJSVariant &sourceObject,
        const tTVPRect &sourceRect, const tTJSVariant &meshPointArray, tjs_int divx,
        tjs_int divy, tTVPBlendOperationMode blendMode, tjs_int opacity,
        bool clear) {
        return callLayerMeshFamily_guess(
            TJS_W("operateBezierPatch"),
            &motion::detail::operateBezierPatchMemberHint_guess,
            layerClassObject, renderLayerObject, sourceObject, sourceRect,
            meshPointArray, divx, divy, true, blendMode, opacity, stNearest,
            clear);
    }

    std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor) {
        return {
            static_cast<int>(packedColor & 0xFFu),
            static_cast<int>((packedColor >> 8) & 0xFFu),
            static_cast<int>((packedColor >> 16) & 0xFFu),
            static_cast<int>((packedColor >> 24) & 0xFFu),
        };
    }

    iTJSDispatch2 *resolvePrimaryLayerObject(iTJSDispatch2 *layerTreeOwnerObject) {
        if(!layerTreeOwnerObject) {
            return nullptr;
        }

        tTJSVariant ownerVar(layerTreeOwnerObject, layerTreeOwnerObject);
        tTJSVariant primaryVar;
        if(!getObjectProperty(ownerVar, TJS_W("primaryLayer"), primaryVar) ||
           primaryVar.Type() != tvtObject || !primaryVar.AsObjectNoAddRef()) {
            return nullptr;
        }

        if(auto *resolved = tryResolveLayerDispatch(primaryVar)) {
            return resolved;
        }
        return primaryVar.AsObjectNoAddRef();
    }

    iTJSDispatch2 *resolveMainWindowOwnerObject() {
        if(!TVPMainWindow) {
            return nullptr;
        }
        auto *owner = TVPMainWindow->GetOwnerNoAddRef();
        if(owner) {
            return owner;
        }

        iTJSDispatch2 *global = TVPGetScriptDispatch();
        if(!global) {
            return nullptr;
        }

        tTJSVariant windowClassVar;
        tTJSVariant mainWindowVar;
        iTJSDispatch2 *resolved = nullptr;
        if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("Window"), nullptr,
                                         &windowClassVar, global)) &&
           windowClassVar.Type() == tvtObject &&
           windowClassVar.AsObjectNoAddRef() &&
           TJS_SUCCEEDED(windowClassVar.AsObjectNoAddRef()->PropGet(
               0, TJS_W("mainWindow"), nullptr, &mainWindowVar,
               windowClassVar.AsObjectNoAddRef())) &&
           mainWindowVar.Type() == tvtObject &&
           mainWindowVar.AsObjectNoAddRef()) {
            resolved = mainWindowVar.AsObjectNoAddRef();
        }

        global->Release();
        return resolved;
    }

    iTJSDispatch2 *resolveMainWindowPrimaryLayerObject() {
        return resolvePrimaryLayerObject(resolveMainWindowOwnerObject());
    }

    iTJSDispatch2 *createLayerObject(iTJSDispatch2 *layerTreeOwnerObject,
                                     iTJSDispatch2 *parentLayerObject) {
        if(!layerTreeOwnerObject) {
            return nullptr;
        }

        tTJSVariant layerClassVar;
        iTJSDispatch2 *created = nullptr;
        const bool haveLayerClass =
            getLayerClassDispatchVariant_guess(layerClassVar);
        if(haveLayerClass) {
            tTJSVariant ownerVar(layerTreeOwnerObject, layerTreeOwnerObject);
            tTJSVariant parentVar =
                parentLayerObject ? tTJSVariant(parentLayerObject, parentLayerObject)
                                  : tTJSVariant();
            tTJSVariant *args[] = { &ownerVar, &parentVar };
            if(TJS_FAILED(layerClassVar.AsObjectNoAddRef()->CreateNew(
                   0, nullptr, nullptr, &created, 2, args,
                   layerClassVar.AsObjectNoAddRef()))) {
                created = nullptr;
            }
        }

        return created;
    }

    bool configureReusableLayerObject(iTJSDispatch2 *layerObject,
                                      iTJSDispatch2 *parentLayerObject,
                                      tTVPLayerType layerType,
                                      bool visible,
                                      bool absoluteOrderMode) {
        auto *layer = resolveNativeLayer(layerObject);
        if(!layer) {
            return false;
        }

        if(parentLayerObject) {
            if(auto *parentLayer = resolveNativeLayer(parentLayerObject);
               parentLayer && layer->GetParent() != parentLayer) {
                layer->SetParent(parentLayer);
            }
        }

        layer->SetType(layerType);
        layer->SetAbsoluteOrderMode(absoluteOrderMode);
        layer->SetVisible(visible);
        return true;
    }

    iTJSDispatch2 *ensureReusableLayerObject(tTJSVariant &slot,
                                             iTJSDispatch2 *layerTreeOwnerObject,
                                             iTJSDispatch2 *parentLayerObject,
                                             tTVPLayerType layerType,
                                             bool visible,
                                             bool absoluteOrderMode) {
        if(!parentLayerObject && layerTreeOwnerObject) {
            parentLayerObject = resolvePrimaryLayerObject(layerTreeOwnerObject);
        }

        iTJSDispatch2 *layerObject =
            slot.Type() == tvtObject ? slot.AsObjectNoAddRef() : nullptr;
        if(!layerObject) {
            if(!layerTreeOwnerObject) {
                return nullptr;
            }
            layerObject = createLayerObject(layerTreeOwnerObject, parentLayerObject);
            if(!layerObject) {
                return nullptr;
            }
            slot = tTJSVariant(layerObject, layerObject);
            layerObject->Release();
            layerObject = slot.AsObjectNoAddRef();
        }

        if(!configureReusableLayerObject(layerObject, parentLayerObject,
                                         layerType, visible,
                                         absoluteOrderMode)) {
            return nullptr;
        }
        return layerObject;
    }

    tTJSNI_BaseLayer *resolveNativeLayer(iTJSDispatch2 *layerObject) {
        if(!layerObject) {
            return nullptr;
        }
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(layerObject->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return nullptr;
        }
        return layer;
    }

    bool setObjectIntProperty(iTJSDispatch2 *object, const tjs_char *name,
                              tjs_int value) {
        if(!object) {
            return false;
        }
        tTJSVariant var(value);
        return TJS_SUCCEEDED(
            object->PropSet(TJS_MEMBERENSURE, name, nullptr, &var, object));
    }

    bool prepareLayerForRender(iTJSDispatch2 *layerObject,
                               int width, int height,
                               tjs_uint32 clearColor) {
        auto *layer = resolveNativeLayer(layerObject);
        if(!layer || width <= 0 || height <= 0) {
            return false;
        }

        if(!layer->GetHasImage()) {
            layer->SetHasImage(true);
        }
        layer->SetImageSize(static_cast<tjs_uint>(width),
                            static_cast<tjs_uint>(height));
        layer->SetSize(width, height);
        layer->SetClip(0, 0, width, height);
        tTVPRect rect(0, 0, width, height);
        layer->FillRect(rect, clearColor);
        return true;
    }

    tTVPBlendOperationMode resolveBlendOperationMode_guess(
        int rawBlendMode) {
        // The four current renderers do not pass the raw item blend flag
        // through to operateRect. They first map the low 4 bits to the final TVP blend
        // operation mode: 1->0xE, 2/5->0xF, 3->0x10, 4->0x11, and the raw 0 /
        // default path ultimately composites with mode 2 in the common case.
        switch(rawBlendMode & 0x0F) {
            case 1:
                return omPsAdditive;       // 0xE
            case 2:
            case 5:
                return omPsSubtractive;    // 0xF
            case 3:
                return omPsMultiplicative; // 0x10
            case 4:
                return omPsScreen;         // 0x11
            case 0:
            default:
                return omAlpha;            // 0x2
        }
    }

    bool shouldUseDirectRenderPath_guess(
        const motion::detail::PreparedRenderItem &item,
        tjs_int completionType) {
        const unsigned lowNibble =
            static_cast<unsigned>(item.blendMode) & 0x0Fu;
        return completionType == 0 && item.parentItem == nullptr &&
            (lowNibble == 0u || lowNibble > 5u);
    }

    std::array<tTVPPointD, 3> buildAffineTrianglePoints(
        const std::array<float, 8> &corners,
        float xOffset,
        float yOffset) {
        return {{
            { static_cast<double>(corners[0] + xOffset),
              static_cast<double>(corners[1] + yOffset) },
            { static_cast<double>(corners[2] + xOffset),
              static_cast<double>(corners[3] + yOffset) },
            { static_cast<double>(corners[6] + xOffset),
              static_cast<double>(corners[7] + yOffset) },
        }};
    }

    motion::D3DAdaptor *ensureSharedD3DAdaptor() {
        // All four references use a zero-initialized raw process-global slot.
        // It is neither guarded nor registered for destruction: the first
        // successful constructor publishes it and the process retains it.
        if(!g_sharedD3DAdaptor_guess) {
            // The dimensions are sampled before allocation.  The allocating
            // constructor then obtains and retains the main Window owner; a
            // constructor failure leaves the slot null so a later draw retries.
            const int width = static_cast<int>(TVPMainWindow->GetWidth());
            const int height = static_cast<int>(TVPMainWindow->GetHeight());
            g_sharedD3DAdaptor_guess = new motion::D3DAdaptor(
                TVPMainWindow->GetOwnerNoAddRef(), width, height,
                width / 2, height / 2);
        }
        return g_sharedD3DAdaptor_guess;
    }

    bool computeRenderClipRect(
        const motion::detail::PreparedRenderItem &entry,
        int canvasWidth,
        int canvasHeight,
        RenderClipRect &out,
        std::string *failureReason) {
        return computeRenderClipRect(
            entry,
            {0.0f, 0.0f,
             static_cast<float>(canvasWidth),
             static_cast<float>(canvasHeight)},
            out, failureReason);
    }

    bool computeRenderClipRect(
        const motion::detail::PreparedRenderItem &entry,
        const std::array<float, 4> &targetClip,
        RenderClipRect &out,
        std::string *failureReason) {
        // The current canvas pass first intersects the four float paint-box
        // values with the target/camera rect. Only a valid
        // viewport is rounded; without one, fractional paint-box edges remain
        // fractional in the resulting clip rectangle.
        const float cameraLeft = targetClip[0];
        const float cameraTop = targetClip[1];
        const float cameraRight = targetClip[2];
        const float cameraBottom = targetClip[3];
        float clipLeft = cameraLeft < entry.paintBox[0]
            ? entry.paintBox[0]
            : cameraLeft;
        float clipTop = cameraTop < entry.paintBox[1]
            ? entry.paintBox[1]
            : cameraTop;
        float clipRight = entry.paintBox[2] < cameraRight
            ? entry.paintBox[2]
            : cameraRight;
        float clipBottom = entry.paintBox[3] < cameraBottom
            ? entry.paintBox[3]
            : cameraBottom;

        if(entry.viewport[2] >= entry.viewport[0] &&
           entry.viewport[3] >= entry.viewport[1]) {
            const float viewportLeft = floorf(entry.viewport[0]);
            const float viewportTop = floorf(entry.viewport[1]);
            const float viewportRight = ceilf(entry.viewport[2]);
            const float viewportBottom = ceilf(entry.viewport[3]);
            clipLeft = viewportLeft < clipLeft ? clipLeft : viewportLeft;
            clipTop = viewportTop < clipTop ? clipTop : viewportTop;
            clipRight = clipRight < viewportRight ? clipRight : viewportRight;
            clipBottom = clipBottom < viewportBottom ? clipBottom
                                                     : viewportBottom;
        }

        if(!(clipLeft < clipRight && clipTop < clipBottom)) {
            if(failureReason) {
                *failureReason = fmt::format(
                    "invalid_intersection paintBox=[{:.3f},{:.3f},{:.3f},{:.3f}] viewport={}",
                    entry.paintBox[0], entry.paintBox[1], entry.paintBox[2],
                    entry.paintBox[3],
                    entry.hasViewport
                        ? fmt::format("[{:.3f},{:.3f},{:.3f},{:.3f}]",
                                      entry.viewport[0], entry.viewport[1],
                                      entry.viewport[2], entry.viewport[3])
                        : std::string("<invalid default>"));
            }
            return false;
        }

        out.left = clipLeft;
        out.top = clipTop;
        out.right = clipRight;
        out.bottom = clipBottom;
        if(failureReason) {
            failureReason->clear();
        }
        return out.left < out.right && out.top < out.bottom;
    }

    bool isAccurateSlaRenderEnabled() {
        if(TVPIsSoftwareRenderManager()) {
            return true;
        }
        return IndividualConfigManager::GetInstance()->GetValue<bool>(
            "ogl_accurate_render", false);
    }

    void clearLayerAlphaOutsideRect(iTJSDispatch2 *layerObject,
                                    const tTVPRect &outerRect,
                                    const tTVPRect &innerRect,
                                    tTJSVariant &dispatchResult) {
        auto clearMask = [&](const tTVPRect &rect) {
            if(rect.left < rect.right && rect.top < rect.bottom) {
                // Each strip is an observable script-visible fillRect call
                // with five Integer Variants and the retained destination as
                // objthis. The same result Variant is reused across calls.
                callLayerFillRectInteger_guess(
                    layerObject, rect, dispatchResult);
            }
        };

        clearMask(tTVPRect(outerRect.left, outerRect.top,
                           innerRect.left, outerRect.bottom));
        clearMask(tTVPRect(innerRect.right, outerRect.top,
                           outerRect.right, outerRect.bottom));
        clearMask(tTVPRect(std::max(outerRect.left, innerRect.left),
                           outerRect.top,
                           std::min(outerRect.right, innerRect.right),
                           innerRect.top));
        clearMask(tTVPRect(std::max(outerRect.left, innerRect.left),
                           innerRect.bottom,
                           std::min(outerRect.right, innerRect.right),
                           outerRect.bottom));
    }

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
        int itemFlags) {
        // The native ABI's two owning call-argument Variants already exist on
        // entry. It copies the destination once more into ncbPropAccessor,
        // whose AsObject owner remains retained through the compositor tail.
        tTJSVariant destinationCopy(dstLayerVariant);
        ncbPropAccessor destination(destinationCopy);
        destinationCopy.Clear();

        const tjs_int clipLeft =
            destination.getIntValue(TJS_W("clipLeft"));
        const tjs_int clipTop =
            destination.getIntValue(TJS_W("clipTop"));
        const tjs_int clipWidth =
            destination.getIntValue(TJS_W("clipWidth"));
        const tjs_int clipHeight =
            destination.getIntValue(TJS_W("clipHeight"));
        const tTVPRect dstClip(
            clipLeft, clipTop, clipLeft + clipWidth,
            clipTop + clipHeight);
        tTJSVariant dispatchResult;

        if(dstClip.left > dstX) {
            srcX += dstClip.left - dstX;
            width -= dstClip.left - dstX;
            dstX = dstClip.left;
        }
        if(dstClip.top > dstY) {
            srcY += dstClip.top - dstY;
            height -= dstClip.top - dstY;
            dstY = dstClip.top;
        }
        if(dstX + width > dstClip.right) {
            width = dstClip.right - dstX;
        }
        if(dstY + height > dstClip.bottom) {
            height = dstClip.bottom - dstY;
        }

        const tTVPRect overlapRect(dstX, dstY, dstX + width, dstY + height);

        if(width <= 0 || height <= 0) {
            // Empty intersection never touches the source layer. All four
            // references dispatch the complete destination clip only for op 1.
            if(itemFlags == 1) {
                callLayerFillRectInteger_guess(
                    destination.GetDispatch(), dstClip, dispatchResult);
            }
            return;
        }

        // Native delays both strict Layer conversions until overlap is known
        // non-empty, converts source before destination, and then trusts the
        // native instances/images. Invalid internal state fails naturally.
        auto *srcLayer = resolveNativeLayer(
            srcLayerVariant.AsObjectNoAddRef());
        auto *dstLayer = resolveNativeLayer(
            dstLayerVariant.AsObjectNoAddRef());
        auto *srcTexture = srcLayer->GetMainImage()->GetTexture();
        const tTVPRect srcRect(
            srcX, srcY, srcX + width, srcY + height);
        tRenderTexRectArray::Element sourceTextures[] = {
            tRenderTexRectArray::Element(srcTexture, srcRect),
        };

        // This setup intentionally precedes mask-mode/op validation. In all
        // four references an unsupported non-empty operation still exposes the
        // same bitmap side effects (software destination independence, or GPU
        // writable-target acquisition) before falling through to Layer.update.
        const std::uint8_t *srcPixels = nullptr;
        std::uint8_t *dstPixels = nullptr;
        int srcPitch = 0;
        int dstPitch = 0;
        iTVPTexture2D *dstReferenceTexture = nullptr;
        iTVPTexture2D *dstRenderTexture = nullptr;
        if(TVPIsSoftwareRenderManager()) {
            srcPixels = static_cast<const std::uint8_t *>(
                srcTexture->GetPixelData());
            dstPixels = static_cast<std::uint8_t *>(
                dstLayer->GetMainImagePixelBufferForWrite());
            srcPitch = srcTexture->GetPitch();
            dstPitch = dstLayer->GetMainImagePixelBufferPitch();
            srcPixels += srcY * srcPitch + srcX * 4;
            dstPixels += dstY * dstPitch + dstX * 4;
        } else {
            dstReferenceTexture = dstLayer->GetMainImage()->GetTexture();
            dstRenderTexture = dstLayer->GetMainImage()->GetTextureForRender(
                true, &overlapRect);
        }

        if(playerStencilType == 1) {
            if(itemFlags == 5 || itemFlags == 6) {
                if(TVPIsSoftwareRenderManager()) {
                    auto *dstRow = dstPixels;
                    const auto *srcRow = srcPixels;
                    for(int y = 0; y < height; ++y) {
                        for(int x = 0; x < width; ++x) {
                            const auto srcAlpha =
                                static_cast<int>(srcRow[x * 4 + 3]);
                            auto &dstAlpha = dstRow[x * 4 + 3];
                            dstAlpha = static_cast<std::uint8_t>(
                                srcAlpha +
                                ((255 - srcAlpha) *
                                 static_cast<int>(dstAlpha)) / 255);
                        }
                        srcRow += srcPitch;
                        dstRow += dstPitch;
                    }
                } else {
                    static std::uint32_t methodHint = 0;
                    static iTVPRenderMethod *method =
                        TVPGetRenderManager()
                            ->GetOrCompileRenderMethod(
                                "AddAlphaMask", &methodHint,
                                "void main() { gl_FragColor = texture2D(tex0, v_texCoord0); }\n",
                                1, 0)
                            ->SetBlendFuncSeparate(
                                GL_FUNC_ADD, GL_ZERO, GL_ONE, GL_ONE,
                                GL_ONE_MINUS_SRC_ALPHA);
                    TVPGetRenderManager()->OperateRect(
                        method, dstRenderTexture, dstReferenceTexture,
                        overlapRect, tRenderTexRectArray(sourceTextures));
                }
            } else if(itemFlags == 2) {
                if(TVPIsSoftwareRenderManager()) {
                    auto *dstRow = dstPixels;
                    const auto *srcRow = srcPixels;
                    for(int y = 0; y < height; ++y) {
                        for(int x = 0; x < width; ++x) {
                            const auto srcAlpha =
                                static_cast<int>(srcRow[x * 4 + 3]);
                            auto &dstAlpha = dstRow[x * 4 + 3];
                            dstAlpha = static_cast<std::uint8_t>(
                                ((255 - srcAlpha) *
                                 static_cast<int>(dstAlpha)) / 255);
                        }
                        srcRow += srcPitch;
                        dstRow += dstPitch;
                    }
                } else {
                    static std::uint32_t methodHint = 0;
                    static iTVPRenderMethod *method =
                        TVPGetRenderManager()
                            ->GetOrCompileRenderMethod(
                                "AlphaMaskRev", &methodHint,
                                "void main() { gl_FragColor = texture2D(tex0, v_texCoord0); }\n",
                                1, 0)
                            ->SetBlendFuncSeparate(
                                GL_FUNC_ADD, GL_ZERO, GL_ONE, GL_ZERO,
                                GL_ONE_MINUS_SRC_ALPHA);
                    TVPGetRenderManager()->OperateRect(
                        method, dstRenderTexture, dstReferenceTexture,
                        overlapRect, tRenderTexRectArray(sourceTextures));
                }
            } else if(itemFlags == 1) {
                clearLayerAlphaOutsideRect(
                    destination.GetDispatch(), dstClip, overlapRect,
                    dispatchResult);
                if(TVPIsSoftwareRenderManager()) {
                    auto *dstRow = dstPixels;
                    const auto *srcRow = srcPixels;
                    for(int y = 0; y < height; ++y) {
                        for(int x = 0; x < width; ++x) {
                            const auto srcAlpha =
                                static_cast<int>(srcRow[x * 4 + 3]);
                            auto &dstAlpha = dstRow[x * 4 + 3];
                            dstAlpha = static_cast<std::uint8_t>(
                                (static_cast<int>(dstAlpha) * srcAlpha) / 255);
                        }
                        srcRow += srcPitch;
                        dstRow += dstPitch;
                    }
                } else {
                    static std::uint32_t methodHint = 0;
                    static iTVPRenderMethod *method =
                        TVPGetRenderManager()
                            ->GetOrCompileRenderMethod(
                                "AlphaMask", &methodHint,
                                "void main() { gl_FragColor = texture2D(tex0, v_texCoord0); }\n",
                                1, 0)
                            ->SetBlendFuncSeparate(
                                GL_FUNC_ADD, GL_ZERO, GL_ONE, GL_ZERO,
                                GL_SRC_ALPHA);
                    TVPGetRenderManager()->OperateRect(
                        method, dstRenderTexture, dstReferenceTexture,
                        overlapRect, tRenderTexRectArray(sourceTextures));
                }
            }
        } else if(playerStencilType == 0) {
            if(itemFlags == 5 || itemFlags == 6) {
                if(TVPIsSoftwareRenderManager()) {
                    auto *dstRow = dstPixels;
                    const auto *srcRow = srcPixels;
                    for(int y = 0; y < height; ++y) {
                        for(int x = 0; x < width; ++x) {
                            if(static_cast<int>(srcRow[x * 4 + 3]) >= threshold) {
                                dstRow[x * 4 + 3] = 255;
                            }
                        }
                        srcRow += srcPitch;
                        dstRow += dstPitch;
                    }
                } else {
                    static std::uint32_t methodHint = 0;
                    static iTVPRenderMethod *method =
                        TVPGetRenderManager()
                            ->GetOrCompileRenderMethod(
                                "AlphaMaskThresholdFill", &methodHint,
                                "uniform float threshold;\n"
                                "void main() { gl_FragColor = vec4(0,0,0,step(threshold, texture2D(tex0, v_texCoord0).a)); }\n",
                                1, 0)
                            ->SetBlendFuncSeparate(
                                GL_MAX, GL_ZERO, GL_ONE, GL_ZERO,
                                GL_ONE_MINUS_SRC_ALPHA);
                    static int thresholdId =
                        method->EnumParameterID("threshold");
                    method->SetParameterOpa(thresholdId, threshold);
                    TVPGetRenderManager()->OperateRect(
                        method, dstRenderTexture, dstReferenceTexture,
                        overlapRect, tRenderTexRectArray(sourceTextures));
                }
            } else if(itemFlags == 2) {
                if(TVPIsSoftwareRenderManager()) {
                    auto *dstRow = dstPixels;
                    const auto *srcRow = srcPixels;
                    for(int y = 0; y < height; ++y) {
                        for(int x = 0; x < width; ++x) {
                            if(static_cast<int>(srcRow[x * 4 + 3]) >= threshold) {
                                dstRow[x * 4 + 3] = 0;
                            }
                        }
                        srcRow += srcPitch;
                        dstRow += dstPitch;
                    }
                } else {
                    static std::uint32_t methodHint = 0;
                    static iTVPRenderMethod *method =
                        TVPGetRenderManager()
                            ->GetOrCompileRenderMethod(
                                "AlphaMaskThresholdCrop", &methodHint,
                                "uniform float threshold;\n"
                                "void main() { gl_FragColor = vec4(0,0,0,step(threshold, texture2D(tex0, v_texCoord0).a)); }\n",
                                1, 0)
                            ->SetBlendFuncSeparate(
                                GL_FUNC_ADD, GL_ZERO, GL_ONE, GL_ZERO,
                                GL_ONE_MINUS_SRC_ALPHA);
                    static int thresholdId =
                        method->EnumParameterID("threshold");
                    method->SetParameterOpa(thresholdId, threshold);
                    TVPGetRenderManager()->OperateRect(
                        method, dstRenderTexture, dstReferenceTexture,
                        overlapRect, tRenderTexRectArray(sourceTextures));
                }
            } else if(itemFlags == 1) {
                clearLayerAlphaOutsideRect(
                    destination.GetDispatch(), dstClip, overlapRect,
                    dispatchResult);
                if(TVPIsSoftwareRenderManager()) {
                    auto *dstRow = dstPixels;
                    const auto *srcRow = srcPixels;
                    for(int y = 0; y < height; ++y) {
                        for(int x = 0; x < width; ++x) {
                            if(static_cast<int>(srcRow[x * 4 + 3]) < threshold) {
                                dstRow[x * 4 + 3] = 0;
                            }
                        }
                        srcRow += srcPitch;
                        dstRow += dstPitch;
                    }
                } else {
                    static std::uint32_t methodHint = 0;
                    static iTVPRenderMethod *method =
                        TVPGetRenderManager()
                            ->GetOrCompileRenderMethod(
                                "AlphaMaskThreshold", &methodHint,
                                "uniform float threshold;\n"
                                "void main() { gl_FragColor = vec4(0,0,0,step(threshold, texture2D(tex0, v_texCoord0).a)); }\n",
                                1, 0)
                            ->SetBlendFuncSeparate(
                                GL_FUNC_ADD, GL_ZERO, GL_ONE, GL_ZERO,
                                GL_SRC_ALPHA);
                    static int thresholdId =
                        method->EnumParameterID("threshold");
                    method->SetParameterOpa(thresholdId, threshold);
                    TVPGetRenderManager()->OperateRect(
                        method, dstRenderTexture, dstReferenceTexture,
                        overlapRect, tRenderTexRectArray(sourceTextures));
                }
            }
        }

        // Every non-empty overlap ends in the script-visible four-Integer
        // update call, including unsupported mode/op no-ops.
        callLayerUpdateInteger_guess(
            destination.GetDispatch(), overlapRect, dispatchResult);
    }

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
        int itemFlags) {
        applyMotionAlphaMaskOwnedVariants_guess(
            dstLayerVariant, dstX, dstY, srcLayerVariant, srcX, srcY,
            width, height, threshold, playerStencilType, itemFlags);
    }

#if defined(KRKR2_WASMTIME_HEADLESS)
    struct FirstPixelProbe {
        bool ok = false;
        std::uint32_t bgra = 0;
        int b = 0;
        int g = 0;
        int r = 0;
        int a = 0;
        int x = 0;
        int y = 0;
    };

    FirstPixelProbe readPixelForDiagnostics(const iTVPBaseBitmap *bitmap,
                                            int x,
                                            int y) {
        FirstPixelProbe probe;
        probe.x = x;
        probe.y = y;
        if(!bitmap || bitmap->GetWidth() <= 0 || bitmap->GetHeight() <= 0) {
            return probe;
        }
        if(x < 0 || y < 0 ||
           x >= static_cast<int>(bitmap->GetWidth()) ||
           y >= static_cast<int>(bitmap->GetHeight())) {
            return probe;
        }
        const auto *row = static_cast<const std::uint8_t *>(
            bitmap->GetScanLine(static_cast<tjs_uint>(y)));
        if(!row) {
            return probe;
        }
        std::memcpy(&probe.bgra, row + static_cast<size_t>(x) * 4u,
                    sizeof(probe.bgra));
        probe.b = static_cast<int>(probe.bgra & 0xffu);
        probe.g = static_cast<int>((probe.bgra >> 8) & 0xffu);
        probe.r = static_cast<int>((probe.bgra >> 16) & 0xffu);
        probe.a = static_cast<int>((probe.bgra >> 24) & 0xffu);
        probe.ok = true;
        return probe;
    }

    FirstPixelProbe readFirstPixelForDiagnostics(const iTVPBaseBitmap *bitmap) {
        return readPixelForDiagnostics(bitmap, 0, 0);
    }

    void appendPointerJson(std::string &out, const char *name, const void *ptr) {
        out += ",\"";
        out += name;
        out += "\":";
        if(ptr) {
            out += "\"";
            out += fmt::format("{}", ptr);
            out += "\"";
        } else {
            out += "null";
        }
    }

    void appendPixelProbeJson(std::string &out, const char *name,
                              const FirstPixelProbe &probe) {
        out += fmt::format(
            ",\"{}\":{{\"ok\":{},\"x\":{},\"y\":{},\"bgra\":\"0x{:08x}\",\"b\":{},\"g\":{},\"r\":{},\"a\":{}}}",
            name, probe.ok ? "true" : "false", probe.x, probe.y, probe.bgra,
            probe.b, probe.g, probe.r, probe.a);
    }

    void appendPixelSamplesJson(
        std::string &out,
        const char *name,
        const std::vector<FirstPixelProbe> &samples) {
        out += ",\"";
        out += name;
        out += "\":[";
        for(size_t i = 0; i < samples.size(); ++i) {
            const auto &probe = samples[i];
            if(i != 0) {
                out += ",";
            }
            out += fmt::format(
                "{{\"ok\":{},\"x\":{},\"y\":{},\"bgra\":\"0x{:08x}\",\"b\":{},\"g\":{},\"r\":{},\"a\":{}}}",
                probe.ok ? "true" : "false", probe.x, probe.y, probe.bgra,
                probe.b, probe.g, probe.r, probe.a);
        }
        out += "]";
    }

    template <size_t N>
    void appendFloatArrayJson(std::string &out,
                              const char *name,
                              const std::array<float, N> &values) {
        out += ",\"";
        out += name;
        out += "\":[";
        for(size_t i = 0; i < values.size(); ++i) {
            if(i != 0) {
                out += ",";
            }
            out += fmt::format("{:.9g}", values[i]);
        }
        out += "]";
    }

    void appendPointArrayJson(std::string &out,
                              const char *name,
                              const std::array<tTVPPointD, 3> &points) {
        out += ",\"";
        out += name;
        out += "\":[";
        for(size_t i = 0; i < points.size(); ++i) {
            if(i != 0) {
                out += ",";
            }
            out += fmt::format("[{:.17g},{:.17g}]", points[i].x,
                               points[i].y);
        }
        out += "]";
    }

    const char *bltMethodNameForDiagnostics(tTVPBBBltMethod method) {
        switch(method) {
            case bmCopy: return "bmCopy";
            case bmCopyOnAlpha: return "bmCopyOnAlpha";
            case bmAlpha: return "bmAlpha";
            case bmAlphaOnAlpha: return "bmAlphaOnAlpha";
            case bmAddAlphaOnAlpha: return "bmAddAlphaOnAlpha";
            case bmAlphaOnAddAlpha: return "bmAlphaOnAddAlpha";
            case bmCopyOnAddAlpha: return "bmCopyOnAddAlpha";
            default: return "other";
        }
    }

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
        tTVPBBStretchType type) {
        tTVPBBBltMethod bltMethod = bmCopy;
        const bool bltMethodOk =
            renderLayer &&
            renderLayer->ResolveBltMethodForDiagnostics(bltMethod, blendMode);
        const iTVPBaseBitmap *sourceImage =
            sourceArgLayer
                ? static_cast<const iTVPBaseBitmap *>(
                      sourceArgLayer->GetMainImage())
                : static_cast<const iTVPBaseBitmap *>(srcBmp.get());
        auto *targetImage = renderLayer ? renderLayer->GetMainImage() : nullptr;
        const auto sourcePixel =
            readFirstPixelForDiagnostics(sourceImage);
        const auto targetPixel =
            readFirstPixelForDiagnostics(targetImage);
        const auto affinePointArgs =
            buildAffineTrianglePoints(item.corners, -0.5f, -0.5f);
        std::vector<FirstPixelProbe> sourcePixelSamples;
        for(const auto &[x, y] : {
                std::pair<int, int>{0, 0},
                std::pair<int, int>{1, 42},
                std::pair<int, int>{2, 42},
                std::pair<int, int>{3, 42},
                std::pair<int, int>{1, 43},
                std::pair<int, int>{2, 43},
                std::pair<int, int>{3, 43},
                std::pair<int, int>{1, 49},
                std::pair<int, int>{3, 49},
                std::pair<int, int>{1, 50},
                std::pair<int, int>{3, 50},
            }) {
            sourcePixelSamples.push_back(
                readPixelForDiagnostics(sourceImage, x, y));
        }
        std::vector<FirstPixelProbe> targetPixelSamples;
        for(const auto &[x, y] : {
                std::pair<int, int>{725, 693},
                std::pair<int, int>{725, 694},
                std::pair<int, int>{725, 695},
                std::pair<int, int>{725, 696},
                std::pair<int, int>{725, 697},
                std::pair<int, int>{726, 700},
                std::pair<int, int>{726, 701},
            }) {
            targetPixelSamples.push_back(
                readPixelForDiagnostics(targetImage, x, y));
        }

        std::string payload;
        payload += fmt::format(
            "\"probePhase\":\"{}\",\"branch\":\"{}\","
            "\"executionMethod\":\"{}\",\"nodeIndex\":{},"
            "\"meshType\":{},\"blendMode\":{},\"opacity\":{},\"stretchType\":{},"
            "\"targetFace\":{},\"targetDrawFace\":{},\"targetHoldAlpha\":{},"
            "\"resolvedBltMethodOk\":{},\"resolvedBltMethod\":{},"
            "\"resolvedBltMethodName\":\"{}\"",
            probePhase ? probePhase : "",
            branch ? branch : "",
            executionMethod ? executionMethod : "",
            item.nodeIndex,
            item.meshType,
            static_cast<int>(blendMode),
            opacity,
            static_cast<int>(type),
            renderLayer ? static_cast<int>(renderLayer->GetFace()) : -1,
            renderLayer ? static_cast<int>(
                              renderLayer->GetDrawFaceForDiagnostics()) : -1,
            renderLayer && renderLayer->GetHoldAlpha() ? 1 : 0,
            bltMethodOk ? "true" : "false",
            bltMethodOk ? static_cast<int>(bltMethod) : -1,
            bltMethodOk ? bltMethodNameForDiagnostics(bltMethod) : "unresolved");
        appendPointerJson(payload, "renderLayer", renderLayer);
        appendPointerJson(payload, "targetImage", targetImage);
        appendPointerJson(payload, "sourceBitmap", srcBmp.get());
        appendPointerJson(payload, "sourceObject", sourceArgObject);
        appendPointerJson(payload, "sourceNativeLayer", sourceArgLayer);
        appendPointerJson(payload, "sourceImage", sourceImage);
        payload += fmt::format(
            ",\"sourceArgClass\":\"{}\"",
            sourceArgClass ? sourceArgClass
                           : (sourceArgLayer ? "Layer" : "Bitmap"));
        payload += fmt::format(
            ",\"sourceSize\":[{},{}],\"targetSize\":[{},{}]",
            sourceImage ? static_cast<int>(sourceImage->GetWidth()) : 0,
            sourceImage ? static_cast<int>(sourceImage->GetHeight()) : 0,
            renderLayer ? static_cast<int>(renderLayer->GetWidth()) : 0,
            renderLayer ? static_cast<int>(renderLayer->GetHeight()) : 0);
        appendFloatArrayJson(payload, "renderItemCorners", item.corners);
        appendPointArrayJson(payload, "operateAffinePointArgs",
                             affinePointArgs);
        payload += fmt::format(
            ",\"softwareAffinePath\":\"{}\","
            "\"softwareAffineRenderer\":\"{}\","
            "\"softwareAffineAlphaBlendDReady\":{},"
            "\"softwareAffineTempFirstPixelValid\":{},"
            "\"softwareAffineTempFirstPixel\":\"0x{:08x}\","
            "\"softwareAffineTargetFirstPixelBeforeValid\":{},"
            "\"softwareAffineTargetFirstPixelBefore\":\"0x{:08x}\","
            "\"softwareAffineTargetFirstPixelAfterValid\":{},"
            "\"softwareAffineTargetFirstPixelAfter\":\"0x{:08x}\","
            "\"softwareAffineAlphaBlendDProbeValid\":{},"
            "\"softwareAffineAlphaBlendDProbePixel\":\"0x{:08x}\","
            "\"softwareAffineAlphaBlendDCProbeValid\":{},"
            "\"softwareAffineAlphaBlendDCProbePixel\":\"0x{:08x}\","
            "\"softwareAffineAlphaBlendDPointsToC\":{},"
            "\"softwareAffineRenderMethodOpacity\":{},"
            "\"softwareAffineRenderMethodBranch\":\"{}\"",
            TVPGetSoftwareAffinePathForWasmtime(),
            TVPGetSoftwareAffineRendererForWasmtime(),
            TVPGetSoftwareAffineAlphaBlendDReadyForWasmtime() ? "true"
                                                              : "false",
            TVPGetSoftwareAffineTempFirstPixelValidForWasmtime() ? "true"
                                                                 : "false",
            TVPGetSoftwareAffineTempFirstPixelForWasmtime(),
            TVPGetSoftwareAffineTargetFirstPixelBeforeValidForWasmtime()
                ? "true"
                : "false",
            TVPGetSoftwareAffineTargetFirstPixelBeforeForWasmtime(),
            TVPGetSoftwareAffineTargetFirstPixelAfterValidForWasmtime()
                ? "true"
                : "false",
            TVPGetSoftwareAffineTargetFirstPixelAfterForWasmtime(),
            TVPGetSoftwareAffineAlphaBlendDProbeValidForWasmtime() ? "true"
                                                                   : "false",
            TVPGetSoftwareAffineAlphaBlendDProbePixelForWasmtime(),
            TVPGetSoftwareAffineAlphaBlendDCProbeValidForWasmtime() ? "true"
                                                                    : "false",
            TVPGetSoftwareAffineAlphaBlendDCProbePixelForWasmtime(),
            TVPGetSoftwareAffineAlphaBlendDPointsToCForWasmtime() ? "true"
                                                                  : "false",
            TVPGetSoftwareAffineRenderMethodOpacityForWasmtime(),
            TVPGetSoftwareAffineRenderMethodBranchForWasmtime());
        appendPixelProbeJson(payload, "sourceFirstPixel", sourcePixel);
        appendPixelProbeJson(payload, "targetFirstPixel", targetPixel);
        appendPixelSamplesJson(payload, "sourcePixelSamples",
                               sourcePixelSamples);
        appendPixelSamplesJson(payload, "targetPixelSamples",
                               targetPixelSamples);
        motion::detail::motionTraceRenderDirectExecuteProbe(
            player, samplePoint, payload.c_str());
    }
#endif

} // namespace motion::internal::render_detail
