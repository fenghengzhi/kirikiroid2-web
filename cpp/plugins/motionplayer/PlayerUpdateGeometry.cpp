// PlayerUpdateGeometry.cpp — updateLayers geometry, visibility, camera, and shape phases
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"
#include "MotionDispatch.h"

namespace motion {
    void Player::updateLayersPhase3_CameraConstraint() {
        auto &nodes = _nodes;
        if(_preview || nodes.size() < 2) {
            return;
        }

        bool hasMinimumX = false;
        bool hasDirectX = false;
        bool hasMaximumX = false;
        bool hasMinimumY = false;
        bool hasDirectY = false;
        bool hasMaximumY = false;
        bool hasMinimumZ = false;
        bool hasDirectZ = false;
        bool hasMaximumZ = false;
        double minimumX = kCameraConstraintExtent_guess;
        double directX = 0.0;
        double maximumX = -kCameraConstraintExtent_guess;
        double minimumY = kCameraConstraintExtent_guess;
        double directY = 0.0;
        double maximumY = -kCameraConstraintExtent_guess;
        double minimumZ = kCameraConstraintExtent_guess;
        double directZ = 0.0;
        double maximumZ = -kCameraConstraintExtent_guess;

        for(size_t index = 1; index < nodes.size(); ++index) {
            auto &constraint = nodes[index];
            if(constraint.nodeType != 9 || constraint.activeSlot().done) {
                continue;
            }

            const auto *target = findNodeByRawLabel_guess(
                constraint.activeSlot().anchorTarget, false);
            if(target == nullptr) {
                target = &nodes[0];
            }
            const int constraintType = remapCameraConstraintType_guess(
                constraint.anchorType_guess,
                constraint.accumulated.flipX,
                constraint.accumulated.flipY);

            switch(constraintType) {
            case 0:
                if(target->accumulated.posX < constraint.accumulated.posX) {
                    const double offset = target->accumulated.posX
                        - constraint.accumulated.posX;
                    if(offset <= minimumX) {
                        minimumX = offset;
                    }
                    hasMinimumX = true;
                }
                break;
            case 1:
                directX = target->accumulated.posX
                    - constraint.accumulated.posX;
                hasDirectX = true;
                break;
            case 2:
                if(target->accumulated.posX > constraint.accumulated.posX) {
                    const double offset = target->accumulated.posX
                        - constraint.accumulated.posX;
                    if(offset >= maximumX) {
                        maximumX = offset;
                    }
                    hasMaximumX = true;
                }
                break;
            case 3:
                if(target->accumulated.posY < constraint.accumulated.posY) {
                    const double offset = target->accumulated.posY
                        - constraint.accumulated.posY;
                    if(offset <= minimumY) {
                        minimumY = offset;
                    }
                    hasMinimumY = true;
                }
                break;
            case 4:
                directY = target->accumulated.posY
                    - constraint.accumulated.posY;
                hasDirectY = true;
                break;
            case 5:
                if(target->accumulated.posY > constraint.accumulated.posY) {
                    const double offset = target->accumulated.posY
                        - constraint.accumulated.posY;
                    if(offset >= maximumY) {
                        maximumY = offset;
                    }
                    hasMaximumY = true;
                }
                break;
            case 6:
                if(target->accumulated.posZ < constraint.accumulated.posZ) {
                    const double offset = target->accumulated.posZ
                        - constraint.accumulated.posZ;
                    if(offset <= minimumZ) {
                        minimumZ = offset;
                    }
                    hasMinimumZ = true;
                }
                break;
            case 7:
                directZ = target->accumulated.posZ
                    - constraint.accumulated.posZ;
                hasDirectZ = true;
                break;
            case 8:
                if(target->accumulated.posZ > constraint.accumulated.posZ) {
                    const double offset = target->accumulated.posZ
                        - constraint.accumulated.posZ;
                    if(offset >= maximumZ) {
                        maximumZ = offset;
                    }
                    hasMaximumZ = true;
                }
                break;
            default:
                break;
            }
        }

        const double offsetX = selectCameraConstraintOffset_guess(
            hasMinimumX, minimumX,
            hasDirectX, directX,
            hasMaximumX, maximumX);
        const double offsetY = selectCameraConstraintOffset_guess(
            hasMinimumY, minimumY,
            hasDirectY, directY,
            hasMaximumY, maximumY);
        const double offsetZ = selectCameraConstraintOffset_guess(
            hasMinimumZ, minimumZ,
            hasDirectZ, directZ,
            hasMaximumZ, maximumZ);

        if(offsetX != 0.0 || offsetY != 0.0 || offsetZ != 0.0) {
            _cameraConstraintDirty_guess = true;
            for(size_t index = 1; index < nodes.size(); ++index) {
                nodes[index].accumulated.posX += offsetX;
                nodes[index].accumulated.posY += offsetY;
                nodes[index].accumulated.posZ += offsetZ;
            }
        }
    }

    void Player::updateLayersPhase3_VertexComputation() {
        auto &nodes = _nodes;
        // Four-reference vertex computation. Current per-target entry points,
        // layouts and count sites live in analysis/ rather than source comments.
        std::vector<detail::MeshPoint> combinedPatch;
        for (size_t vi = 1; vi < nodes.size(); ++vi) {
            auto &vn = nodes[vi];
            const int parentIdx = vn.parentIndex;
            auto &parentNode = nodes[static_cast<size_t>(parentIdx)];

            // The property callback runs before the parent mesh-state bytes are
            // consumed. It receives an independently retained Variant copy.
            if (vn.hasEmoteEdit_guess()) {
                const tTJSVariant emoteEdit = vn.emoteEditVariant;
                vn.priorDraw = detail::motionPropGetBool(
                    emoteEdit, TJS_W("priorDraw"), 0,
                    &detail::emoteEditPriorDrawMemberHint_guess);
            } else {
                vn.priorDraw = false;
            }

            vn.meshAncestor =
                (parentNode.hasMeshData
                 || parentNode.meshInheritanceSeparator_guess)
                    ? &parentNode
                    : parentNode.meshAncestor;
            vn.meshVertexPassDirty_guess = vn.accumulated.dirty
                || (vn.meshAncestor != nullptr
                    && vn.meshAncestor->meshVertexPassDirty_guess);
            if(!vn.meshVertexPassDirty_guess) {
                // Even a skipped node removes raw-combined parents from the
                // separately linked runtime mesh chain. The per-node mesh
                // flags deliberately retain their preceding values here.
                if(vn.meshCombine && vn.hasMeshData) {
                    detail::MotionNode *combineParent = &parentNode;
                    while(true) {
                        if(combineParent->hasMeshData
                           && combineParent == vn.meshAncestor) {
                            vn.meshAncestor = combineParent->meshAncestor;
                        }
                        if(!combineParent->meshCombine) {
                            break;
                        }
                        combineParent = &nodes[static_cast<size_t>(
                            combineParent->parentIndex)];
                    }
                }
                continue;
            }

            const auto &activeSlot = vn.activeSlot();
            vn.hasMeshData = !activeSlot.done
                && vn.meshType != 0
                && !vn.meshControlPoints.empty()
                && vn.source.valid
                && (vn.meshFlags & 8) != 0;
            vn.meshInheritanceSeparator_guess = vn.meshAncestor != nullptr
                && (vn.inheritFlags & 0x02000000) == 0;

            // nodeType 1/5 special position via the parent mesh chain.
            // if ((1 << nodeType) & 0x22) != 0 → nodeType 1 (shape) or 5 (camera)
            if (((1 << vn.nodeType) & 0x22) != 0) {
                double px = vn.accumulated.posX;
                double py = vn.accumulated.posY;
                // Walk the ancestor chain and evaluate through each live mesh.
                detail::MotionNode *clipWalk = vn.meshAncestor;
                while (clipWalk) {
                    auto &cn = *clipWalk;
                    if (cn.hasMeshData) {
                        const auto mapped =
                            mapMeshPositionThroughAncestor_guess(px, py, cn);
                        px = mapped.x;
                        py = mapped.y;
                        ++_processedMeshVerticesNum;
                    }
                    clipWalk = cn.meshAncestor;
                }
                vn.vertexPosX = px;
                vn.vertexPosY = py;
                vn.vertexPosZ = vn.accumulated.posZ;
            }

            // A finished active slot skips source-quad and mesh materialization.
            if (!vn.activeSlot().done) {
                // This is the vertex-pass eligibility mask, independent of the
                // earlier draw-item selection mask.
                // Normal: 7233 = 0x1C41, preview: 7241 = 0x1C49
                const int vbm = _preview ? 7241 : 7233;
                const bool vertexEligible = vn.hasEmoteEdit_guess()
                    || ((vbm & (1 << vn.nodeType)) != 0);

                if (vertexEligible && vn.source.valid) {
                    const std::vector<detail::MeshPoint> *effectivePatch =
                        &vn.meshControlPoints;
                    if(vn.meshCombine) {
                        if(vn.hasMeshData) {
                            combinedPatch = vn.meshControlPoints;
                            detail::MotionNode *combineParent = &parentNode;
                            while(true) {
                                if(combineParent->hasMeshData) {
                                    if(combineParent == vn.meshAncestor) {
                                        vn.meshAncestor =
                                            combineParent->meshAncestor;
                                    }
                                    addBezierPatchDelta_guess(
                                        combinedPatch,
                                        combineParent->meshControlPoints);
                                }
                                if(!combineParent->meshCombine) {
                                    break;
                                }
                                combineParent = &nodes[static_cast<size_t>(
                                    combineParent->parentIndex)];
                            }
                        } else {
                            // Native vector::clear shape: retain the backing
                            // allocation for reuse by the next node.
                            combinedPatch.clear();
                        }
                        effectivePatch = &combinedPatch;
                    }

                    const double m11 = vn.matrix.m11, m12 = vn.matrix.m12;
                    const double m21 = vn.matrix.m21, m22 = vn.matrix.m22;
                    const double posX = vn.accumulated.posX;
                    const double posY = vn.accumulated.posY
                        + vn.accumulated.posZ * _zFactor;
                    double meshPositionX = posX;
                    double meshPositionY = posY;

                    // Translate the source-space origin through the accumulated
                    // affine matrix before placing the source quad. Native
                    // reads ox/oy from the selected slot at this use site; it
                    // has no separate per-node clip-origin cache.
                    const double totalOX = vn.source.originX + activeSlot.ox;
                    const double totalOY = vn.source.originY + activeSlot.oy;
                    const double orgX = posX - (m12 * totalOY + totalOX * m11);
                    const double orgY = posY - (totalOY * m22 + totalOX * m21);

                    const double cw = vn.source.width;
                    const double ch = vn.source.height;

                    // Clear both derived vectors while preserving their backing
                    // allocations and leaving the raw patch untouched.
                    vn.transformedMeshControlPoints.clear();
                    vn.compositeMeshPoints.clear();

                    // Materialize the own-affine-transformed 4x4 patch from the
                    // effective raw/combined control-point vector.
                    if(vn.meshType == 1 && !effectivePatch->empty()) {
                        const double mw11 = m11 * cw, mw12 = m12 * ch;
                        const double mw21 = m21 * cw, mw22 = m22 * ch;
                        vn.transformedMeshControlPoints.resize(16);
                        for(size_t pointIndex = 0; pointIndex < 16;
                            ++pointIndex) {
                            const auto &sourcePoint =
                                (*effectivePatch)[pointIndex];
                            vn.transformedMeshControlPoints[pointIndex] = {
                                static_cast<float>(
                                    orgX + mw11 * sourcePoint.x +
                                    mw12 * sourcePoint.y),
                                static_cast<float>(
                                    orgY + mw21 * sourcePoint.x +
                                    mw22 * sourcePoint.y),
                            };
                        }

                        // Only a currently live mesh publishes the inverse map.
                        // The references have no singular-matrix guard.
                        if(vn.hasMeshData) {
                            const double det = mw11 * mw22 - mw12 * mw21;
                            vn.meshInvM11 = mw22 / det;
                            vn.meshInvM12 = -(mw12 / det);
                            vn.meshInvM21 = -(mw21 / det);
                            vn.meshInvM22 = mw11 / det;
                            vn.meshInvOffX = -static_cast<float>(orgX);
                            vn.meshInvOffY = -static_cast<float>(orgY);
                        }
                    }

                    // Raw/combined patch state is updated under the outer
                    // source-valid gate.  Four-corner and inherited-grid
                    // materialization have their own narrower type/blank gate.
                    if(selectVertexQuadMaterialization_guess(
                            vn.hasEmoteEdit_guess(), vn.nodeType,
                            _preview, vn.source.blank)) {
                        // Affine four-corner quad, ordered TL/TR/BR/BL.
                        {
                            const double fx = orgX;
                            const double fy = orgY;
                            vn.vertices[0] = static_cast<float>(fx);
                            vn.vertices[1] = static_cast<float>(fy);
                            vn.vertices[2] = static_cast<float>(fx + m11*cw);
                            vn.vertices[3] = static_cast<float>(fy + m21*cw);
                            vn.vertices[4] = static_cast<float>(fx + m11*cw + m12*ch);
                            vn.vertices[5] = static_cast<float>(fy + m21*cw + m22*ch);
                            vn.vertices[6] = static_cast<float>(fx + m12*ch);
                            vn.vertices[7] = static_cast<float>(fy + m22*ch);
                        }

                        if(vn.meshAncestor != nullptr) {
                            const float *gridCorners = vn.vertices;
                            std::uint32_t division = 0;
                            const std::uint32_t width =
                                meshDoubleToUnsignedTowardZeroSaturated_guess(cw);

                            if(vn.meshType == 1 && !effectivePatch->empty()) {
                                const std::uint32_t height =
                                    meshDoubleToUnsignedTowardZeroSaturated_guess(ch);
                                division = scaledOwnMeshDivision_guess(
                                    getMeshDivisionRatio(),
                                    static_cast<std::uint32_t>(vn.meshDivision));
                                const std::uint32_t denominator = width + height;
                                const std::uint32_t splitX = unsignedDivideA64Profile_guess(
                                    division * width, denominator);
                                vn.meshDivX = meshDivisionCounterWordToInt_guess(
                                    splitX + 1u);
                                vn.meshDivY = meshDivisionCounterWordToInt_guess(
                                    division - splitX + 1u);
                                gridCorners =
                                    unitBezierPatchQuad_guess.data();
                            } else {
                                const std::uint32_t currentExtent =
                                    meshDoubleToUnsignedTowardZeroSaturated_guess(
                                        cw + ch);
                                if(vn.meshType == 1) {
                                    division = scaledOwnMeshDivision_guess(
                                        getMeshDivisionRatio(),
                                        static_cast<std::uint32_t>(vn.meshDivision));
                                } else {
                                    detail::MotionNode *divisionSource =
                                        vn.meshAncestor;
                                    while(!divisionSource->hasMeshData) {
                                        divisionSource =
                                            divisionSource->meshAncestor;
                                    }
                                    const std::uint32_t sourceExtent =
                                        meshDoubleToUnsignedTowardZeroSaturated_guess(
                                            divisionSource->source.width
                                            + divisionSource->source.height);
                                    const std::uint32_t sourceDivision =
                                        scaledInheritedMeshDivision_guess(
                                            getMeshDivisionRatio(),
                                            static_cast<std::uint32_t>(
                                                divisionSource->meshDivision));
                                    division = unsignedDivideA64Profile_guess(
                                        sourceDivision * currentExtent,
                                        sourceExtent);
                                    if(division >= 50u) {
                                        division = 50u;
                                    }
                                }
                                const std::uint32_t splitX = unsignedDivideA64Profile_guess(
                                    division * width, currentExtent);
                                vn.meshDivX = meshDivisionCounterWordToInt_guess(
                                    splitX + 1u);
                                vn.meshDivY = meshDivisionCounterWordToInt_guess(
                                    division - splitX + 1u);
                            }

                            buildBilinearMeshGrid_guess(
                                vn.meshDivX, vn.meshDivY,
                                vn.compositeMeshPoints, gridCorners);
                            if(gridCorners ==
                               unitBezierPatchQuad_guess.data()) {
                                for(auto &point : vn.compositeMeshPoints) {
                                    point = evaluateBezierPatchVector_guess(
                                        vn.transformedMeshControlPoints,
                                        point.x, point.y);
                                }
                            }

                            detail::MotionNode *ancestor = vn.meshAncestor;
                            if(!vn.meshInheritanceSeparator_guess) {
                                while(ancestor != nullptr
                                      && !ancestor->meshInheritanceSeparator_guess) {
                                    if(ancestor->hasMeshData) {
                                        for(auto &point : vn.compositeMeshPoints) {
                                            point = mapMeshPointThroughAncestor_guess(
                                                point, *ancestor);
                                        }
                                        const auto mappedPosition =
                                            mapMeshPositionThroughAncestor_guess(
                                                meshPositionX, meshPositionY,
                                                *ancestor);
                                        meshPositionX = mappedPosition.x;
                                        meshPositionY = mappedPosition.y;
                                        _processedMeshVerticesNum +=
                                            static_cast<std::uint32_t>(
                                                vn.compositeMeshPoints.size()) + 1u;
                                    }
                                    ancestor = ancestor->meshAncestor;
                                }
                            }

                            const double shiftBaseX = meshPositionX;
                            const double shiftBaseY = meshPositionY;
                            while(ancestor != nullptr) {
                                if(ancestor->hasMeshData) {
                                    const auto mappedPosition =
                                        mapMeshPositionThroughAncestor_guess(
                                            meshPositionX, meshPositionY,
                                            *ancestor);
                                    meshPositionX = mappedPosition.x;
                                    meshPositionY = mappedPosition.y;
                                    ++_processedMeshVerticesNum;
                                }
                                ancestor = ancestor->meshAncestor;
                            }

                            if(shiftBaseX != meshPositionX
                               || shiftBaseY != meshPositionY) {
                                const float deltaX = static_cast<float>(
                                    meshPositionX - shiftBaseX);
                                const float deltaY = static_cast<float>(
                                    meshPositionY - shiftBaseY);
                                for(auto &point : vn.compositeMeshPoints) {
                                    point.x += deltaX;
                                    point.y += deltaY;
                                }
                            }
                        } else if(vn.meshType == 1) {
                            // A top-level mesh does not materialize the composite
                            // point vector here, but the references still charge
                            // the tessellation grid that rendering will process.
                            const auto division = scaledOwnMeshDivision_guess(
                                getMeshDivisionRatio(),
                                static_cast<std::uint32_t>(vn.meshDivision));
                            const auto width =
                                meshDoubleToUnsignedTowardZeroSaturated_guess(cw);
                            const auto height =
                                meshDoubleToUnsignedTowardZeroSaturated_guess(ch);
                            const auto denominator = width + height;
                            const auto splitX = unsignedDivideA64Profile_guess(
                                division * width, denominator);
                            _processedMeshVerticesNum +=
                                (division - splitX + 2u) * (splitX + 2u);
                        }

                        // A forced-visible node mirrors evaluated geometry into
                        // its retained emoteEdit object. Conversion/property
                        // exceptions unwind through the retained dispatches.
                        if(vn.hasEmoteEdit_guess()) {
                            mirrorForceVisibleGeometry_guess(
                                vn.emoteEditVariant,
                                meshPositionX, meshPositionY,
                                m11, m12, m21, m22,
                                cw, ch, totalOX, totalOY,
                                vn.accumulated.flipX,
                                vn.accumulated.flipY,
                                vn.accumulated.scaleX,
                                vn.accumulated.scaleY,
                                vn.accumulated.slantX,
                                vn.accumulated.angle);
                        }
                    }
                }
            }
        }

    }

    void Player::updateLayersPhase3_Visibility() {
        auto &nodes = _nodes;
        // Root is not visited. Its constructor-zeroed draw flag and null
        // visible-ancestor state are deliberately left untouched.
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            // Native parent indices are consumed without a bounds guard.
            const int parentIndex = node.parentIndex;
            auto &parent = nodes[static_cast<size_t>(parentIndex)];
            node.visibleAncestor = selectVisibleAncestor_guess(parent);

            node.drawFlag = selectNodeDrawFlag_guess(
                node.activeSlot().done,
                node.stencilType,
                node.accumulated.active,
                node.hasEmoteEdit_guess(),
                node.nodeType,
                _preview,
                node.source.valid);
        }

    }

    void Player::updateLayersPhase3_CameraNode() {
        auto &nodes = _nodes;
        // The first active type-5 node wins. A missing camera does not clear
        // the previously computed offsets or camera-query state.
        _hasCamera = false;
        for (size_t i = 1; i < nodes.size(); ++i) {
            const auto &camNode = nodes[i];
            if (camNode.nodeType != 5 || !camNode.accumulated.active) continue;
            _hasCamera = true;

            const detail::MotionNode *targetNode = nullptr;
            const detail::MotionNode *focusNode = &camNode;
            const auto &cameraTarget = camNode.activeSlot().cameraTarget;
            if(!cameraTarget.IsEmpty()) {
                targetNode = findNodeByRawLabel_guess(cameraTarget, false);
            }
            if(targetNode != nullptr) {
                focusNode = targetNode;
            }

            // Camera focus consumes the vertex-computation output, not the
            // accumulated transform. Both projected deltas narrow to float
            // before the root Player's affine transform is applied.
            const auto &rootNode = nodes[0];
            const float deltaX = narrowAndNegateCameraNodeDelta_guess(
                focusNode->vertexPosX - rootNode.vertexPosX);
            const float deltaY = narrowAndNegateCameraNodeDelta_guess(
                focusNode->vertexPosZ * _zFactor + focusNode->vertexPosY
                - (_zFactor * rootNode.vertexPosZ + rootNode.vertexPosY));

            const auto &drawAffineOwner = *_rootPlayer;
            _cameraOffsetX = quantizeCameraNodeOffset_guess(
                drawAffineOwner._drawAffineM11,
                drawAffineOwner._drawAffineM12,
                deltaX, deltaY);
            _cameraOffsetY = quantizeCameraNodeOffset_guess(
                drawAffineOwner._drawAffineM21,
                drawAffineOwner._drawAffineM22,
                deltaX, deltaY);

            if (_cameraActive) {
                _cameraFov = camNode.cameraFov;
                _cameraPosX = camNode.vertexPosX;
                _cameraPosY = camNode.vertexPosY;
                _cameraPosZ = camNode.vertexPosZ;
                // Empty target and lookup miss deliberately retain the previous
                // cross-frame target coordinates.
                if(targetNode != nullptr) {
                    _cameraTargetX = targetNode->vertexPosX;
                    _cameraTargetY = targetNode->vertexPosY;
                    _cameraTargetZ = targetNode->vertexPosZ;
                }
                _cameraAngle = cameraNodeAngleDegrees_guess(
                    _cameraPosX, _cameraPosZ,
                    _cameraTargetX, _cameraTargetZ);
            }
            break;
        }

    }

    void Player::updateLayersPhase3_ShapeAABB() {
        auto &nodes = _nodes;
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            // Native parent indices are consumed without a bounds guard.
            const auto &parent = nodes[static_cast<size_t>(sn.parentIndex)];
            if (sn.nodeType != 7 || !sn.accumulated.active) {
                sn.clipAABB = parent.clipAABB;
                continue;
            }

            const double m11 = sn.matrix.m11, m12 = sn.matrix.m12;
            const double m21 = sn.matrix.m21, m22 = sn.matrix.m22;
            const double px = sn.accumulated.posX, py = sn.accumulated.posY;
            const auto &slot = sn.activeSlot();
            const double originX = slot.oy * m12 + slot.ox * m11;
            const double originY = slot.oy * m22 + slot.ox * m21;

            const auto xBounds = orderShapeAxis_guess(
                px - m12 * 16.0 - m11 * 16.0 - originX,
                m12 * 16.0 + px + m11 * 16.0 - originX);
            const auto yBounds = orderShapeAxis_guess(
                py - m22 * 16.0 - m21 * 16.0 - originY,
                m22 * 16.0 + py + m21 * 16.0 - originY);
            const double projectedZ = _zFactor * sn.accumulated.posZ;

            sn.shapeAABB[0] = static_cast<float>(xBounds.minimum);
            sn.shapeAABB[1] = static_cast<float>(
                projectedZ + yBounds.minimum);
            sn.shapeAABB[2] = static_cast<float>(xBounds.maximum);
            sn.shapeAABB[3] = static_cast<float>(
                projectedZ + yBounds.maximum);

            if (parent.clipAABB != nullptr) {
                const float *parentClip = parent.clipAABB;
                sn.shapeAABB[0] = clampShapeMinimumToParent_guess(
                    sn.shapeAABB[0], parentClip[0]);
                sn.shapeAABB[1] = clampShapeMinimumToParent_guess(
                    sn.shapeAABB[1], parentClip[1]);
                sn.shapeAABB[2] = clampShapeMaximumToParent_guess(
                    sn.shapeAABB[2], parentClip[2]);
                sn.shapeAABB[3] = clampShapeMaximumToParent_guess(
                    sn.shapeAABB[3], parentClip[3]);
            }
            sn.clipAABB = sn.shapeAABB;
        }

    }

    void Player::updateLayersPhase3_ShapeGeometry() {
        auto &nodes = _nodes;
        // Eligible type-1 nodes update only the slots owned by their current
        // shape kind. Skipped and unused slots retain their prior bytes.
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            if (sn.nodeType != 1 || sn.activeSlot().done) continue;
            const auto &slot = sn.activeSlot();
            updateShapeGeometryRecord_guess(
                sn.shapeGeometry,
                sn.shapeType,
                sn.vertexPosX,
                sn.vertexPosY,
                sn.accumulated.scaleX,
                sn.accumulated.scaleY,
                sn.matrix.m11,
                sn.matrix.m12,
                sn.matrix.m21,
                sn.matrix.m22,
                slot.ox,
                slot.oy);
        }

    }


} // namespace motion
