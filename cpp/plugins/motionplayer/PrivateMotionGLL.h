#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "tjs.h"

class tTJSNI_BaseLayer;
class iTVPTexture2D;

namespace motion {

    class SeparateLayerAdaptor;

    struct PrivateMotionGLLPackedPointLike_0x6DF33C {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct PrivateMotionGLLRenderItemInputLike_0x6DE738 {
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
        std::vector<PrivateMotionGLLPackedPointLike_0x6DF33C> points;
    };

    iTJSDispatch2 *ensurePrivateMotionGLLLike_0x6D5948(
        SeparateLayerAdaptor &sla,
        const tTJSVariant &ownerVariant,
        const tTJSVariant &targetLayerVariant,
        iTJSDispatch2 *targetLayerObject,
        int canvasWidth,
        int canvasHeight);

    tTJSNI_BaseLayer *resolvePrivateMotionGLLNativeLike_0x6DE24C(
        iTJSDispatch2 *object);

    void clearPrivateMotionGLLRenderQueueLike_0x6DE738(
        iTJSDispatch2 *object);
    void appendPrivateMotionGLLRenderItemLike_0x6DE738(
        iTJSDispatch2 *object,
        const PrivateMotionGLLRenderItemInputLike_0x6DE738 &item);
    std::size_t privateMotionGLLRenderQueueSizeLike_0x6DE738(
        iTJSDispatch2 *object);

} // namespace motion
