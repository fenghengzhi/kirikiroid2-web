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
            // 0x6C06E8 gates on a zero delta or a clear post-draw snapshot.
            // The snapshot records whether the internal render Layer was
            // materialized last frame, which the width/height read below needs.
            // type-10 anchors are absent from the logo fixtures, so this path is
            // inert there.
            if (_deltaTime == 0.0 || !_internalRenderLayerReady) {
                an.anchorEnabled = false;
                an.source.valid = false;
                continue;
            }
            an.anchorEnabled = true;
            {
                ncbPropAccessor internal{tTJSVariant(_internalRenderLayer)};
                // sub_6C0528 @0x6C075C CopyRefs the Player-owned internal Layer
                // into the node source before publishing source.valid. This
                // exact object identity is sub_6C1B70's fast-path predicate.
                an.source.object = _internalRenderLayer;
                an.source.valid = true;
                // The two dimensions are independently probed and then read
                // once; a failed probe yields zero. There is deliberately no
                // positive-size clamp, so the later scale calculation retains
                // the original divide-by-zero boundary.
                const auto readDimension = [&](const tjs_char *member) -> tjs_int {
                    {
                        tTJSVariant probe;
                        if(TJS_FAILED(internal.GetDispatch()->PropGet(
                               TJS_MEMBERMUSTEXIST, member, nullptr, &probe,
                               internal.GetDispatch()))) {
                            return 0;
                        }
                    }
                    tTJSVariant value;
                    (void)internal.GetDispatch()->PropGet(
                        0, member, nullptr, &value, internal.GetDispatch());
                    return static_cast<tjs_int>(value.AsInteger());
                };
                an.source.width = static_cast<double>(
                    readDimension(TJS_W("width")));
                const double height = static_cast<double>(
                    readDimension(TJS_W("height")));
                const double originX = an.source.width * 0.5;
                an.source.clipLeft = 0.0;
                an.source.clipTop = 0.0;
                an.source.height = height;
                an.source.originX = originX;
                an.source.originY = height * 0.5;
                an.source.clipRight = 1.0;
                an.source.clipBottom = 1.0;
            }

            // Damping exponent — byte-verified disasm @0x6C0884-0x6C08B8:
            //   v27     = (*a1+592)/(*a1+1168) = _deltaTime / _speedMul
            //   dampPow = dt*(v27*dt/v27)/v27/60.0/feedbackTimespan,
            //   dt = _deltaTime
            // The redundant (v27*dt/v27) mul/div is preserved verbatim — FP is
            // not associative, so it is kept to reproduce the exact value the
            // binary computes (it does NOT collapse to dt). The binary divides by
            // feedback.timespan directly: the former max(.,0.001) guard and the
            // _frameLastTime numerator were both port inventions.
            const double dt = _deltaTime;
            const double v27 = _deltaTime / _speedMul;
            const double dampPow =
                dt * (v27 * dt / v27) / v27 / 60.0 / an.feedbackTimespan;

            // Angle damping (0x6C08C0..0x6C08E0)
            double angle = an.accumulated.angle;
            if (angle >= 180.0)
                angle = 360.0 - (360.0 - angle) * dampPow;
            else
                angle = angle * dampPow;
            an.accumulated.angle = angle;

            // Scale damping (0x6C08E0..0x6C0924)
            an.accumulated.scaleX = std::pow(
                an.accumulated.scaleX * 32.0 / an.source.width, dampPow);
            an.accumulated.scaleY = std::pow(
                an.accumulated.scaleY * 32.0 / an.source.height, dampPow);

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
                // RGB-channel base (0x6C0A68 region): the binary at 0x6c0aac does
                //   v1 = qword_14D7C50[(blend & 0xF0) == 0x10]
                // where qword_14D7C50 = {255.0, 128.0} (byte-verified @0x14D7C50).
                // So the boolean (==0x10) indexes the array: TRUE  -> index 1 -> 128.0,
                //                                            FALSE -> index 0 -> 255.0.
                // i.e. default-blend (==0x10) uses 128.0; non-default uses 255.0.
                // (commit 5018087 restored the lost 128.0 branch but wired it to the
                //  wrong side; corrected here per 0x6C0528 decompile. Alpha base below
                //  is always 255.0.)
                //
                // The blend byte the binary reads is *(node + 536*activeSlotIndex + 44)
                // (0x6c0a80/0x6c0aac with slot index *(node+1392) @0x6c06d4) — i.e. the
                // ACTIVE clip slot's per-slot blendMode (slot0@node+320, slot1@node+856,
                // +44 = ClipSlot::blendMode). There is no node-level blend mirror.
                // Read the active slot directly to match the binary's data source.
                const bool isDefaultBlend =
                    (an.activeSlot().blendMode & 0xF0) == 0x10;
                const double base = isDefaultBlend ? 128.0 : 255.0;
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
