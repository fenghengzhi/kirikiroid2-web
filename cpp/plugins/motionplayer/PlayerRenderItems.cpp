// PlayerRenderItems.cpp — calcBounds and prepared render-item build
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "PlayerRenderInternal.h"
#include "MotionTraceWeb.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>

using namespace motion::internal;

namespace {

    // The native implementation keeps one unordered input pair and its result
    // in process-global storage. Identity fast paths do not update this cache.
    // It is deliberately unsynchronized, matching the reference boundary.
    std::uint32_t packedColorCacheFirst_guess = 0;
    std::uint32_t packedColorCacheSecond_guess = 0;
    std::uint32_t packedColorCacheResult_guess = 0;

    struct DispatchRelease_guess {
        void operator()(iTJSDispatch2 *dispatch) const {
            if(dispatch) {
                dispatch->Release();
            }
        }
    };

    using RetainedDispatch_guess =
        std::unique_ptr<iTJSDispatch2, DispatchRelease_guess>;

    inline std::array<std::uint32_t, 4> copyPackedColorsFromBytes(
        const uint8_t (&colorBytes)[16]) {
        std::array<std::uint32_t, 4> packedColors{};
        std::memcpy(packedColors.data(), colorBytes,
                    sizeof(std::uint32_t) * packedColors.size());
        return packedColors;
    }

    inline std::array<int, 4> unpackPackedRgba(std::uint32_t packedColor) {
        return {
            static_cast<int>(packedColor & 0xFFu),
            static_cast<int>((packedColor >> 8) & 0xFFu),
            static_cast<int>((packedColor >> 16) & 0xFFu),
            static_cast<int>((packedColor >> 24) & 0xFFu),
        };
    }

    // The scalar helper uses signed truncation after multiplying by 256.
    // Express the observed NaN/saturation boundary without relying on an
    // undefined out-of-range C++ floating-to-integer conversion. Android
    // arm64 vectorizes the first horizontal pair through signed 64-bit lanes,
    // so malformed out-of-range clipLeft/clipRight values remain a documented
    // compiler-specific boundary rather than a second source-level algorithm.
    inline std::uint32_t packedColorInterpolationWeightS32_guess(
        double ratio) {
        if(std::isnan(ratio)) return 0;
        if(ratio >= 8388608.0) return 0x7FFFFFFFu;
        if(ratio <= -8388608.0) return 0x80000000u;
        return static_cast<std::uint32_t>(
            static_cast<std::int32_t>(std::trunc(ratio * 256.0)));
    }

}

namespace motion::internal {

    std::int32_t prepareBezierPatchDivision_guess(
        double ratio, std::uint32_t meshDivision) {
        const double scaledDivision =
            ratio * static_cast<double>(meshDivision);
        std::int32_t converted;
        constexpr double signedUpper = 0x1p31;
        constexpr double signedLower = -0x1p31;

        if(std::isnan(scaledDivision)) {
            converted = 0;
        } else if(scaledDivision >= signedUpper) {
            converted = std::numeric_limits<std::int32_t>::max();
        } else if(scaledDivision <= signedLower) {
            converted = std::numeric_limits<std::int32_t>::min();
        } else {
            converted = static_cast<std::int32_t>(scaledDivision);
        }
        return converted >= 50 ? 50 : converted;
    }

    std::uint32_t multiplyPackedColorWeights_guess(
        std::uint32_t lhs, std::uint32_t rhs) {
        constexpr std::uint32_t identity = 0xFF808080u;
        if(rhs == identity) return lhs;
        if(lhs == identity) return rhs;

        if((packedColorCacheFirst_guess == lhs &&
            packedColorCacheSecond_guess == rhs) ||
           (packedColorCacheFirst_guess == rhs &&
            packedColorCacheSecond_guess == lhs)) {
            return packedColorCacheResult_guess;
        }

        std::uint32_t result = 0;
        for(unsigned shift = 0; shift != 24; shift += 8) {
            const auto lhsChannel = (lhs >> shift) & 0xFFu;
            const auto rhsChannel = (rhs >> shift) & 0xFFu;
            const auto product = std::min<std::uint32_t>(
                255u, (lhsChannel * rhsChannel) >> 7);
            result |= product << shift;
        }
        const auto alpha =
            (((lhs >> 24) & 0xFFu) * ((rhs >> 24) & 0xFFu)) / 255u;
        result |= alpha << 24;

        packedColorCacheFirst_guess = lhs;
        packedColorCacheSecond_guess = rhs;
        packedColorCacheResult_guess = result;
        return result;
    }

    std::uint32_t interpolatePackedColor_guess(
        const tTJSVariant &curve, std::uint32_t from,
        std::uint32_t to, double ratio) {
        if(from == to) {
            return from;
        }
        if(curve.Type() != tvtVoid) {
            ratio = evaluateVariableTrackEasing_guess(curve, ratio);
        }

        const std::uint32_t weight =
            packedColorInterpolationWeightS32_guess(ratio);
        const std::uint32_t inverseWeight = 256u - weight;
        constexpr std::uint32_t pairMask = 0x00FF00FFu;
        const std::uint32_t highPairs =
            (((from >> 8) & pairMask) * inverseWeight)
            + (((to >> 8) & pairMask) * weight);
        const std::uint32_t lowPairs =
            ((from & pairMask) * inverseWeight)
            + ((to & pairMask) * weight);
        return highPairs
            ^ ((highPairs ^ (lowPairs >> 8)) & pairMask);
    }

}

namespace {

    // Remap the four accumulated corner colors through the source
    // descriptor's normalized clip rectangle. The two early returns precede
    // the default curve Variant's lifetime in all four reference binaries.
    inline void remapPackedColorsForSourceClip_guess(
        const motion::detail::MotionNode::SourceState &source,
        std::array<std::uint32_t, 4> &colors) {
        if(source.clipLeft == 0.0 && source.clipTop == 0.0
           && source.clipRight == 1.0 && source.clipBottom == 1.0) {
            return;
        }
        if(colors[0] == colors[1] && colors[1] == colors[2]
           && colors[2] == colors[3]) {
            return;
        }

        const tTJSVariant colorCurve;
        const std::uint32_t topLeft = interpolatePackedColor_guess(
            colorCurve, colors[0], colors[1], source.clipLeft);
        const std::uint32_t topRight = interpolatePackedColor_guess(
            colorCurve, colors[0], colors[1], source.clipRight);
        const std::uint32_t bottomLeft = interpolatePackedColor_guess(
            colorCurve, colors[2], colors[3], source.clipLeft);
        const std::uint32_t bottomRight = interpolatePackedColor_guess(
            colorCurve, colors[2], colors[3], source.clipRight);

        colors[0] = interpolatePackedColor_guess(
            colorCurve, topLeft, bottomLeft, source.clipTop);
        colors[1] = interpolatePackedColor_guess(
            colorCurve, topRight, bottomRight, source.clipTop);
        colors[2] = interpolatePackedColor_guess(
            colorCurve, topLeft, bottomLeft, source.clipBottom);
        colors[3] = interpolatePackedColor_guess(
            colorCurve, topRight, bottomRight, source.clipBottom);
    }

} // anonymous namespace

namespace motion::internal::render_detail {

    namespace {

        inline void translatePreparedPoint_guess(
            float &x, float &y, float cameraOffsetX, float cameraOffsetY) {
            // Both operands are floats in the reference pass. Keep the
            // narrowing point here rather than promoting the addition.
            x += cameraOffsetX;
            y += cameraOffsetY;
        }

        inline void projectPreparedPoint_guess(
            float &x, float &y, double itemZ,
            double projectionOriginX, double projectionOriginY,
            double projectionZ) {
            const double sourceX = static_cast<double>(x);
            const double sourceY = static_cast<double>(y);
            const double denominator = itemZ - projectionZ;
            x = static_cast<float>(
                sourceX - itemZ * (sourceX - projectionOriginX) /
                              denominator);
            y = static_cast<float>(
                sourceY - itemZ * (sourceY - projectionOriginY) /
                              denominator);
        }

        inline void growProjectedPaintBox_guess(
            std::array<float, 4> &paintBox, float x, float y) {
            const float left = std::floor(x);
            // Plain ordered comparisons are intentional: an unordered NaN
            // coordinate leaves the corresponding extreme sentinel intact.
            if(left < paintBox[0]) paintBox[0] = left;
            const float top = std::floor(y);
            if(top < paintBox[1]) paintBox[1] = top;
            const float right = std::ceil(x);
            if(right > paintBox[2]) paintBox[2] = right;
            const float bottom = std::ceil(y);
            if(bottom > paintBox[3]) paintBox[3] = bottom;
        }

        inline void projectAndGrowPreparedPoint_guess(
            float &x, float &y, double itemZ,
            double projectionOriginX, double projectionOriginY,
            double projectionZ, std::array<float, 4> &paintBox) {
            projectPreparedPoint_guess(
                x, y, itemZ, projectionOriginX, projectionOriginY,
                projectionZ);
            growProjectedPaintBox_guess(paintBox, x, y);
        }

    } // anonymous namespace

    void applyPreparedRenderItemProjectionCore_guess(
        detail::PreparedRenderItemList &mainList,
        float cameraOffsetX,
        float cameraOffsetY,
        bool stereovisionActive,
        double stereovisionCameraX,
        double stereovisionCameraY,
        double stereovisionCameraZ) {
        double projectionOriginX = 0.0;
        double projectionOriginY = 0.0;
        if(stereovisionActive) {
            projectionOriginX = stereovisionCameraX +
                                static_cast<double>(cameraOffsetX);
            projectionOriginY = stereovisionCameraY +
                                static_cast<double>(cameraOffsetY);
        }

        // The post-prepare pass receives only the sorted main pointer-vector.
        // Auxiliary composite items are not part of this call boundary. Every
        // stored main pointer is trusted and is dereferenced on loop entry.
        for(auto *entryPtr : mainList) {
            auto &entry = *entryPtr;

            for(size_t i = 0; i < entry.corners.size(); i += 2) {
                translatePreparedPoint_guess(
                    entry.corners[i], entry.corners[i + 1],
                    cameraOffsetX, cameraOffsetY);
            }
            for(auto &point : entry.commandCompositeMeshPoints) {
                translatePreparedPoint_guess(
                    point.x, point.y, cameraOffsetX, cameraOffsetY);
            }
            if(entry.meshType == 1) {
                for(auto &point : entry.meshPoints) {
                    translatePreparedPoint_guess(
                        point.x, point.y, cameraOffsetX, cameraOffsetY);
                }
            }
            entry.paintBox[0] += cameraOffsetX;
            entry.paintBox[1] += cameraOffsetY;
            entry.paintBox[2] += cameraOffsetX;
            entry.paintBox[3] += cameraOffsetY;
            // The native loop does not consult the Web-side validity flag;
            // even the {1,1,-1,-1} null rectangle receives the offset.
            entry.viewport[0] += cameraOffsetX;
            entry.viewport[1] += cameraOffsetY;
            entry.viewport[2] += cameraOffsetX;
            entry.viewport[3] += cameraOffsetY;

            // C++ equality has the ordered IEEE behavior seen in all four
            // targets: signed zeros compare equal, while NaN enters the pass.
            if(!stereovisionActive ||
               entry.sortKey == stereovisionCameraZ) {
                continue;
            }

            constexpr float maximum =
                std::numeric_limits<float>::max();
            entry.paintBox = { maximum, maximum, -maximum, -maximum };

            for(size_t i = 0; i < entry.corners.size(); i += 2) {
                projectAndGrowPreparedPoint_guess(
                    entry.corners[i], entry.corners[i + 1], entry.sortKey,
                    projectionOriginX, projectionOriginY,
                    stereovisionCameraZ, entry.paintBox);
            }
            for(auto &point : entry.commandCompositeMeshPoints) {
                projectAndGrowPreparedPoint_guess(
                    point.x, point.y, entry.sortKey,
                    projectionOriginX, projectionOriginY,
                    stereovisionCameraZ, entry.paintBox);
            }
            if(entry.meshType == 1) {
                for(auto &point : entry.meshPoints) {
                    projectAndGrowPreparedPoint_guess(
                        point.x, point.y, entry.sortKey,
                        projectionOriginX, projectionOriginY,
                        stereovisionCameraZ, entry.paintBox);
                }
            }
            // commandBezierPatchPoints is the raw command payload and remains
            // untouched; viewport is translated above but never projected.
        }
    }

} // namespace motion::internal::render_detail

namespace motion {

    void Player::calcBounds() {
        // Keep an independent ResourceManager dispatch reference alive across
        // recursive child calls, even though the body never invokes it.
        tTJSVariant resourceManagerCopy(_resourceManager);
        RetainedDispatch_guess resourceManager(
            resourceManagerCopy.AsObject());
        resourceManagerCopy.Clear();

        // Path materialization belongs only to the opt-in Web diagnostic
        // sidecar. The native AABB pass does not read motion context here.
        // Keep the ordinary path free of unrelated Variant-to-string
        // conversion and allocation.
        std::string motionPath;
        const bool traceCalcBounds = detail::logoChainTraceEnabled();
        if(traceCalcBounds) {
            motionPath = matchedMotionPath();
        }

        _boundsMinX = std::numeric_limits<double>::max();
        _boundsMinY = std::numeric_limits<double>::max();
        _boundsMaxX = -std::numeric_limits<double>::max();
        _boundsMaxY = -std::numeric_limits<double>::max();

        auto mergeBounds = [&](double minX, double minY, double maxX,
                               double maxY) {
            if(minX <= _boundsMinX) _boundsMinX = minX;
            if(minY <= _boundsMinY) _boundsMinY = minY;
            if(maxX >= _boundsMaxX) _boundsMaxX = maxX;
            if(maxY >= _boundsMaxY) _boundsMaxY = maxY;
        };

        for(size_t nodeIndex = 1; nodeIndex < _nodes.size(); ++nodeIndex) {
            auto &node = _nodes[nodeIndex];

            // Particle children contribute before the active-slot gate and
            // the ordinary source path.
            if(!_preview && node.nodeType == 4) {
                // Retain this node's Array dispatch once across the count
                // read, all indexed lookups, and every recursive child pass.
                // A child callback may replace or clear the node Variant,
                // but that must not switch the receiver mid-traversal.
                detail::ScopedParticleArrayDispatch_guess particleArray(
                    node.particleArrayVar);
                auto *const array = particleArray.get();
                const int particleCount = static_cast<int>(
                    detail::particleArrayCount_guess(array));
                for(int particleIndex = 0; particleIndex < particleCount;
                    ++particleIndex) {
                    auto *child =
                        detail::particleArrayGetNativePlayerAt_guess(
                            array, static_cast<tjs_int>(particleIndex));
                    child->calcBounds();
                    mergeBounds(child->_boundsMinX, child->_boundsMinY,
                                child->_boundsMaxX, child->_boundsMaxY);
                }
            }

            // Type-3 and ordinary paths run only while the active clip slot is
            // not marked done.
            if(node.activeSlot().done) {
                continue;
            }

            // A non-preview nested-motion node copies its child's Player AABB
            // into its node AABB and merges that result directly.
            if(!_preview && node.nodeType == 3) {
                auto *child = node.getChildPlayer();
                child->calcBounds();
                node.bounds[0] = static_cast<float>(child->_boundsMinX);
                node.bounds[1] = static_cast<float>(child->_boundsMinY);
                node.bounds[2] = static_cast<float>(child->_boundsMaxX);
                node.bounds[3] = static_cast<float>(child->_boundsMaxY);
                mergeBounds(node.bounds[0], node.bounds[1], node.bounds[2],
                            node.bounds[3]);
                continue;
            }

            // The ordinary path gates on the preview-specific node-type mask
            // plus source.valid. drawFlag and drawnThisFrame are not inputs.
            const int visBitmaskCalc =
                _preview ? 0x1449 : 0x1441;
            if(((1 << node.nodeType) & visBitmaskCalc) == 0 ||
               !node.source.valid) {
                continue;
            }

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = -std::numeric_limits<float>::max();
            float maxY = -std::numeric_limits<float>::max();
            auto extendPoint = [&](float x, float y) {
                if(x <= minX) minX = x;
                if(y <= minY) minY = y;
                if(x >= maxX) maxX = x;
                if(y >= maxY) maxY = y;
            };

            node.bounds[0] = std::numeric_limits<float>::max();
            node.bounds[1] = std::numeric_limits<float>::max();
            node.bounds[2] = -std::numeric_limits<float>::max();
            node.bounds[3] = -std::numeric_limits<float>::max();

            // Composite points have priority; otherwise scan exactly sixteen
            // transformed mesh-control points. Two empty derived vectors fall
            // back to the four ordinary corners.
            if(!node.compositeMeshPoints.empty()) {
                for(const auto &point : node.compositeMeshPoints) {
                    extendPoint(point.x, point.y);
                }
            } else if(!node.transformedMeshControlPoints.empty()) {
                for(size_t pointIndex = 0; pointIndex < 16; ++pointIndex) {
                    const auto &point =
                        node.transformedMeshControlPoints[pointIndex];
                    extendPoint(point.x, point.y);
                }
            } else {
                for(int ci = 0; ci < 4; ++ci) {
                    extendPoint(node.vertices[ci * 2],
                                node.vertices[ci * 2 + 1]);
                }
            }

            const std::array<float, 4> expectedBounds = {
                std::floor(minX),
                std::floor(minY),
                std::ceil(maxX),
                std::ceil(maxY)
            };
            node.bounds[0] = expectedBounds[0];
            node.bounds[1] = expectedBounds[1];
            node.bounds[2] = expectedBounds[2];
            node.bounds[3] = expectedBounds[3];
            mergeBounds(node.bounds[0], node.bounds[1], node.bounds[2],
                        node.bounds[3]);
            if(traceCalcBounds &&
               detail::logoChainTraceEnabledForPath(motionPath)) {
                const std::array<float, 4> actualBounds = {
                    node.bounds[0], node.bounds[1], node.bounds[2],
                    node.bounds[3]
                };
                bool ok = true;
                for(size_t bi = 0; bi < expectedBounds.size(); ++bi) {
                    if(std::fabs(expectedBounds[bi] - actualBounds[bi]) >
                       0.01f) {
                        ok = false;
                        break;
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "calcBounds.node", "Player.calcBounds",
                    _clampedEvalTime,
                    fmt::format(
                        "from=minmax({:.3f},{:.3f},{:.3f},{:.3f}) exp=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        minX, minY, maxX, maxY, expectedBounds[0],
                        expectedBounds[1], expectedBounds[2],
                        expectedBounds[3]),
                    fmt::format(
                        "nodeIndex={} label={} act=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        node.index,
                        node.layerName.IsEmpty() ? std::string("<root>")
                                                 : detail::narrow(node.layerName),
                        actualBounds[0], actualBounds[1], actualBounds[2],
                        actualBounds[3]),
                    ok,
                    "Player_calcBounds produced an unexpected node AABB");
            }
        }

        if(traceCalcBounds) {
            detail::logoChainTraceLogf(
                motionPath, "calcBounds.player", "Player.calcBounds",
                _clampedEvalTime,
                "playerBounds=({:.3f},{:.3f},{:.3f},{:.3f}) ordered={}",
                _boundsMinX, _boundsMinY, _boundsMaxX, _boundsMaxY,
                (_boundsMaxX >= _boundsMinX &&
                 _boundsMaxY >= _boundsMinY) ? 1 : 0);
        }
    }

    void Player::appendPreparedRenderItems(
        std::vector<detail::PreparedRenderItem *> &mainList,
        std::vector<detail::PreparedRenderItem *> &auxList,
        std::uint32_t inheritedColor,
        bool inheritedDrawFlag19,
        bool inheritedFlag18) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        // The Android probe hooks this recursive builder, so every child call
        // has its own enter/leave pair. Keep the probe on this source boundary
        // rather than on the outer content-gate/build/sort wrapper.
        detail::motionTraceRenderBuildItemsEnter(
            this, inheritedColor, inheritedDrawFlag19, inheritedFlag18);
        struct MotionTraceBuildItemsScope {
            Player *player;
            const std::vector<detail::PreparedRenderItem *> &mainList;
            const std::vector<detail::PreparedRenderItem *> &auxList;
            ~MotionTraceBuildItemsScope() {
                detail::motionTraceRenderBuildItemsLeave(
                    player, mainList, auxList);
            }
        } motionTraceBuildItemsScope{this, mainList, auxList};
#endif

        auto &entries = mainList;
        const auto &nodes = _nodes;
        // Motion-path conversion and the trace/snapshot projections below are
        // Web diagnostics, not part of the native recursive builder. Keep the
        // ordinary recursive path free of TJS string conversion and logging
        // temporaries.
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        const bool logoSnapshotEnabled = detail::logoSnapshotMarkEnabled();
        std::string motionPath;
        if(logoTraceEnabled || logoSnapshotEnabled) {
            motionPath = matchedMotionPath();
        }
        const bool traceForPath =
            logoTraceEnabled &&
            detail::logoChainTraceEnabledForPath(motionPath);
        const bool snapshotForPath =
            logoSnapshotEnabled &&
            detail::logoSnapshotMarkEnabledForPath(motionPath);
        const bool snapshotWindow =
            snapshotForPath &&
            motionPath.find("m2logo.mtn") != std::string::npos &&
            _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0;
        const std::uint32_t effectiveColor =
            internal::multiplyPackedColorWeights_guess(
                inheritedColor, _colorWeightPacked);

        // All four builders take this gate after color multiplication but
        // before copying/converting the priority-content owner.
        if(nodes.size() < 2) {
            return;
        }

        // CopyRef the persistent Variant, convert the copy to an independently
        // retained dispatch, then clear the copy. Numeric getters can replace
        // Player's persistent Variant re-entrantly without changing the
        // dispatch used by later iterations of this build.
        tTJSVariant priorityContentCopy(_rootContentVariant);
        RetainedDispatch_guess priorityContent(
            priorityContentCopy.AsObject());
        priorityContentCopy.Clear();
        const auto priorityNodeAt =
            [&](tjs_int position) -> tjs_int {
                tTJSVariant value;
                (void)priorityContent->PropGetByNum(
                    0, position, &value, priorityContent.get());
                return static_cast<tjs_int>(value.AsInteger());
            };

        // The recursive builder reads the script-visible preview property for
        // these node-type masks.
        const int bitmask = _preview ? 5193 : 5185;
        // The builder dereferences the canonical root owner before every
        // draw-affine read. Recursive child Players therefore
        // consume the top-level render owner's matrix, not their own ctor
        // identity matrix.
        const auto &drawAffineOwner = *_rootPlayer;
        const bool drawAffineMatrixNonIdentity =
            drawAffineOwner._drawAffineMatrixNonIdentity;

        auto appendChildEntriesAtCurrentNode =
            [&](const detail::MotionNode &ownerNode, Player *child,
                std::vector<detail::PreparedRenderItem *> &childMainList,
                bool childDrawFlag19) {
            const size_t mainCountBefore = childMainList.size();
            const size_t auxCountBefore = auxList.size();
            // The recursive child flag is the caller's inherited flag OR'd
            // with this owner node's one-byte priorDraw value. It never reads
            // the independent Player::_priorDraw property.
            // The recursive call receives the caller's same main/aux vectors;
            // there is no child-local prepare/sort stage.
            child->appendPreparedRenderItems(
                childMainList, auxList,
                (ownerNode.inheritFlags & 0x200) != 0
                    ? effectiveColor
                    : 0xFF808080u,
                childDrawFlag19,
                inheritedFlag18 || ownerNode.priorDraw);
            std::string childMotionPath;
            if(traceForPath || snapshotWindow) {
                childMotionPath = child->matchedMotionPath();
            }
            if(snapshotWindow) {
                std::fprintf(
                    stderr,
                    "SNAPCHILD phase=prepare frame=%.3f childActiveMotion=%s childMotionKey=%s childNodesBuilt=%d childNodeCount=%zu childPreparedItemCount=%zu firstSource=%s\n",
                    _clampedEvalTime,
                    childMotionPath.empty()
                        ? "<none>" : childMotionPath.c_str(),
                    detail::narrow(child->getMotion()).c_str(),
                    child->_nodes.size() > 1 ? 1 : 0,
                    child->_nodes.size(),
                    childMainList.size() - mainCountBefore,
                    childMainList.size() == mainCountBefore ||
                            !childMainList[mainCountBefore] ||
                            childMainList[mainCountBefore]->sourceKey.empty()
                        ? "<none>"
                        : childMainList[mainCountBefore]->sourceKey.c_str());
            }
            if(traceForPath) {
                detail::logoChainTraceLogf(
                    motionPath, "prepare.childMerge",
                    "Player.appendPreparedRenderItems",
                    _clampedEvalTime,
                    "childMotionPath={} mainAdded={} auxAdded={} parentMainTotal={}",
                    childMotionPath.empty()
                        ? std::string("<none>") : childMotionPath,
                    childMainList.size() - mainCountBefore,
                    auxList.size() - auxCountBefore, childMainList.size());
            }
        };

        auto transformPoint = [&](float x, float y) -> tTVPPointD {
            return {
                drawAffineOwner._drawAffineM11 * static_cast<double>(x) +
                    drawAffineOwner._drawAffineM12 * static_cast<double>(y) +
                    drawAffineOwner._drawAffineM14,
                drawAffineOwner._drawAffineM21 * static_cast<double>(x) +
                    drawAffineOwner._drawAffineM22 * static_cast<double>(y) +
                    drawAffineOwner._drawAffineM24
            };
        };

        auto transformAndRoundPreparedRect_guess =
            [&](const std::array<float, 4> &rect) {
                const auto p0 = transformPoint(rect[0], rect[1]);
                const auto p1 = transformPoint(rect[2], rect[1]);
                const auto p2 = transformPoint(rect[2], rect[3]);
                const auto p3 = transformPoint(rect[0], rect[3]);

                // Every double point result is narrowed before the native
                // float comparisons and floorf/ceilf operations. The compare
                // selects the right operand when unordered (and for equal
                // signed zeros), unlike std::min/std::max.
                const auto minFloat = [](float lhs, float rhs) {
                    return lhs < rhs ? lhs : rhs;
                };
                const auto maxFloat = [](float lhs, float rhs) {
                    return lhs > rhs ? lhs : rhs;
                };
                const float p0x = static_cast<float>(p0.x);
                const float p0y = static_cast<float>(p0.y);
                const float p1x = static_cast<float>(p1.x);
                const float p1y = static_cast<float>(p1.y);
                const float p2x = static_cast<float>(p2.x);
                const float p2y = static_cast<float>(p2.y);
                const float p3x = static_cast<float>(p3.x);
                const float p3y = static_cast<float>(p3.y);
                const float minX = minFloat(
                    minFloat(minFloat(p0x, p1x), p2x), p3x);
                const float minY = minFloat(
                    minFloat(minFloat(p0y, p1y), p2y), p3y);
                const float maxX = maxFloat(
                    maxFloat(maxFloat(p0x, p1x), p2x), p3x);
                const float maxY = maxFloat(
                    maxFloat(maxFloat(p0y, p1y), p2y), p3y);
                return std::array<float, 4>{
                    std::floor(minX), std::floor(minY),
                    std::ceil(maxX), std::ceil(maxY)
                };
            };

        constexpr std::array<float, 4> kInvalidPreparedPaintBox = {
            1.0f, 1.0f, -1.0f, -1.0f
        };

        // The recursive builder does not walk deque order. For each logical
        // non-root slot it reads the retained priority content in reverse,
        // applies the native 32-bit +1 wrap, and selects that node.
        // drawnThisFrame is cleared only after selection, preserving the
        // duplicate/omitted-index boundary behavior.
        for(size_t logicalIndex = 1;
            logicalIndex < nodes.size(); ++logicalIndex) {
            const auto priorityPosition = static_cast<tjs_int>(
                nodes.size() - logicalIndex - 1);
            const auto rawIndex = priorityNodeAt(priorityPosition);
            const auto i = static_cast<tjs_int>(
                static_cast<tjs_uint32>(rawIndex) + 1u);
            // Like native deque iterator arithmetic, this is unchecked.
            auto &node = _nodes[static_cast<size_t>(i)];
            node.drawnThisFrame = false;
            if(!_preview) {
                // Particle recursion precedes the selected node's active gate
                // and writes directly into the caller's shared lists.
                if(node.nodeType == 4) {
                    // One independently retained Array dispatch spans the
                    // count read and every indexed child lookup. Re-entrant
                    // mutation of the node Variant does not switch receiver.
                    detail::ScopedParticleArrayDispatch_guess particleArray(
                        node.particleArrayVar);
                    auto *const array = particleArray.get();
                    const int particleCount = static_cast<int>(
                        detail::particleArrayCount_guess(array));
                    for(int pi = 0; pi < particleCount; ++pi) {
                        appendChildEntriesAtCurrentNode(
                            node,
                            detail::particleArrayGetNativePlayerAt_guess(
                                array, static_cast<tjs_int>(pi)),
                            mainList,
                            inheritedDrawFlag19);
                    }
                    continue;
                }
            }
            if(!node.accumulated.active) continue;
            if(!_preview && node.nodeType == 3) {
                auto *const child = node.getChildPlayer();
                if(!node.drawFlag &&
                   !node.stencilCompositeMaskReferenced) {
                    // A plain type-3 node
                    // contributes its child's items directly to the caller's
                    // main/aux lists and never creates a wrapper item.
                    appendChildEntriesAtCurrentNode(
                        node, child, mainList, inheritedDrawFlag19);
                    continue;
                }

                // drawFlag/maskRef selects
                // the persistent node-owned type-3 wrapper path. The wrapper
                // itself is never inserted into mainList; its child vector is
                // populated first and then range-inserted into caller main.
                node.drawnThisFrame = true;
                if(!node.preparedRenderItem) {
                    node.preparedRenderItem =
                        new detail::PreparedRenderItem();
                }
                auto &wrapper = *node.preparedRenderItem;
                // The builder copies the node label on every wrapper
                // population, not only when the persistent item is allocated.
                wrapper.ownerLabel = node.layerName;
                wrapper.nodeIndex = static_cast<int>(i);
                // The type-3 wrapper has no ordinary source publication. Its
                // native borrowed source pointer remains dormant on a fresh
                // item, or retains the value left by an earlier item path.
                wrapper.hasOwnSource = false;
                wrapper.drawFlag = false;
                wrapper.stencilComposite = node.stencilType;
                wrapper.paintBox = {
                    node.bounds[0], node.bounds[1],
                    node.bounds[2], node.bounds[3]
                };
                if(node.clipAABB) {
                    wrapper.viewport = {
                        node.clipAABB[0], node.clipAABB[1],
                        node.clipAABB[2], node.clipAABB[3]
                    };
                    wrapper.hasViewport =
                        node.clipAABB[2] >= node.clipAABB[0] &&
                        node.clipAABB[3] >= node.clipAABB[1];
                } else {
                    wrapper.viewport = kInvalidPreparedPaintBox;
                    wrapper.hasViewport = false;
                }
                if(drawAffineMatrixNonIdentity) {
                    if(wrapper.hasViewport) {
                        wrapper.viewport =
                            transformAndRoundPreparedRect_guess(
                                wrapper.viewport);
                    }
                    wrapper.paintBox =
                        transformAndRoundPreparedRect_guess(wrapper.paintBox);
                }

                detail::PreparedRenderItem *wrapperParentItem = nullptr;
                if(node.drawFlag) {
                    auxList.push_back(&wrapper);
                    // The native field is a nullable raw MotionNode pointer.
                    // This portable index uses only -1 as null; every other
                    // value is selected without a range or self guard.
                    if(node.visibleAncestorIndex != -1) {
                        auto &ancestor = _nodes[static_cast<size_t>(
                            node.visibleAncestorIndex)];
                        if(!ancestor.preparedRenderItem) {
                            ancestor.preparedRenderItem =
                                new detail::PreparedRenderItem();
                        }
                        wrapperParentItem = ancestor.preparedRenderItem;
                    }
                }
                // This store occurs after aux growth and ancestor-item ensure.
                // If either throws, the persistent wrapper keeps its previous
                // parent pointer just as the native partially updated item.
                wrapper.parentItem = wrapperParentItem;

                wrapper.childItems.clear();
                appendChildEntriesAtCurrentNode(
                    node, child, wrapper.childItems, true);
                mainList.insert(mainList.end(),
                                wrapper.childItems.begin(),
                                wrapper.childItems.end());
                continue;
            }
            const bool hasOwnSource = node.source.valid;
            if(!node.forceVisible &&
               (((1 << node.nodeType) & bitmask) == 0)) {
                continue;
            }
            if(!hasOwnSource) continue;

            // Admission publishes this persistent node byte before lazy item
            // allocation or any item-field/string/vector operation. An
            // exception below therefore leaves the node marked drawn even
            // when it never reaches the caller's main vector.
            node.drawnThisFrame = true;

            // Each node owns one persistent raw item;
            // allocate only once, then overwrite the fields reached by this
            // population path without reconstructing the object.
            if(!node.preparedRenderItem) {
                node.preparedRenderItem = new detail::PreparedRenderItem();
            }
            auto &entry = *node.preparedRenderItem;
            // The builder refreshes the independent owner string on
            // every ordinary-item population.
            entry.ownerLabel = node.layerName;
            entry.nodeIndex = static_cast<int>(i);
            // The native flag prefix is published before command-key
            // conversion and before any numeric/source/paint suffix.
            entry.skipFlag0 =
                (((_preview ? 1097 : 1089) & (1 << node.nodeType)) == 0);
            entry.rawFlag16 = node.source.blank;
            entry.skipFlag1 = inheritedFlag18 || node.priorDraw;
            // Coerce the persistent context Variant to a temporary ttstr, then
            // copy that independently owning string into the render item.
            entry.commandKey = ttstr(_findMotionContextVariant);
            entry.layerId1 = node.layerId1;
            entry.layerId2 = node.layerId2;
            // The render-item sort-key field stores accumulated posZ, while
            // coordinateMode and
            // objTriPriority occupy independent integer fields.
            entry.sortKey = node.accumulated.posZ;
            entry.coordinateMode = node.coordinateMode;
            entry.objTriPriority = node.objTriPriority;
            entry.commandCoord = {
                node.accumulated.posX,
                _zFactor * node.accumulated.posZ + node.accumulated.posY,
            };
            entry.originX = node.source.originX + node.activeSlot().ox;
            entry.originY = node.source.originY + node.activeSlot().oy;
            entry.commandMatrix = {
                node.accumulated.m11,
                node.accumulated.m12,
                node.accumulated.m21,
                node.accumulated.m22,
            };
            entry.packedColors = copyPackedColorsFromBytes(node.colorBytes);
            for(auto &packedColor : entry.packedColors) {
                packedColor = internal::multiplyPackedColorWeights_guess(
                    packedColor, effectiveColor);
            }
            // The builder next passes the persistent source descriptor and
            // accumulated four-color array to the source-clip remapper.
            remapPackedColorsForSourceClip_guess(
                node.source, entry.packedColors);

            // Raw node corners are copied after color remap and before the
            // active-slot source owner. This order is observable when a later
            // owner/vector operation fails on a reused persistent item.
            for(int ci = 0; ci < 4; ++ci) {
                entry.corners[ci * 2] = node.vertices[ci * 2];
                entry.corners[ci * 2 + 1] = node.vertices[ci * 2 + 1];
            }

            // The active clip-slot string is copied independently of the Web
            // source object and owns its prepared-item storage.
            entry.commandSrc = node.activeSlot().srcValue;
            // Render construction reads the active clip slot directly; the
            // timeline evaluator has no accumulated blend-mode output field.
            entry.blendMode = node.activeSlot().blendMode;
            entry.opacity = node.accumulated.opacity;
            // Native publication of the borrowed descriptor is late: it
            // follows corners/source/blend/opacity rather than the owner-label
            // prefix. Web diagnostic sidecars are refreshed separately below.
            entry.sourceState = &node.source;
            entry.stencilComposite = node.stencilType;
            // The recursive builder combines the node-local draw causes with
            // the flag inherited from the outer/root traversal.
            entry.drawFlag =
                node.drawFlag || node.stencilCompositeMaskReferenced ||
                inheritedDrawFlag19;
            entry.hasOwnSource = hasOwnSource;

            // The native nullable ancestor pointer is consumed after the
            // source/color/opacity/stencil writes and before copying the raw
            // paint/clip geometry. The portable index has exactly one null
            // sentinel (-1); all other values, including self, are unchecked.
            if(node.visibleAncestorIndex != -1) {
                auto &ancestor = _nodes[
                    static_cast<size_t>(node.visibleAncestorIndex)];
                if(!ancestor.preparedRenderItem) {
                    ancestor.preparedRenderItem =
                        new detail::PreparedRenderItem();
                }
                entry.parentItem = ancestor.preparedRenderItem;
            } else {
                entry.parentItem = nullptr;
            }
            entry.visibleAncestorIndex = node.visibleAncestorIndex;

            // The paint box is an independent raw node AABB, not a reduction
            // of the item corner or mesh arrays. It is copied before the
            // pointer-backed viewport and later transformed in place.
            entry.paintBox = {
                node.bounds[0], node.bounds[1],
                node.bounds[2], node.bounds[3]
            };

            // A non-null clip pointer is copied before its ordering check.
            // Invalid pointer-backed coordinates therefore remain observable;
            // only a null pointer installs the native invalid sentinel.
            if(node.clipAABB) {
                entry.viewport = {
                    node.clipAABB[0], node.clipAABB[1],
                    node.clipAABB[2], node.clipAABB[3]
                };
                entry.hasViewport =
                    node.clipAABB[2] >= node.clipAABB[0] &&
                    node.clipAABB[3] >= node.clipAABB[1];
            } else {
                entry.viewport = kInvalidPreparedPaintBox;
                entry.hasViewport = false;
            }
            entry.meshType = node.meshType;
            entry.meshDivX = node.meshDivX;
            entry.meshDivY = node.meshDivY;

            // Three independent vector owners: composite points are always
            // assigned (and therefore also cleared), while processed and raw
            // Bezier points are populated only for the type-1 mesh branch.
            entry.commandCompositeMeshPoints = node.compositeMeshPoints;
            if(!entry.commandCompositeMeshPoints.empty()) {
                entry.meshType = 2;
            } else if(node.meshType == 1) {
                if(node.meshControlPoints.empty()) {
                    entry.meshType = 0;
                } else {
                    entry.commandPatchDivision =
                        prepareBezierPatchDivision_guess(
                            getMeshDivisionRatio(),
                            static_cast<std::uint32_t>(node.meshDivision));
                    entry.meshPoints =
                        node.transformedMeshControlPoints;
                    entry.commandBezierPatchPoints =
                        node.meshControlPoints;
                }
            }

            // This is one late native stage: corners, the composite-mesh
            // vector and the processed mesh vector are mutated in place. The
            // raw Bezier control-point vector is deliberately excluded.
            if(drawAffineMatrixNonIdentity) {
                for(int ci = 0; ci < 4; ++ci) {
                    const auto transformed = transformPoint(
                        entry.corners[ci * 2], entry.corners[ci * 2 + 1]);
                    entry.corners[ci * 2] =
                        static_cast<float>(transformed.x);
                    entry.corners[ci * 2 + 1] =
                        static_cast<float>(transformed.y);
                }
                for(auto &point : entry.commandCompositeMeshPoints) {
                    const auto transformed = transformPoint(point.x, point.y);
                    point.x = static_cast<float>(transformed.x);
                    point.y = static_cast<float>(transformed.y);
                }
                for(auto &point : entry.meshPoints) {
                    const auto transformed = transformPoint(point.x, point.y);
                    point.x = static_cast<float>(transformed.x);
                    point.y = static_cast<float>(transformed.y);
                }
            }

            if(drawAffineMatrixNonIdentity && entry.hasViewport) {
                entry.viewport =
                    transformAndRoundPreparedRect_guess(entry.viewport);
            }

            if(drawAffineMatrixNonIdentity) {
                entry.paintBox =
                    transformAndRoundPreparedRect_guess(entry.paintBox);
            }

            // Port-only narrow diagnostic snapshot. Keep its potentially
            // allocating conversion after the complete native item overwrite
            // so it cannot change which native prefix is committed by an
            // earlier command-key, ancestor, or mesh failure.
            entry.sourceKey = detail::narrow(node.source.path);

            if(traceForPath) {
                const std::array<float, 8> expectedCorners = {
                    static_cast<float>(drawAffineOwner._drawAffineM11 *
                                           static_cast<double>(node.vertices[0]) +
                                       drawAffineOwner._drawAffineM12 *
                                           static_cast<double>(node.vertices[1]) +
                                       drawAffineOwner._drawAffineM14),
                    static_cast<float>(drawAffineOwner._drawAffineM21 *
                                           static_cast<double>(node.vertices[0]) +
                                       drawAffineOwner._drawAffineM22 *
                                           static_cast<double>(node.vertices[1]) +
                                       drawAffineOwner._drawAffineM24),
                    static_cast<float>(drawAffineOwner._drawAffineM11 *
                                           static_cast<double>(node.vertices[2]) +
                                       drawAffineOwner._drawAffineM12 *
                                           static_cast<double>(node.vertices[3]) +
                                       drawAffineOwner._drawAffineM14),
                    static_cast<float>(drawAffineOwner._drawAffineM21 *
                                           static_cast<double>(node.vertices[2]) +
                                       drawAffineOwner._drawAffineM22 *
                                           static_cast<double>(node.vertices[3]) +
                                       drawAffineOwner._drawAffineM24),
                    static_cast<float>(drawAffineOwner._drawAffineM11 *
                                           static_cast<double>(node.vertices[4]) +
                                       drawAffineOwner._drawAffineM12 *
                                           static_cast<double>(node.vertices[5]) +
                                       drawAffineOwner._drawAffineM14),
                    static_cast<float>(drawAffineOwner._drawAffineM21 *
                                           static_cast<double>(node.vertices[4]) +
                                       drawAffineOwner._drawAffineM22 *
                                           static_cast<double>(node.vertices[5]) +
                                       drawAffineOwner._drawAffineM24),
                    static_cast<float>(drawAffineOwner._drawAffineM11 *
                                           static_cast<double>(node.vertices[6]) +
                                       drawAffineOwner._drawAffineM12 *
                                           static_cast<double>(node.vertices[7]) +
                                       drawAffineOwner._drawAffineM14),
                    static_cast<float>(drawAffineOwner._drawAffineM21 *
                                           static_cast<double>(node.vertices[6]) +
                                       drawAffineOwner._drawAffineM22 *
                                           static_cast<double>(node.vertices[7]) +
                                       drawAffineOwner._drawAffineM24)
                };
                const auto effectiveColor = unpackPackedRgba(entry.packedColors[0]);
                detail::logoChainTraceLogf(
                    motionPath, "prepare.item",
                    "Player.appendPreparedRenderItems", _clampedEvalTime,
                    "nodeIndex={} src={} blend={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] meshType={} meshDiv=({},{}) sortKey={:.3f} coordinateMode={} objTriPriority={} layerId=({}, {}) nodeDrawFlag={} maskRef={} itemDrawFlag={} visibleAncestorIndex={} slotDone={} frameType={} stencilType={}",
                    entry.nodeIndex,
                    entry.sourceKey.empty() ? std::string("<none>")
                                            : entry.sourceKey,
                    entry.blendMode, entry.opacity, entry.packedColors[0],
                    entry.packedColors[1], entry.packedColors[2],
                    entry.packedColors[3], effectiveColor[0],
                    effectiveColor[1], effectiveColor[2], effectiveColor[3],
                    entry.meshType, entry.meshDivX, entry.meshDivY,
                    entry.sortKey, entry.coordinateMode, entry.objTriPriority,
                    entry.layerId1, entry.layerId2, node.drawFlag ? 1 : 0,
                    node.stencilCompositeMaskReferenced ? 1 : 0,
                    entry.drawFlag ? 1 : 0, entry.visibleAncestorIndex,
                    node.activeSlot().done ? 1 : 0,
                    node.activeSlot().done
                        ? 0
                        : (node.activeSlot().crossfading ? 3 : 2),
                    node.stencilType);
                bool cornersOk = node.source.width <= 0.0 &&
                    node.source.height <= 0.0;
                if(!cornersOk) {
                    cornersOk = true;
                    for(size_t ci = 0; ci < expectedCorners.size(); ++ci) {
                        if(std::fabs(entry.corners[ci] - expectedCorners[ci]) >
                           0.01f) {
                            cornersOk = false;
                            break;
                        }
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "prepare.corners",
                    "Player.appendPreparedRenderItems",
                    _clampedEvalTime,
                    fmt::format(
                        "drawAffine*vertices exp=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        expectedCorners[0], expectedCorners[1],
                        expectedCorners[2], expectedCorners[3],
                        expectedCorners[4], expectedCorners[5],
                        expectedCorners[6], expectedCorners[7]),
                    fmt::format(
                        "nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex, entry.corners[0], entry.corners[1],
                        entry.corners[2], entry.corners[3], entry.corners[4],
                        entry.corners[5], entry.corners[6], entry.corners[7]),
                    cornersOk,
                    "PreparedRenderItem corners diverged from drawAffineMatrix * node.vertices");
                detail::logoChainTraceCheck(
                    motionPath, "prepare.paintBox",
                    "Player.appendPreparedRenderItems",
                    _clampedEvalTime,
                    fmt::format(
                        "paintBox from transformed geometry exp=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.paintBox[0], entry.paintBox[1],
                        entry.paintBox[2], entry.paintBox[3]),
                    fmt::format(
                        "nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex, entry.paintBox[0], entry.paintBox[1],
                        entry.paintBox[2], entry.paintBox[3]),
                    true,
                    "PreparedRenderItem paintBox diverged from transformed geometry");
                detail::logoChainTraceCheck(
                    motionPath, "prepare.viewport",
                    "Player.appendPreparedRenderItems",
                    _clampedEvalTime,
                    entry.hasViewport
                        ? fmt::format(
                              "parent shapeAABB chain exp=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                              entry.viewport[0], entry.viewport[1],
                              entry.viewport[2], entry.viewport[3])
                        : std::string(
                              "parent shapeAABB chain exp=<invalid default>"),
                    entry.hasViewport
                        ? fmt::format(
                              "nodeIndex={} act=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                              entry.nodeIndex, entry.viewport[0],
                              entry.viewport[1], entry.viewport[2],
                              entry.viewport[3])
                        : fmt::format("nodeIndex={} act=<invalid default>",
                                      entry.nodeIndex),
                    true,
                    "PreparedRenderItem viewport propagation diverged from parent clip chain");
            }

            if(snapshotWindow && _clampedEvalTime >= 43.0 &&
               (entry.nodeIndex == 14 || entry.nodeIndex == 15 ||
                entry.nodeIndex == 19 ||
                (entry.nodeIndex >= 20 && entry.nodeIndex <= 29))) {
                std::fprintf(
                    stderr,
                    "SNAPPREP frame=%.3f nodeIndex=%d source=%s hasOwnSource=%d rawFlags=[%d,%d,%d,%d] priorDraw=%d inherited18=%d sortKey=%.3f coordinateMode=%d objTriPriority=%d visibleAncestorIndex=%d drawFlag=%d opacity=%d paintBox=[%.1f,%.1f,%.1f,%.1f] viewport=%s\n",
                    _clampedEvalTime, entry.nodeIndex,
                    entry.sourceKey.empty() ? "<none>" : entry.sourceKey.c_str(),
                    entry.hasOwnSource ? 1 : 0,
                    entry.rawFlag16 ? 1 : 0, entry.skipFlag0 ? 1 : 0,
                    entry.skipFlag1 ? 1 : 0, entry.drawFlag ? 1 : 0,
                    node.priorDraw, inheritedFlag18 ? 1 : 0, entry.sortKey,
                    entry.coordinateMode, entry.objTriPriority,
                    entry.visibleAncestorIndex, entry.drawFlag ? 1 : 0,
                    entry.opacity, entry.paintBox[0], entry.paintBox[1],
                    entry.paintBox[2], entry.paintBox[3],
                    entry.hasViewport
                        ? fmt::format("[{:.1f},{:.1f},{:.1f},{:.1f}]",
                                      entry.viewport[0], entry.viewport[1],
                                      entry.viewport[2], entry.viewport[3])
                              .c_str()
                        : "<invalid>");
            }

            entries.push_back(&entry);
        }

        // This is a distinct node-order post-pass. A qualifying type-12 item
        // has already entered mainList; the same borrowed pointer is also
        // appended to auxList. Its child vector is rebuilt solely from the
        // raw stencilCompositeMaskNodes sequence, retaining duplicates.
        for(size_t i = 1; i < _nodes.size(); ++i) {
            auto &node = _nodes[i];
            if(node.nodeType != 12 || (node.stencilType & 4) == 0 ||
               !node.drawnThisFrame) {
                continue;
            }
            if(!node.preparedRenderItem) {
                node.preparedRenderItem = new detail::PreparedRenderItem();
            }
            auto *const parentItem = node.preparedRenderItem;
            auxList.push_back(parentItem);

            parentItem->childItems.clear();
            parentItem->childItems.push_back(parentItem);
            for(auto *maskNode : node.stencilCompositeMaskNodes) {
                // The native pointer vector is trusted and each element is
                // dereferenced immediately. A null entry is not tolerated.
                if(!maskNode->drawnThisFrame) {
                    continue;
                }
                if(maskNode->nodeType != 0 && maskNode->nodeType != 3) {
                    continue;
                }
                if(!maskNode->preparedRenderItem) {
                    maskNode->preparedRenderItem =
                        new detail::PreparedRenderItem();
                }
                auto *const maskItem = maskNode->preparedRenderItem;
                if(maskNode->nodeType == 0 || _preview) {
                    parentItem->childItems.push_back(maskItem);
                } else {
                    parentItem->childItems.insert(
                        parentItem->childItems.end(),
                        maskItem->childItems.begin(),
                        maskItem->childItems.end());
                }
            }
        }
    }

    bool Player::prepareRenderItems(
        detail::PreparedRenderItemList &mainList,
        detail::PreparedRenderItemList &auxList) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderPrepareEnter(this);
#endif
        // The outer wrapper owns the motion-content type-tag gate. The
        // recursive builder has no equivalent early return.
        if(!hasMotionContent()) {
#if defined(KRKR2_WASMTIME_HEADLESS)
            detail::motionTraceRenderPrepareLeave(
                this, false, mainList, auxList);
#endif
            return false;
        }
        const bool logoTraceEnabled = detail::logoChainTraceEnabled();
        const bool logoSnapshotEnabled = detail::logoSnapshotMarkEnabled();
        std::string motionPath;
        if(logoTraceEnabled || logoSnapshotEnabled) {
            motionPath = matchedMotionPath();
        }
        const bool traceForPath =
            logoTraceEnabled &&
            detail::logoChainTraceEnabledForPath(motionPath);
        const bool snapshotForPath =
            logoSnapshotEnabled &&
            detail::logoSnapshotMarkEnabledForPath(motionPath);

        // The outer wrapper passes neutral color and two false lineage flags
        // into the recursive builder. Callers construct both vectors empty.
        appendPreparedRenderItems(
            mainList,
            auxList,
            0xFF808080u, false, false);
        // The four native wrappers allocate only their implementation's
        // stable-sort pointer buffer. This parallel double-vector exists only
        // for the opt-in ordering trace and must not be built otherwise.
        std::optional<std::vector<double>> beforeSortKeys;
        if(traceForPath) {
            beforeSortKeys.emplace();
            beforeSortKeys->reserve(mainList.size());
            for(const auto *item : mainList) {
                beforeSortKeys->push_back(item ? item->sortKey : 0.0);
            }
        }
        // Stable-sort by the render-item sort key.
        std::stable_sort(
            mainList.begin(),
            mainList.end(),
            [](const detail::PreparedRenderItem *lhs,
               const detail::PreparedRenderItem *rhs) {
                // The native comparator immediately dereferences both trusted
                // raw pointers and performs one ordered double less-than.
                return lhs->sortKey < rhs->sortKey;
            });
        if(traceForPath) {
            std::ostringstream beforeSort;
            std::ostringstream afterSort;
            for(size_t i = 0; i < beforeSortKeys->size(); ++i) {
                if(i) beforeSort << ",";
                beforeSort << (*beforeSortKeys)[i];
            }
            for(size_t i = 0; i < mainList.size(); ++i) {
                if(i) afterSort << ",";
                afterSort << (mainList[i] ? mainList[i]->sortKey : 0.0);
            }
            detail::logoChainTraceLogf(
                motionPath, "prepare.sort", "Player.prepareRenderItems",
                _clampedEvalTime,
                "itemCount={} sortKeysBefore=[{}] sortKeysAfter=[{}]",
                mainList.size(), beforeSort.str(),
                afterSort.str());
        }

        if(snapshotForPath &&
           motionPath.find("m2logo.mtn") != std::string::npos &&
            _clampedEvalTime >= 43.0 && _clampedEvalTime <= 50.0) {
            for(size_t i = 0; i < mainList.size(); ++i) {
                const auto *itemPtr = mainList[i];
                if(!itemPtr) {
                    continue;
                }
                const auto &item = *itemPtr;
                if(!(item.nodeIndex == 14 || item.nodeIndex == 15 ||
                     item.nodeIndex == 19 ||
                     (item.nodeIndex >= 20 && item.nodeIndex <= 29))) {
                    continue;
                }
                std::fprintf(
                    stderr,
                    "SNAPPREPORDER frame=%.3f order=%zu nodeIndex=%d source=%s sortKey=%.3f visibleAncestorIndex=%d parentNodeIndex=%d childCount=%zu coordinateMode=%d objTriPriority=%d\n",
                    _clampedEvalTime, i, item.nodeIndex,
                    item.sourceKey.empty() ? "<none>" : item.sourceKey.c_str(),
                    item.sortKey, item.visibleAncestorIndex,
                    item.parentItem ? item.parentItem->nodeIndex : -1,
                    item.childItems.size(), item.coordinateMode,
                    item.objTriPriority);
            }
        }
        // The wrapper returns true whenever the motion-content type tag is
        // nonzero, even when mainList is empty.
        const bool ok = true;
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderPrepareLeave(this, ok, mainList, auxList);
#endif
        return ok;
    }

    void Player::applyPreparedRenderItemProjection_guess(
        detail::PreparedRenderItemList &mainList) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderApplyProjectionEnter(this);
#endif
        internal::render_detail::applyPreparedRenderItemProjectionCore_guess(
            mainList,
            _cameraOffsetX,
            _cameraOffsetY,
            _stereovisionActive,
            _stereovisionCameraX_guess,
            _stereovisionCameraY_guess,
            _stereovisionCameraZ_guess);
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderApplyProjectionLeave(this, mainList);
#endif
    }

} // namespace motion
