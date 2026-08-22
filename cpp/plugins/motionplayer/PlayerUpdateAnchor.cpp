// PlayerUpdateAnchor.cpp — feedback-anchor update phase
#include "PlayerUpdateLayersInternal.h"

namespace motion {
    void Player::updateLayersPhase3_AnchorNode() {
        auto &nodes = _nodes;

        // This value is deliberately neither initialized nor reset per node.
        // The equal-color/default-blend branch consumes the previous value,
        // including the indeterminate first-use boundary in the references.
        double carriedRgbBase_guess;

        for(size_t index = 1; index < nodes.size(); ++index) {
            auto &anchor = nodes[index];
            if(anchor.nodeType != 10 || !anchor.accumulated.active) {
                continue;
            }

            _needsInternalAssignImages = true;
            if(_deltaTime == 0.0 || !_internalRenderLayerReady) {
                anchor.source.valid = false;
                continue;
            }

            {
                ncbPropAccessor internal{tTJSVariant(_internalRenderLayer)};
                anchor.source.object = _internalRenderLayer;
                anchor.source.valid = true;

                anchor.source.width = static_cast<double>(
                    internal.getIntValue(TJS_W("width"), 0));
                anchor.source.height = static_cast<double>(
                    internal.getIntValue(TJS_W("height"), 0));
                anchor.source.originX = anchor.source.width * 0.5;
                anchor.source.originY = anchor.source.height * 0.5;
                anchor.source.clipLeft = 0.0;
                anchor.source.clipTop = 0.0;
                anchor.source.clipRight = 1.0;
                anchor.source.clipBottom = 1.0;
            }

            const double deltaTime = _deltaTime;
            const double scaledDelta = _deltaTime / _speedMul;
            const double dampPower =
                deltaTime
                * (scaledDelta * deltaTime / scaledDelta)
                / scaledDelta / 60.0 / anchor.feedbackTimespan;

            double angle = anchor.accumulated.angle;
            if(angle >= 180.0) {
                angle = 360.0 - (360.0 - angle) * dampPower;
            } else {
                angle *= dampPower;
            }
            anchor.accumulated.angle = angle;

            anchor.accumulated.scaleX = std::pow(
                anchor.accumulated.scaleX * 32.0 / anchor.source.width,
                dampPower);
            anchor.accumulated.scaleY = std::pow(
                anchor.accumulated.scaleY * 32.0 / anchor.source.height,
                dampPower);
            anchor.accumulated.slantX *= dampPower;
            anchor.accumulated.slantY *= dampPower;

            {
                Affine2x3 local = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(local, anchor);
                anchor.accumulated.m11 = local[0];
                anchor.accumulated.m21 = local[1];
                anchor.accumulated.m12 = local[2];
                anchor.accumulated.m22 = local[3];
            }

            if(!_independentLayerInherit) {
                const auto &root = nodes[0];
                const double m11 = anchor.accumulated.m11;
                const double m12 = anchor.accumulated.m12;
                const double m21 = anchor.accumulated.m21;
                const double m22 = anchor.accumulated.m22;
                anchor.accumulated.m11 =
                    root.accumulated.m11 * m11
                    + root.accumulated.m12 * m21;
                anchor.accumulated.m21 =
                    root.accumulated.m21 * m11
                    + root.accumulated.m22 * m21;
                anchor.accumulated.m12 =
                    root.accumulated.m11 * m12
                    + root.accumulated.m12 * m22;
                anchor.accumulated.m22 =
                    root.accumulated.m21 * m12
                    + root.accumulated.m22 * m22;
            }

            dampAnchorOpacity_guess(
                anchor.accumulated.opacity,
                anchor.anchorOpaScale,
                dampPower);

            const auto &root = nodes[0];
            anchor.accumulated.posX = root.accumulated.posX
                + dampPower
                    * (anchor.accumulated.posX - root.accumulated.posX);
            anchor.accumulated.posY = root.accumulated.posY
                + dampPower
                    * (anchor.accumulated.posY - root.accumulated.posY);
            anchor.accumulated.posZ = root.accumulated.posZ
                + dampPower
                    * (anchor.accumulated.posZ - root.accumulated.posZ);

            const auto packedColors =
                copyPackedColorsFromBytes(anchor.colorBytes);
            const bool allEqual =
                packedColors[0] == packedColors[1]
                && packedColors[1] == packedColors[2]
                && packedColors[2] == packedColors[3];
            const bool defaultBlend =
                (anchor.activeSlot().blendMode & 0xF0) == 0x10;

            double rgbBase;
            bool duplicateFirst;
            if(allEqual) {
                if(defaultBlend) {
                    rgbBase = carriedRgbBase_guess;
                } else {
                    rgbBase = 255.0;
                    if(packedColors[0] == 0xFFFFFFFFu) {
                        continue;
                    }
                }
                if(packedColors[0] == 0xFF808080u) {
                    carriedRgbBase_guess = rgbBase;
                    continue;
                }
                duplicateFirst = true;
            } else {
                rgbBase = defaultBlend ? 128.0 : 255.0;
                duplicateFirst = false;
            }

            const int colorCount = duplicateFirst ? 1 : 4;
            for(int colorIndex = 0; colorIndex < colorCount; ++colorIndex) {
                dampAnchorPackedColor_guess(
                    anchor.colorBytes + colorIndex * 4,
                    anchor.anchorColorScale + colorIndex * 4,
                    rgbBase,
                    dampPower);
            }
            if(duplicateFirst) {
                std::memcpy(anchor.colorBytes + 4,
                            anchor.colorBytes, 4);
                std::memcpy(anchor.colorBytes + 8,
                            anchor.colorBytes, 4);
                std::memcpy(anchor.colorBytes + 12,
                            anchor.colorBytes, 4);
            }
            carriedRgbBase_guess = rgbBase;
        }
    }
} // namespace motion
