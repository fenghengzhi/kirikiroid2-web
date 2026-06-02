// PlayerUpdateAnchor.cpp — updateLayers anchor node phase
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"

namespace motion {
    void Player::updateLayersPhase3_AnchorNode() {
        auto &nodes = _nodes;
        // --- sub_6C0528: Anchor node processing (nodeType=10) ---
        // Aligned to 0x6C0528. For each nodeType=10 active node,
        // apply exponential damping toward root node values.
        for (size_t ai = 1; ai < nodes.size(); ++ai) {
            auto &an = nodes[ai];
            if (an.nodeType != 10 || !an.accumulated.active) continue;
            _needsInternalAssignImages = true;
            // 0x6C06E8 gate: binary tests `Player+592 == 0.0 || !*(Player+612)`.
            // Player+592 = _deltaTime (NOT _frameLastTime/Player+904). Player+612
            // = the post-draw snapshot of +613 (_internalRenderLayerReady, written
            // by updateLayerAfterDrawLike_0x6CE7D8) — "the internal render Layer
            // was materialized last frame", which the w/h read below depends on.
            // type-10 anchors are absent from the logo fixtures, so this path is
            // inert there.
            if (_deltaTime == 0.0 || !_internalRenderLayerReady) {
                an.anchorEnabled = false;
                an.renderTreeFlag200 = false;
                continue;
            }
            an.anchorEnabled = true;
            an.renderTreeFlag200 = true;
            // Read width/height (0x6C0790..0x6C0848): the binary reads them from
            // the per-PLAYER internal render Layer — sub_A0F5E0(player+696) then
            // PropGet(L"width"/L"height") — so all anchor nodes share ONE w/h =
            // that Layer's size (set to the window size by setLayerSizeLike_
            // 0x6CE19C). The port's mirror is _internalRenderLayer; the +612 gate
            // above guarantees it was materialized last frame. NO `<=0?32` clamp
            // (the binary has none; a failed PropGet yields w=0 -> pow(scale*32/0)
            // = inf, the real binary behavior).
            double cw = 0.0;
            double ch = 0.0;
            if (_internalRenderLayer.Type() == tvtObject) {
                iTJSDispatch2 *rl = _internalRenderLayer.AsObjectNoAddRef();
                tTJSVariant wv;
                tTJSVariant hv;
                if (rl && TJS_SUCCEEDED(rl->PropGet(0, TJS_W("width"), nullptr,
                                                    &wv, rl))) {
                    cw = static_cast<double>(static_cast<tjs_int>(wv));
                }
                if (rl && TJS_SUCCEEDED(rl->PropGet(0, TJS_W("height"), nullptr,
                                                    &hv, rl))) {
                    ch = static_cast<double>(static_cast<tjs_int>(hv));
                }
            }
            an.clipW = cw;
            an.clipH = ch;
            an.originX = cw * 0.5;
            an.originY = ch * 0.5;

            // Damping exponent — byte-verified disasm @0x6C0884-0x6C08B8:
            //   v27     = (*a1+592)/(*a1+1168) = _deltaTime / _speedMul
            //   dampPow = dt*(v27*dt/v27)/v27/60.0/anchorDamping,  dt = _deltaTime
            // The redundant (v27*dt/v27) mul/div is preserved verbatim — FP is
            // not associative, so it is kept to reproduce the exact value the
            // binary computes (it does NOT collapse to dt). The binary divides by
            // anchorDamping directly: the former max(.,0.001) guard and the
            // _frameLastTime numerator were both port inventions.
            const double dt = _deltaTime;
            const double v27 = _deltaTime / _speedMul;
            const double dampPow =
                dt * (v27 * dt / v27) / v27 / 60.0 / an.anchorDamping;

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
                // RGB-channel base (0x6C0A68 region): the binary selects from
                // qword_14D7C50[] = (blendMode & 0xF0) == 0x10 ? 255.0 : 128.0.
                // The port previously had 255.0 : 255.0 (the 128.0 branch was
                // lost). Alpha always uses 255.0 (below).
                const bool isDefaultBlend =
                    (an.interpolatedCache.blendMode & 0xF0) == 0x10;
                const double base = isDefaultBlend ? 255.0 : 128.0;
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
