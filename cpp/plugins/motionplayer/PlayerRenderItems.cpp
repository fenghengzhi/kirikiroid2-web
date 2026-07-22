// PlayerRenderItems.cpp — calcBounds and prepared render-item build
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "MotionTraceWeb.h"

#include <spdlog/spdlog.h>

using namespace motion::internal;

namespace {

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

    // sub_6C2334 @0x6C2384..0x6C245C and 0x6C34A4..0x6C3588:
    // 0xFF808080 is the identity weight. RGB channels use the native /128
    // weight domain and saturate to 255, while alpha uses ordinary /255
    // multiplication (the compiler emits the 0x80808081 high-word sequence).
    inline std::uint32_t multiplyPackedColorWeightsLike_0x6C2334(
        std::uint32_t lhs, std::uint32_t rhs) {
        constexpr std::uint32_t identity = 0xFF808080u;
        if(lhs == identity) return rhs;
        if(rhs == identity) return lhs;

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
        return result;
    }

} // anonymous namespace

namespace motion {

    void Player::calcBounds() {
        // Equivalent to sub_6D5164 @ 0x6D5178's `player+544` null gate —
        // without a loaded motion there is no render list to measure.
        if(!hasMotionContent()) {
            _boundsMinX = 0.0;
            _boundsMinY = 0.0;
            _boundsMaxX = 0.0;
            _boundsMaxY = 0.0;
            return;
        }
        const auto motionPath = matchedMotionPath();

        _boundsMinX = 1e308;
        _boundsMinY = 1e308;
        _boundsMaxX = -1e308;
        _boundsMaxY = -1e308;

        bool haveBounds = false;
        auto mergeBounds = [&](double minX, double minY, double maxX,
                               double maxY) {
            if(minX > maxX || minY > maxY) {
                return;
            }
            if(!haveBounds) {
                _boundsMinX = minX;
                _boundsMinY = minY;
                _boundsMaxX = maxX;
                _boundsMaxY = maxY;
                haveBounds = true;
                return;
            }
            if(minX < _boundsMinX) _boundsMinX = minX;
            if(minY < _boundsMinY) _boundsMinY = minY;
            if(maxX > _boundsMaxX) _boundsMaxX = maxX;
            if(maxY > _boundsMaxY) _boundsMaxY = maxY;
        };

        for(size_t nodeIndex = 1; nodeIndex < _nodes.size(); ++nodeIndex) {
            auto &node = _nodes[nodeIndex];

            // 0x6C3F08..0x6C4018: particle children contribute before the
            // active-slot gate and the ordinary source path.
            if(!_preview && node.nodeType == 4) {
                const int particleCount = node.getParticleCount();
                for(int particleIndex = 0; particleIndex < particleCount;
                    ++particleIndex) {
                    auto *child = node.getParticleChild(particleIndex);
                    child->calcBounds();
                    mergeBounds(child->_boundsMinX, child->_boundsMinY,
                                child->_boundsMaxX, child->_boundsMaxY);
                }
            }

            // 0x6C4028: the type-3 and ordinary paths run only while the
            // active clip slot's +344 byte is zero.
            if(node.activeSlot().done) {
                continue;
            }

            // 0x6C4040..0x6C4278: a non-preview motion node copies its child
            // Player bounds into node+1888 and merges them directly.
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

            // Aligned to Player_calcBounds @ 0x6C40B0 (libkrkr2.so):
            //   v30 = 1 << nodeType
            //   v31 = completionType ? 0x1449 : 0x1441
            //   if ((v31 & v30) == 0 || !*(BYTE*)(node+200)) skip
            // The actual native gate is the Path A nodeType mask PLUS
            // node.source.valid (node+0xC8) — NOT drawFlag (Path B) and
            // NOT drawnThisFrame (node+1944, set by sub_6C2334 but not
            // read here). Port's previous drawFlag/drawnThisFrame reads
            // were both proxies; this is the authoritative gate.
            const int visBitmaskCalc =
                _preview ? 0x1449 : 0x1441;  // binary calcBounds gates on +1092 (preview)
            if(((1 << node.nodeType) & visBitmaskCalc) == 0 ||
               !node.source.valid) {
                continue;
            }

            bool haveNodeBounds = false;
            double minX = 0.0;
            double minY = 0.0;
            double maxX = 0.0;
            double maxY = 0.0;
            auto extendPoint = [&](double x, double y) {
                if(!haveNodeBounds) {
                    minX = maxX = x;
                    minY = maxY = y;
                    haveNodeBounds = true;
                    return;
                }
                if(x < minX) minX = x;
                if(y < minY) minY = y;
                if(x > maxX) maxX = x;
                if(y > maxY) maxY = y;
            };

            node.bounds[0] = std::numeric_limits<float>::max();
            node.bounds[1] = std::numeric_limits<float>::max();
            node.bounds[2] = -std::numeric_limits<float>::max();
            node.bounds[3] = -std::numeric_limits<float>::max();

            // 0x6C40BC..0x6C43A4: +2048 has priority; otherwise scan the
            // exactly-16-point +2072 patch; only two empty derived vectors
            // fall back to the four node+1856 corners.
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

            if(!haveNodeBounds) {
                continue;
            }

            const std::array<float, 4> expectedBounds = {
                static_cast<float>(std::floor(minX)),
                static_cast<float>(std::floor(minY)),
                static_cast<float>(std::ceil(maxX)),
                static_cast<float>(std::ceil(maxY))
            };
            node.bounds[0] = expectedBounds[0];
            node.bounds[1] = expectedBounds[1];
            node.bounds[2] = expectedBounds[2];
            node.bounds[3] = expectedBounds[3];
            mergeBounds(node.bounds[0], node.bounds[1], node.bounds[2],
                        node.bounds[3]);
            if(detail::logoChainTraceEnabledForPath(motionPath)) {
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
                    motionPath, "calcBounds.node", "0x6C3D04",
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

        detail::logoChainTraceLogf(
            motionPath, "calcBounds.player", "0x6C3D04", _clampedEvalTime,
            "playerBounds=({:.3f},{:.3f},{:.3f},{:.3f}) haveBounds={}",
            _boundsMinX, _boundsMinY, _boundsMaxX, _boundsMaxY,
            haveBounds ? 1 : 0);
    }

    void Player::appendPreparedRenderItems(
        std::vector<detail::PreparedRenderItem *> &mainList,
        std::vector<detail::PreparedRenderItem *> &auxList,
        std::uint32_t inheritedColor,
        bool inheritedDrawFlag19,
        bool inheritedFlag18) {
        // sub_6D5164 @ 0x6D5178: the first instruction of the libkrkr2.so
        // build+sort wrapper is `if (!*(DWORD*)(player+544)) return 0;`.
        // _motionContentVariant.Type() is the source-level +544 type-tag gate.
        if(!hasMotionContent()) {
            return;
        }

        auto &entries = mainList;
        const auto &nodes = _nodes;
        const auto motionPath = matchedMotionPath();
        // sub_6C2334@0x6C31C8/0x6C337C/0x6C38A0 reads Player+1092,
        // the script-visible preview property, for these node-type masks.
        const int bitmask = _preview ? 5193 : 5185;
        const auto &dam = _drawAffineMatrix;
        const bool drawAffineMatrixNonIdentity =
            _drawAffineMatrixNonIdentity;
        const std::uint32_t effectiveColor =
            multiplyPackedColorWeightsLike_0x6C2334(
                inheritedColor, _colorWeightPacked);

        auto appendChildEntriesAtCurrentNode =
            [&](const detail::MotionNode &ownerNode, Player *child,
                std::vector<detail::PreparedRenderItem *> &childMainList,
                bool childDrawFlag19) {
            if(!child) {
                return;
            }
            const size_t mainCountBefore = childMainList.size();
            const size_t auxCountBefore = auxList.size();
            // aligned with sub_6C2334 @0x6C2334 (recursion @0x6c2b5c/0x6c3124/
            // 0x6c36ac): child a6 = (a6 & 1) || (node+48 != 0), i.e. the
            // parent's inherited flag OR'd with THIS node's priorDraw bool
            // (node+48 = sub_6636D4("priorDraw") != 0, written at 0x6bc6c4).
            // Must use the node-level priorDraw, not Player::_priorDraw.
            // The recursive call receives the caller's SAME main/aux vectors;
            // there is no child-local prepare/sort stage in sub_6C2334.
            child->appendPreparedRenderItems(
                childMainList, auxList,
                (ownerNode.inheritFlags & 0x200) != 0
                    ? effectiveColor
                    : 0xFF808080u,
                childDrawFlag19,
                inheritedFlag18 || ownerNode.priorDraw != 0);
            const auto childMotionPath = child->matchedMotionPath();
            if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
               motionPath.find("m2logo.mtn") != std::string::npos &&
               _clampedEvalTime >= 30.0 && _clampedEvalTime <= 50.0) {
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
            detail::logoChainTraceLogf(
                motionPath, "prepare.childMerge", "0x6C2334/0x6D4F00",
                _clampedEvalTime,
                "childMotionPath={} mainAdded={} auxAdded={} parentMainTotal={}",
                childMotionPath.empty()
                    ? std::string("<none>") : childMotionPath,
                childMainList.size() - mainCountBefore,
                auxList.size() - auxCountBefore, childMainList.size());
        };

        auto transformPoint = [&](float x, float y) -> tTVPPointD {
            return { dam[0] * static_cast<double>(x) +
                         dam[2] * static_cast<double>(y) + dam[4],
                     dam[1] * static_cast<double>(x) +
                         dam[3] * static_cast<double>(y) + dam[5] };
        };

        auto transformRectLike_0x6C2334 =
            [&](const std::array<float, 4> &rect) {
                if(!drawAffineMatrixNonIdentity) {
                    return rect;
                }
                const auto p0 = transformPoint(rect[0], rect[1]);
                const auto p1 = transformPoint(rect[2], rect[1]);
                const auto p2 = transformPoint(rect[2], rect[3]);
                const auto p3 = transformPoint(rect[0], rect[3]);
                return std::array<float, 4>{
                    static_cast<float>(std::floor(std::min(
                        std::min(p0.x, p1.x), std::min(p2.x, p3.x)))),
                    static_cast<float>(std::floor(std::min(
                        std::min(p0.y, p1.y), std::min(p2.y, p3.y)))),
                    static_cast<float>(std::ceil(std::max(
                        std::max(p0.x, p1.x), std::max(p2.x, p3.x)))),
                    static_cast<float>(std::ceil(std::max(
                        std::max(p0.y, p1.y), std::max(p2.y, p3.y))))
                };
            };

        constexpr std::array<float, 4> kInvalidPreparedPaintBox = {
            1.0f, 1.0f, -1.0f, -1.0f
        };

        // sub_6C2334 @0x6C313C..0x6C31C4 does not walk deque order. For each
        // logical non-root slot it reads Player+616 content in REVERSE order,
        // adds one to the stored zero-based layer index, and selects that node.
        // drawnThisFrame is cleared only after selection, preserving the
        // binary's duplicate/omitted-index boundary behavior.
        for(size_t logicalIndex = 1;
            logicalIndex < nodes.size(); ++logicalIndex) {
            const auto priorityPosition = static_cast<tjs_int>(
                nodes.size() - logicalIndex - 1);
            const auto i = static_cast<size_t>(
                detail::motionPropGetIntByNum(
                    _rootContentVariant, priorityPosition) + 1);
            auto &node = _nodes[i];
            node.drawnThisFrame = false;
            if(!_preview) {
                // 0x6C31DC: particle recursion precedes the node+1505 active
                // gate and writes directly into the caller's lists.
                if(node.nodeType == 4) {
                    const int particleCount = node.getParticleCount();
                    for(int pi = 0; pi < particleCount; ++pi) {
                        appendChildEntriesAtCurrentNode(
                            node, node.getParticleChild(pi),
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
                    // sub_6C2334 @0x6C272C..0x6C3124: a plain type-3 node
                    // contributes its child's items directly to the caller's
                    // main/aux lists and never creates a wrapper item.
                    appendChildEntriesAtCurrentNode(
                        node, child, mainList, inheritedDrawFlag19);
                    continue;
                }

                // sub_6C2334 @0x6C273C..0x6C2B74: drawFlag/maskRef selects
                // the persistent node-owned type-3 wrapper path. The wrapper
                // itself is never inserted into mainList; its child vector is
                // populated first and then range-inserted into caller main.
                node.drawnThisFrame = true;
                if(!node.preparedRenderItem) {
                    node.preparedRenderItem =
                        new detail::PreparedRenderItem();
                }
                auto &wrapper = *node.preparedRenderItem;
                // sub_6C2334@0x6C27B4 copies the node label on every wrapper
                // population, not only when the persistent item is allocated.
                wrapper.ownerLabel = node.layerName;
                wrapper.nodeIndex = static_cast<int>(i);
                wrapper.nativeNode = &node;
                wrapper.hasOwnSource = false;
                wrapper.drawFlag = false;
                wrapper.stencilComposite = node.stencilType;
                wrapper.paintBox = transformRectLike_0x6C2334({
                    node.bounds[0], node.bounds[1],
                    node.bounds[2], node.bounds[3]
                });
                wrapper.viewport = kInvalidPreparedPaintBox;
                wrapper.hasViewport = false;
                if(node.clipAABB &&
                   node.clipAABB[2] >= node.clipAABB[0] &&
                   node.clipAABB[3] >= node.clipAABB[1]) {
                    wrapper.viewport = transformRectLike_0x6C2334({
                        node.clipAABB[0], node.clipAABB[1],
                        node.clipAABB[2], node.clipAABB[3]
                    });
                    wrapper.hasViewport = true;
                }

                wrapper.parentItem = nullptr;
                if(node.drawFlag) {
                    auxList.push_back(&wrapper);
                    if(node.visibleAncestorIndex >= 0 &&
                       node.visibleAncestorIndex <
                           static_cast<int>(_nodes.size()) &&
                       node.visibleAncestorIndex != node.index) {
                        auto &ancestor = _nodes[static_cast<size_t>(
                            node.visibleAncestorIndex)];
                        if(!ancestor.preparedRenderItem) {
                            ancestor.preparedRenderItem =
                                new detail::PreparedRenderItem();
                        }
                        wrapper.parentItem =
                            ancestor.preparedRenderItem;
                    }
                }

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

            // sub_6C2334 @0x6C32D0: node+1904 owns one persistent raw item;
            // allocate only once, then overwrite the fields reached by this
            // population path without reconstructing the object.
            if(!node.preparedRenderItem) {
                node.preparedRenderItem = new detail::PreparedRenderItem();
            }
            auto &entry = *node.preparedRenderItem;
            // sub_6C2334@0x6C3348 refreshes the independent owner string on
            // every ordinary-item population.
            entry.ownerLabel = node.layerName;
            entry.nodeIndex = static_cast<int>(i);
            entry.nativeNode = &node;
            // 0x6C25DC..0x6C2654: item+264 points directly at the visible
            // ancestor's node-owned item. The ancestor item is allocated on
            // demand but is not thereby inserted into either output list.
            if(node.visibleAncestorIndex >= 0 &&
               node.visibleAncestorIndex < static_cast<int>(_nodes.size()) &&
               node.visibleAncestorIndex != node.index) {
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
            entry.hasOwnSource = hasOwnSource;
            if(hasOwnSource) {
                entry.sourceKey = node.source.path;
                entry.sourceObject = node.source.object;
                entry.sourceTexture = node.source.texture;
                entry.sourceRect = node.source.textureRect;
                // 0x6C35A8..0x6C35F8 copies the active clip-slot ttstr into
                // item+8 independently of the Web source object.
                entry.commandSrc = node.activeSlot().srcValue;
            }
            // Aligned to sub_6D5164 -> sub_6C2334:
            // item+19 = node+1960 ? 1 : (arg5 | node+1961).
            entry.drawFlag =
                node.drawFlag || node.stencilCompositeMaskReferenced ||
                inheritedDrawFlag19;
            entry.rawFlag16 = node.source.blank;
            entry.skipFlag0 =
                (((_preview ? 1097 : 1089) & (1 << node.nodeType)) == 0);
            entry.skipFlag1 = inheritedFlag18 || (node.priorDraw != 0);
            // 0x6C33CC first coerces player+1012 Variant to ttstr, then copies
            // that independently owning string into item+248.
            entry.commandKey = ttstr(_findMotionContextVariant);
            entry.layerId1 = node.layerId1;
            entry.layerId2 = node.layerId2;
            entry.viewport = kInvalidPreparedPaintBox;
            entry.hasViewport = false;

            // Aligned to libkrkr2.so sub_6C2334 (0x6C2334): render item +0x40
            // stores node accumulated posZ, while coordinateMode and
            // objTriPriority occupy independent integer fields.
            entry.sortKey = node.accumulated.posZ;
            entry.commandCoord = {
                node.accumulated.posX,
                _zFactor * node.accumulated.posZ + node.accumulated.posY,
                node.accumulated.posZ,
            };
            entry.commandMatrix = {
                node.accumulated.m11,
                node.accumulated.m12,
                node.accumulated.m21,
                node.accumulated.m22,
            };
            entry.blendMode = node.accumulated.blendMode;
            entry.packedColors = copyPackedColorsFromBytes(node.colorBytes);
            for(auto &packedColor : entry.packedColors) {
                packedColor = multiplyPackedColorWeightsLike_0x6C2334(
                    packedColor, effectiveColor);
            }
            entry.opacity = node.accumulated.opacity;
            entry.stencilComposite = node.stencilType;
            entry.coordinateMode = node.coordinateMode;
            entry.objTriPriority = node.objTriPriority;
            entry.originX = node.source.originX + node.activeSlot().ox;
            entry.originY = node.source.originY + node.activeSlot().oy;
            entry.visibleAncestorIndex = node.visibleAncestorIndex;
            entry.meshType = node.meshType;
            entry.meshDivX = node.meshDivX;
            entry.meshDivY = node.meshDivY;
            // sub_6C2334 @0x6C35AC..0x6C35C4 copies all four node+1856
            // corners unconditionally.  The caller-supplied affine, when
            // active, is applied afterwards at 0x6C2BB0..0x6C2CD0.
            for(int ci = 0; ci < 4; ++ci) {
                const auto pt = drawAffineMatrixNonIdentity
                    ? transformPoint(node.vertices[ci * 2],
                                     node.vertices[ci * 2 + 1])
                    : tTVPPointD{node.vertices[ci * 2],
                                 node.vertices[ci * 2 + 1]};
                entry.corners[ci * 2] = static_cast<float>(pt.x);
                entry.corners[ci * 2 + 1] = static_cast<float>(pt.y);
            }

            // item+184..196 is copied from node+1888, not recomputed from the
            // item vectors.  With an outer affine, 0x6C2960..0x6C2A84 turns
            // the four transformed rect corners into a floor/ceil AABB.
            entry.paintBox = transformRectLike_0x6C2334({
                node.bounds[0], node.bounds[1],
                node.bounds[2], node.bounds[3]
            });

            // Three independent vector owners, exactly as selected at
            // 0x6C2684..0x6C2714:
            //   item+344 <- node+2048 (always assigned, hence also cleared)
            //   item+400 <- node+2072 (type-1/raw-present branch only)
            //   item+376 <- node+2024 (same branch, deliberately NOT affine)
            entry.commandCompositeMeshPoints = node.compositeMeshPoints;
            if(drawAffineMatrixNonIdentity) {
                for(auto &point : entry.commandCompositeMeshPoints) {
                    const auto transformed = transformPoint(point.x, point.y);
                    point.x = static_cast<float>(transformed.x);
                    point.y = static_cast<float>(transformed.y);
                }
            }
            if(!entry.commandCompositeMeshPoints.empty()) {
                entry.meshType = 2;
            } else if(node.meshType == 1) {
                if(node.meshControlPoints.empty()) {
                    entry.meshType = 0;
                } else {
                    entry.commandPatchDivision = static_cast<int>(
                        getMeshDivisionRatio() *
                        static_cast<double>(node.meshDivision));
                    if(entry.commandPatchDivision >= 50) {
                        entry.commandPatchDivision = 50;
                    }
                    entry.meshPoints =
                        node.transformedMeshControlPoints;
                    if(drawAffineMatrixNonIdentity) {
                        for(auto &point : entry.meshPoints) {
                            const auto transformed =
                                transformPoint(point.x, point.y);
                            point.x = static_cast<float>(transformed.x);
                            point.y = static_cast<float>(transformed.y);
                        }
                    }
                    entry.commandBezierPatchPoints =
                        node.meshControlPoints;
                }
            }

            if(node.clipAABB) {
                const float *clipAABB = node.clipAABB;
                if(clipAABB[2] >= clipAABB[0] &&
                   clipAABB[3] >= clipAABB[1]) {
                    if(!drawAffineMatrixNonIdentity) {
                        // sub_6C2334 @ 0x6C27E8 copies node+1936 to item+200
                        // verbatim when Player+611 is zero. In particular, it
                        // does not round a unit-matrix viewport.
                        entry.viewport = {clipAABB[0], clipAABB[1],
                                          clipAABB[2], clipAABB[3]};
                        entry.hasViewport = true;
                    } else {
                        const auto p0 =
                            transformPoint(clipAABB[0], clipAABB[1]);
                        const auto p1 =
                            transformPoint(clipAABB[2], clipAABB[1]);
                        const auto p2 =
                            transformPoint(clipAABB[2], clipAABB[3]);
                        const auto p3 =
                            transformPoint(clipAABB[0], clipAABB[3]);
                    // aligned with sub_6C2334 @0x6c2800-0x6c2954: the
                    // transformed clip bbox is rounded floor(left)/floor(top)/
                    // ceil(right)/ceil(bottom) before being stored into the
                    // render item viewport (item+200..212). The oracle gates
                    // this on item+208>=item+200 && item+212>=item+204 (the
                    // shapeAABB validity check above) and writes:
                    //   *(float*)(item+200) = floorf(minX);
                    //   *(float*)(item+204) = floorf(minY);
                    //   *(float*)(item+208) = ceilf(maxX);
                    //   *(float*)(item+212) = ceilf(maxY);
                    // The previous port stored the raw min/max bbox without the
                    // floor/ceil, leaving fractional viewport values that
                    // diverged from the oracle (e.g. m2logo items[9]:
                    // [612.568,557.332,1293.251,632.964] vs [612,557,1294,633]).
                        entry.viewport = {
                            static_cast<float>(std::floor(std::min(
                                std::min(p0.x, p1.x), std::min(p2.x, p3.x)))),
                            static_cast<float>(std::floor(std::min(
                                std::min(p0.y, p1.y), std::min(p2.y, p3.y)))),
                            static_cast<float>(std::ceil(std::max(
                                std::max(p0.x, p1.x), std::max(p2.x, p3.x)))),
                            static_cast<float>(std::ceil(std::max(
                                std::max(p0.y, p1.y), std::max(p2.y, p3.y))))
                        };
                        entry.hasViewport = true;
                    }
                }
            }

            if(detail::logoChainTraceEnabledForPath(motionPath)) {
                const std::array<float, 8> expectedCorners = {
                    static_cast<float>(dam[0] *
                                           static_cast<double>(node.vertices[0]) +
                                       dam[2] *
                                           static_cast<double>(node.vertices[1]) +
                                       dam[4]),
                    static_cast<float>(dam[1] *
                                           static_cast<double>(node.vertices[0]) +
                                       dam[3] *
                                           static_cast<double>(node.vertices[1]) +
                                       dam[5]),
                    static_cast<float>(dam[0] *
                                           static_cast<double>(node.vertices[2]) +
                                       dam[2] *
                                           static_cast<double>(node.vertices[3]) +
                                       dam[4]),
                    static_cast<float>(dam[1] *
                                           static_cast<double>(node.vertices[2]) +
                                       dam[3] *
                                           static_cast<double>(node.vertices[3]) +
                                       dam[5]),
                    static_cast<float>(dam[0] *
                                           static_cast<double>(node.vertices[4]) +
                                       dam[2] *
                                           static_cast<double>(node.vertices[5]) +
                                       dam[4]),
                    static_cast<float>(dam[1] *
                                           static_cast<double>(node.vertices[4]) +
                                       dam[3] *
                                           static_cast<double>(node.vertices[5]) +
                                       dam[5]),
                    static_cast<float>(dam[0] *
                                           static_cast<double>(node.vertices[6]) +
                                       dam[2] *
                                           static_cast<double>(node.vertices[7]) +
                                       dam[4]),
                    static_cast<float>(dam[1] *
                                           static_cast<double>(node.vertices[6]) +
                                       dam[3] *
                                           static_cast<double>(node.vertices[7]) +
                                       dam[5])
                };
                const auto effectiveColor = unpackPackedRgba(entry.packedColors[0]);
                detail::logoChainTraceLogf(
                    motionPath, "prepare.item", "0x6C2334", _clampedEvalTime,
                    "nodeIndex={} src={} blend={} opacity={} packedColor=[0x{:08x},0x{:08x},0x{:08x},0x{:08x}] effectiveColor=[{},{},{},{}] meshType={} meshDiv=({},{}) sortKey={:.3f} coordinateMode={} objTriPriority={} layerId=({}, {}) nodeDrawFlag={} maskRef={} itemDrawFlag={} visibleAncestorIndex={} slotDone={} frameType={} stencilBase={} stencilType={}",
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
                    node.activeSlot().done ? 1 : 0, node.currentFrameType,
                    node.stencilTypeBase, node.stencilType);
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
                    motionPath, "prepare.corners", "0x6C2334",
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
                    motionPath, "prepare.paintBox", "0x6C2334",
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
                    motionPath, "prepare.viewport", "0x6C2334",
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

            if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
               motionPath.find("m2logo.mtn") != std::string::npos &&
               _clampedEvalTime >= 43.0 && _clampedEvalTime <= 50.0 &&
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

            // Aligned to sub_6C2334 mainList enqueue: this node is now in
            // the Path A render list; mark it so downstream consumers
            // (e.g. calcBounds) can distinguish Path A presence from the
            // Path B drawFlag.
            _nodes[i].drawnThisFrame = true;

            entries.push_back(&entry);
        }

        // 0x6C36CC..0x6C39B0 is a distinct node-order post-pass. A qualifying
        // type-12 item has already entered mainList; the SAME borrowed pointer
        // is additionally appended to auxList. Its child vector is rebuilt
        // solely from node+2600 stencilCompositeMaskNodes in raw stored order.
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
                if(!maskNode || !maskNode->drawnThisFrame) {
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
        const auto motionPath = matchedMotionPath();

#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderBuildItemsEnter(this);
#endif
        // sub_6D5164 @0x6D5184..0x6D5198 passes neutral color and two false
        // lineage flags into sub_6C2334. Callers construct both vectors empty.
        appendPreparedRenderItems(
            mainList,
            auxList,
            0xFF808080u, false, false);
        std::vector<double> beforeSortKeys;
        beforeSortKeys.reserve(mainList.size());
        for(const auto *item : mainList) {
            beforeSortKeys.push_back(item ? item->sortKey : 0.0);
        }
        // Aligned to sub_6D4F00 (0x6D4F00): compare render-item sort key.
        std::stable_sort(
            mainList.begin(),
            mainList.end(),
            [](const detail::PreparedRenderItem *lhs,
               const detail::PreparedRenderItem *rhs) {
                return lhs && rhs ? lhs->sortKey < rhs->sortKey
                                  : rhs != nullptr;
            });
        if(detail::logoChainTraceEnabledForPath(motionPath)) {
            std::ostringstream beforeSort;
            std::ostringstream afterSort;
            for(size_t i = 0; i < beforeSortKeys.size(); ++i) {
                if(i) beforeSort << ",";
                beforeSort << beforeSortKeys[i];
            }
            for(size_t i = 0; i < mainList.size(); ++i) {
                if(i) afterSort << ",";
                afterSort << (mainList[i] ? mainList[i]->sortKey : 0.0);
            }
            detail::logoChainTraceLogf(
                motionPath, "prepare.sort", "0x6D5164/0x6D4F00",
                _clampedEvalTime,
                "itemCount={} sortKeysBefore=[{}] sortKeysAfter=[{}]",
                mainList.size(), beforeSort.str(),
                afterSort.str());
        }

#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderBuildItemsLeave(this, mainList, auxList);
#endif

        if(detail::logoSnapshotMarkEnabledForPath(motionPath) &&
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
        // sub_6D5164 returns 1 whenever the motion-content type tag is nonzero,
        // even when mainList is empty.
        const bool ok = hasMotionContent();
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderPrepareLeave(this, ok, mainList, auxList);
#endif
        return ok;
    }

    void Player::applyPreparedRenderItemTranslateOffsets(
        detail::PreparedRenderItemList &mainList) {
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderApplyTranslateEnter(this);
#endif
        // Aligned to libkrkr2.so Player_applyTranslateOffset (0x6D5264):
        // normal path adds cameraOffset to prepared render items here.
        // Root position is already baked into node state during updateLayers.
        const double ofsX = static_cast<double>(_cameraOffsetX);
        const double ofsY = static_cast<double>(_cameraOffsetY);
        const auto motionPath = matchedMotionPath();
        // Player_applyTranslateOffset @0x6D5264 receives the main pointer
        // vector only; auxiliary composite items are not walked here.
        for(auto *entryPtr : mainList) {
            if(!entryPtr) {
                continue;
            }
            auto &entry = *entryPtr;
            const auto beforeCorners = entry.corners;
            const auto beforePaintBox = entry.paintBox;
            const auto beforeViewport = entry.viewport;
            const auto beforeCompositeMeshPoints =
                entry.commandCompositeMeshPoints;
            const auto beforeMeshPoints = entry.meshPoints;
            for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                entry.corners[ci] = static_cast<float>(
                    static_cast<double>(entry.corners[ci]) + ofsX);
                entry.corners[ci + 1] = static_cast<float>(
                    static_cast<double>(entry.corners[ci + 1]) + ofsY);
            }
            entry.paintBox[0] = static_cast<float>(
                static_cast<double>(entry.paintBox[0]) + ofsX);
            entry.paintBox[1] = static_cast<float>(
                static_cast<double>(entry.paintBox[1]) + ofsY);
            entry.paintBox[2] = static_cast<float>(
                static_cast<double>(entry.paintBox[2]) + ofsX);
            entry.paintBox[3] = static_cast<float>(
                static_cast<double>(entry.paintBox[3]) + ofsY);
            if(entry.hasViewport) {
                entry.viewport[0] = static_cast<float>(
                    static_cast<double>(entry.viewport[0]) + ofsX);
                entry.viewport[1] = static_cast<float>(
                    static_cast<double>(entry.viewport[1]) + ofsY);
                entry.viewport[2] = static_cast<float>(
                    static_cast<double>(entry.viewport[2]) + ofsX);
                entry.viewport[3] = static_cast<float>(
                    static_cast<double>(entry.viewport[3]) + ofsY);
            }
            // Player_applyTranslateOffset@0x6D52B8..0x6D533C walks item+344
            // for every item.  The item+400 vector is walked separately only
            // for meshType 1 at 0x6D5348..0x6D5388; raw item+376 is untouched.
            for(auto &point : entry.commandCompositeMeshPoints) {
                point.x = static_cast<float>(
                    static_cast<double>(point.x) + ofsX);
                point.y = static_cast<float>(
                    static_cast<double>(point.y) + ofsY);
            }
            if(entry.meshType == 1) {
                for(auto &point : entry.meshPoints) {
                    point.x = static_cast<float>(
                        static_cast<double>(point.x) + ofsX);
                    point.y = static_cast<float>(
                        static_cast<double>(point.y) + ofsY);
                }
            }
            if(detail::logoChainTraceEnabledForPath(motionPath)) {
                bool ok = true;
                for(size_t ci = 0; ci < entry.corners.size(); ci += 2) {
                    if(std::fabs((entry.corners[ci] - beforeCorners[ci]) -
                                 static_cast<float>(ofsX)) > 0.01f ||
                       std::fabs((entry.corners[ci + 1] -
                                  beforeCorners[ci + 1]) -
                                 static_cast<float>(ofsY)) > 0.01f) {
                        ok = false;
                        break;
                    }
                }
                if(ok && entry.hasViewport) {
                    for(size_t vi = 0; vi < entry.viewport.size(); vi += 2) {
                        if(std::fabs((entry.viewport[vi] - beforeViewport[vi]) -
                                     static_cast<float>(ofsX)) > 0.01f ||
                           std::fabs((entry.viewport[vi + 1] -
                                      beforeViewport[vi + 1]) -
                                     static_cast<float>(ofsY)) > 0.01f) {
                            ok = false;
                            break;
                        }
                    }
                }
                if(ok) {
                    for(size_t pi = 0;
                        pi < entry.commandCompositeMeshPoints.size(); ++pi) {
                        if(std::fabs(
                               (entry.commandCompositeMeshPoints[pi].x -
                                beforeCompositeMeshPoints[pi].x) -
                               static_cast<float>(ofsX)) > 0.01f ||
                           std::fabs(
                               (entry.commandCompositeMeshPoints[pi].y -
                                beforeCompositeMeshPoints[pi].y) -
                               static_cast<float>(ofsY)) > 0.01f) {
                            ok = false;
                            break;
                        }
                    }
                }
                if(ok && entry.meshType == 1) {
                    for(size_t pi = 0; pi < entry.meshPoints.size(); ++pi) {
                        if(std::fabs(
                               (entry.meshPoints[pi].x - beforeMeshPoints[pi].x) -
                               static_cast<float>(ofsX)) > 0.01f ||
                           std::fabs((entry.meshPoints[pi].y -
                                      beforeMeshPoints[pi].y) -
                                     static_cast<float>(ofsY)) > 0.01f) {
                            ok = false;
                            break;
                        }
                    }
                }
                detail::logoChainTraceCheck(
                    motionPath, "prepare.translate", "0x6D5264",
                    _clampedEvalTime,
                    fmt::format(
                        "cameraOffset=({:.3f},{:.3f}) applied to corners/paintBox/viewport/+344/type1+400",
                        ofsX, ofsY),
                    fmt::format(
                        "nodeIndex={} beforeCorner0=({:.3f},{:.3f}) afterCorner0=({:.3f},{:.3f}) beforePaintBox=[{:.3f},{:.3f},{:.3f},{:.3f}] afterPaintBox=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                        entry.nodeIndex, beforeCorners[0], beforeCorners[1],
                        entry.corners[0], entry.corners[1], beforePaintBox[0],
                        beforePaintBox[1], beforePaintBox[2], beforePaintBox[3],
                        entry.paintBox[0], entry.paintBox[1], entry.paintBox[2],
                        entry.paintBox[3]),
                    ok,
                    "Player_applyTranslateOffset added more than cameraOffset");
            }
        }
#if defined(KRKR2_WASMTIME_HEADLESS)
        detail::motionTraceRenderApplyTranslateLeave(this, mainList);
#endif
    }

} // namespace motion
