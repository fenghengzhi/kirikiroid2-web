#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "MeshPoint.h"
#include "tjs.h"

class tTJSNI_BaseLayer;
class iTVPTexture2D;

namespace motion {

    class SeparateLayerAdaptor;

    struct PrivateMotionGLLRenderItemInput_guess {
        std::int32_t opacity = 0;
        std::uint8_t stencilMaskRef = 0;
        std::uint8_t stencilWriteRef = 0;
        std::int32_t blendMode = 0;
        std::int32_t geometryType = 0;
        std::int32_t meshDivX = 0;
        std::int32_t meshDivY = 0;
        std::array<std::uint32_t, 4> packedColors{};
        std::array<std::int32_t, 4> sourceRect{};
        iTVPTexture2D *sourceTexture = nullptr;
        std::array<detail::MeshPoint, 3> affinePoints{};
    };

    tTJSNI_BaseLayer *ensurePrivateMotionGLL_guess(
        SeparateLayerAdaptor &sla);

    tTJSNI_BaseLayer *resolvePrivateMotionGLLNative_guess(
        iTJSDispatch2 *object);
    tTJSNI_BaseLayer *queryPrivateMotionGLLNativeFromVariant_guess(
        const tTJSVariant &value);

    void clearPrivateMotionGLLRenderQueue_guess(
        iTJSDispatch2 *object);
    void clearPrivateMotionGLLRenderQueue_guess(
        tTJSNI_BaseLayer *layer);
    void setPrivateMotionGLLStencilCount_guess(
        tTJSNI_BaseLayer *layer,
        tjs_int value);
    void appendPrivateMotionGLLRenderItem_guess(
        iTJSDispatch2 *object,
        const PrivateMotionGLLRenderItemInput_guess &item,
        std::vector<detail::MeshPoint> *pointsToSwap);
    void appendPrivateMotionGLLRenderItem_guess(
        tTJSNI_BaseLayer *layer,
        const PrivateMotionGLLRenderItemInput_guess &item,
        std::vector<detail::MeshPoint> *pointsToSwap);
    std::size_t privateMotionGLLRenderQueueSize_guess(
        iTJSDispatch2 *object);
    std::size_t privateMotionGLLRenderQueueSize_guess(
        tTJSNI_BaseLayer *layer);

} // namespace motion
