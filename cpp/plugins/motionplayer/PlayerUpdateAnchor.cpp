// PlayerUpdateAnchor.cpp — updateLayers anchor node phase
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"

namespace motion {
    void Player::updateLayersPhase3_AnchorNode() {
        auto &nodes = _runtime->nodes;
        // --- sub_6C0528: Anchor node processing (nodeType=10) ---
        // Aligned to 0x6C0528. For each nodeType=10 active node,
        // apply exponential damping toward root node values.
        for (size_t ai = 1; ai < nodes.size(); ++ai) {
            auto &an = nodes[ai];
            if (an.nodeType != 10 || !an.accumulated.active) continue;
            _needsInternalAssignImages = true;
            if (_frameLastTime == 0.0) {
                an.anchorEnabled = false;
                an.renderTreeFlag200 = false;
                continue;
            }
            an.anchorEnabled = true;
            an.renderTreeFlag200 = true;
            // Read width/height (0x6C0790..0x6C0848)
            double cw = an.interpolatedCache.width;
            double ch = an.interpolatedCache.height;
            if (cw <= 0.0) cw = 32.0;
            if (ch <= 0.0) ch = 32.0;
            an.clipW = cw;
            an.clipH = ch;
            an.originX = cw * 0.5;
            an.originY = ch * 0.5;

            // Damping exponent (0x6C088C..0x6C08B8)
            // From decompilation: v28 = dt * (v27*dt/v27) / v27 / 60 / damping
            // where v27 = dt/fps. Simplifies to dt*fps/60/damping for dt~1 frame.
            const double dampPow = std::abs(_frameLastTime) / 60.0
                / std::max(an.anchorDamping, 0.001);

            // Angle damping (0x6C08C0..0x6C08E0)
            double angle = an.accumulated.angle;
            if (angle >= 180.0)
                angle = 360.0 - (360.0 - angle) * dampPow;
            else
                angle = angle * dampPow;
            an.accumulated.angle = angle;

            // Scale damping (0x6C08E0..0x6C0924)
            an.accumulated.scaleX = std::pow(
                an.accumulated.scaleX * 32.0 / cw, dampPow);
            an.accumulated.scaleY = std::pow(
                an.accumulated.scaleY * 32.0 / ch, dampPow);

            // Slant damping (0x6C0924..0x6C0938)
            an.accumulated.slantX *= dampPow;
            an.accumulated.slantY *= dampPow;

            // Rebuild local matrix via sub_699940 (0x6C0944)
            {
                Affine2x3 la = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
                applyLocalTransform(la, an);
                an.accumulated.m11 = la[0]; an.accumulated.m21 = la[1];
                an.accumulated.m12 = la[2]; an.accumulated.m22 = la[3];
            }

            // If !independentLayerInherit: multiply with root (0x6C094C)
            if (!_independentLayerInherit && !nodes.empty()) {
                const auto &rn = nodes[0];
                const double nm11 = an.accumulated.m11, nm12 = an.accumulated.m12;
                const double nm21 = an.accumulated.m21, nm22 = an.accumulated.m22;
                an.accumulated.m11 = rn.accumulated.m11*nm11 + rn.accumulated.m12*nm21;
                an.accumulated.m21 = rn.accumulated.m21*nm11 + rn.accumulated.m22*nm21;
                an.accumulated.m12 = rn.accumulated.m11*nm12 + rn.accumulated.m12*nm22;
                an.accumulated.m22 = rn.accumulated.m21*nm12 + rn.accumulated.m22*nm22;
            }

            // Opacity damping (0x6C0994..0x6C09F8)
            {
                int opa = an.accumulated.opacity;
                double opaF = static_cast<double>(opa) / 255.0;
                if (opa == 0) opaF = 1.0 / 255.0;
                double newOpa = std::pow(opaF, dampPow) * 255.0 * an.anchorOpaScale;
                newOpa = std::clamp(newOpa, 0.0, 255.0);
                an.accumulated.opacity = static_cast<int>(newOpa);
                double denom = newOpa;
                if (static_cast<int>(newOpa) < 0) denom += 4294967296.0;
                if (denom != 0.0) an.anchorOpaScale = newOpa / denom;
            }

            // Position lerp toward root (0x6C0A04..0x6C0A4C)
            if (!nodes.empty()) {
                const auto &rn = nodes[0];
                an.accumulated.posX = rn.accumulated.posX
                    + dampPow * (an.accumulated.posX - rn.accumulated.posX);
                an.accumulated.posY = rn.accumulated.posY
                    + dampPow * (an.accumulated.posY - rn.accumulated.posY);
                an.accumulated.posZ = rn.accumulated.posZ
                    + dampPow * (an.accumulated.posZ - rn.accumulated.posZ);
            }

            // Color damping (0x6C0A68..0x6C0C58)
            // Per-channel pow(channel/base, dampPow)*base*colorScale
            {
                const bool isDefaultBlend =
                    (an.interpolatedCache.blendMode & 0xF0) == 0x10;
                const double base = isDefaultBlend ? 255.0 : 255.0;
                const auto packedColors = copyPackedColorsFromBytes(an.colorBytes);
                const bool allEqual =
                    packedColors[0] == packedColors[1]
                    && packedColors[1] == packedColors[2]
                    && packedColors[2] == packedColors[3];
                if (!(allEqual && packedColors[0] == 0xFF808080u)) {
                    int iters = (allEqual) ? 1 : 4;
                    for (int ci = 0; ci < iters && ci < 4; ++ci) {
                        for (int ch = 0; ch < 3; ++ch) {
                            double v = static_cast<double>(an.colorBytes[ci*4+ch]);
                            if (v == 0.0) v = 1.0;
                            double res = base * std::pow(v / base, dampPow)
                                * an.anchorColorScale[ci*4+ch];
                            res = std::clamp(res, 0.0, 255.0);
                            int ir = static_cast<int>(res);
                            double dr = static_cast<double>(ir);
                            if (dr != 0.0) an.anchorColorScale[ci*4+ch] = res / dr;
                            an.colorBytes[ci*4+ch] = static_cast<uint8_t>(ir);
                        }
                        // Alpha channel (0x6C0BA8..0x6C0BE0)
                        double av = static_cast<double>(an.colorBytes[ci*4+3]) / 255.0;
                        if (av == 0.0) av = 1.0 / 255.0;
                        double ares = std::pow(av, dampPow) * 255.0
                            * an.anchorColorScale[ci*4+3];
                        ares = std::clamp(ares, 0.0, 255.0);
                        int iar = static_cast<int>(ares);
                        double dar = static_cast<double>(iar);
                        if (dar != 0.0) an.anchorColorScale[ci*4+3] = ares / dar;
                        an.colorBytes[ci*4+3] = static_cast<uint8_t>(iar);
                    }
                    if (allEqual) {
                        std::memcpy(&an.colorBytes[4], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[8], &an.colorBytes[0], 4);
                        std::memcpy(&an.colorBytes[12], &an.colorBytes[0], 4);
                    }
                }
            }
        }

    }

} // namespace motion
