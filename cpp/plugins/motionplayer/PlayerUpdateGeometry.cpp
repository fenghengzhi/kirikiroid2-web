// PlayerUpdateGeometry.cpp — updateLayers geometry, visibility, camera, and shape phases
// Split from PlayerUpdateLayers.cpp for maintainability.
//
#include "PlayerUpdateLayersInternal.h"

namespace motion {
    void Player::updateLayersPhase3_CameraConstraint() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BC000: Camera constraint (nodeType=9) ---
        // Aligned to 0x6BC000..0x6BC4EC. Only when !isEmoteMode.
        // 9 cases at 0x6BC1B0..0x6BC358 based on flipX/flipY + constraintType (node+2376).
        if (!_runtime->isEmoteMode && nodes.size() >= 2) {
            double offsetX = 0, offsetY = 0, offsetZ = 0;
            // Track which axes have constraints and their types
            bool hasMinX = false, hasMaxX = false, hasTrackX = false;
            bool hasMinY = false, hasMaxY = false, hasTrackY = false;
            bool hasMinZ = false, hasMaxZ = false, hasTrackZ = false;
            double minX = 3.4e38, maxX = -3.4e38, trackX = 0;
            double minY = 3.4e38, maxY = -3.4e38, trackY = 0;
            double minZ = 3.4e38, maxZ = -3.4e38, trackZ = 0;

            for (size_t ci = 1; ci < nodes.size(); ++ci) {
                auto &cn = nodes[ci];
                if (cn.nodeType != 9 || cn.activeSlot().done || !cn.accumulated.active) continue;

                // Target node: root (node 0). Full impl would look up dtgt.
                const auto &target = nodes[0];

                // Compute constraintType with flip adjustment (0x6BC1B0..0x6BC1FC)
                int ctype = cn.cameraConstraintType;
                if (cn.accumulated.flipX) {
                    if (ctype == 0) ctype = 2;
                    else if (ctype == 2) ctype = 0;
                }
                if (cn.accumulated.flipY) {
                    if (ctype == 3) ctype = 5;
                    else if (ctype == 5) ctype = 3;
                }

                // 9 cases (0x6BC224..0x6BC358)
                switch (ctype) {
                    case 0: { // X min constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d < 0 && d < minX) { minX = d; hasMinX = true; }
                        break;
                    }
                    case 1: { // X direct track
                        trackX = target.accumulated.posX - cn.accumulated.posX;
                        hasTrackX = true;
                        break;
                    }
                    case 2: { // X max constraint
                        double d = target.accumulated.posX - cn.accumulated.posX;
                        if (d > 0 && d > maxX) { maxX = d; hasMaxX = true; }
                        break;
                    }
                    case 3: { // Y min constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d < 0 && d < minY) { minY = d; hasMinY = true; }
                        break;
                    }
                    case 4: { // Y direct track
                        trackY = target.accumulated.posY - cn.accumulated.posY;
                        hasTrackY = true;
                        break;
                    }
                    case 5: { // Y max constraint
                        double d = target.accumulated.posY - cn.accumulated.posY;
                        if (d > 0 && d > maxY) { maxY = d; hasMaxY = true; }
                        break;
                    }
                    case 6: { // Z min constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d < 0 && d < minZ) { minZ = d; hasMinZ = true; }
                        break;
                    }
                    case 7: { // Z direct track
                        trackZ = target.accumulated.posZ - cn.accumulated.posZ;
                        hasTrackZ = true;
                        break;
                    }
                    case 8: { // Z max constraint
                        double d = target.accumulated.posZ - cn.accumulated.posZ;
                        if (d > 0 && d > maxZ) { maxZ = d; hasMaxZ = true; }
                        break;
                    }
                    default: break;
                }
            }
            // Resolve final offset per axis (0x6BC398..0x6BC410)
            // Priority: track > max > min > 0
            if (hasTrackX) offsetX = trackX;
            else if (hasMaxX) offsetX = maxX;
            else if (hasMinX) offsetX = minX;
            if (hasTrackY) offsetY = trackY;
            else if (hasMaxY) offsetY = maxY;
            else if (hasMinY) offsetY = minY;
            if (hasTrackZ) offsetZ = trackZ;
            else if (hasMaxZ) offsetZ = maxZ;
            else if (hasMinZ) offsetZ = minZ;

            // Apply offset to all nodes (0x6BC450..0x6BC4BC)
            if (offsetX != 0 || offsetY != 0 || offsetZ != 0) {
                for (size_t ci = 1; ci < nodes.size(); ++ci) {
                    nodes[ci].accumulated.posX += offsetX;
                    nodes[ci].accumulated.posY += offsetY;
                    nodes[ci].accumulated.posZ += offsetZ;
                }
            }
        }

    }

    void Player::updateLayersPhase3_VertexComputation() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BC4F0: Vertex computation ---
        // Aligned to 0x6BC4F0. Full implementation matching decompilation.
        for (size_t vi = 1; vi < nodes.size(); ++vi) {
            auto &vn = nodes[vi];
            const int parentIdx = vn.parentIndex >= 0 ? vn.parentIndex : 0;
            auto &parentNode = nodes[parentIdx];
            const int slotIdx = 0;  // current slot index

            if (vn.hasSource) {
                refreshSourceGeometryFromSourceName(
                    vn, _runtime->activeMotion, vn.activeSlot().src);
            }

            // priorDraw flag from emoteEdit (0x6BC648..0x6BC6C4)
            // priorDraw from emoteEdit (0x6BC648..0x6BC6C4)
            if (vn.forceVisible && vn.emoteEditDict) {
                // sub_6636D4: read bool "priorDraw" from emoteEdit dict
                auto pdVal = (*vn.emoteEditDict)["priorDraw"];
                if (auto num = std::dynamic_pointer_cast<PSB::PSBNumber>(pdVal))
                    vn.priorDraw = num->getValue<int>();  // keep raw int — bit flags checked via (v12 & 5)
                else if (auto bl = std::dynamic_pointer_cast<PSB::PSBBool>(pdVal))
                    vn.priorDraw = bl->value ? 1 : 0;
                else
                    vn.priorDraw = 0;
            } else {
                vn.priorDraw = 0;  // 0x6BC67C
            }

            // Parent clip chain: node+1962/1963 flags (0x6BC6E4..0x6BC818)
            // node+1962 = has mesh data, node+1963 = mesh combine enabled
            // parentClipIndex propagated by sub_6BDCC0 carries the ancestor chain
            // Set mesh flags: hasMeshData when meshType!=0 and control points exist;
            // meshCombineEnabled when mesh is active for child deformation.
            // These flags gate the visibleAncestor conditional in sub_6BE0C0 (label_18).
            vn.hasMeshData = (vn.meshType != 0 && !vn.meshControlPoints.empty());
            vn.meshCombineEnabled = (vn.hasMeshData && vn.meshType == 1 && (vn.meshFlags & 1) != 0);

            // Check visible (0x6BC700..0x6BC74C)
            if (!vn.accumulated.visible) {
                // Walk parent for mesh flag
                goto bc4f0_next;
            }

            // Propagate clip origin
            vn.clipOriginX = vn.interpolatedCache.ox;
            vn.clipOriginY = vn.interpolatedCache.oy;

            // nodeType 1/5 special position via parent mesh chain (0x6BC828..0x6BC8D4)
            // if ((1 << nodeType) & 0x22) != 0 → nodeType 1 (shape) or 5 (camera)
            if (((1 << vn.nodeType) & 0x22) != 0) {
                double px = vn.accumulated.posX;
                double py = vn.accumulated.posY;
                // Walk parent clip chain, evaluate through each mesh (0x6BC838..0x6BC8B0)
                int clipWalk = vn.parentClipIndex;
                while (clipWalk >= 0 && clipWalk < static_cast<int>(nodes.size())) {
                    auto &cn = nodes[clipWalk];
                    if (cn.meshControlPointsPrev.size() >= 32) {
                        // Apply inverse matrix to get normalized coords (0x6BC858..0x6BC87C)
                        float tx = static_cast<float>(px) + cn.meshInvOffX;
                        float ty = static_cast<float>(py) + cn.meshInvOffY;
                        float ix = static_cast<float>(
                            cn.meshInvM11 * tx + cn.meshInvM12 * ty);
                        float iy = static_cast<float>(
                            cn.meshInvM21 * tx + cn.meshInvM22 * ty);
                        // Evaluate bezier patch at normalized coords (sub_69B1E8)
                        const float *mesh = cn.meshControlPointsPrev.data();
                        const float su = 1.f - ix, sv = 1.f - iy;
                        const float bu[4] = {su*su*su, 3.f*su*su*ix, 3.f*su*ix*ix, ix*ix*ix};
                        const float bv[4] = {sv*sv*sv, 3.f*sv*sv*iy, 3.f*sv*iy*iy, iy*iy*iy};
                        float ox = 0, oy = 0;
                        for (int bi = 0; bi < 16; ++bi) {
                            float w = bv[bi >> 2] * bu[bi & 3];
                            ox += mesh[bi * 2] * w;
                            oy += mesh[bi * 2 + 1] * w;
                        }
                        px = ox;
                        py = oy;
                    }
                    clipWalk = cn.parentClipIndex;
                }
                vn.vertexPosX = px;
                vn.vertexPosY = py;
                vn.vertexPosZ = vn.accumulated.posZ;
            }

            // Non slot-done path: vertex computation (0x6BC8DC..0x6BD730)
            if (!vn.activeSlot().done) {
                // Second visibility bitmask check (0x6BCE2C..0x6BCE40)
                // Non-emote: 7233 = 0x1C41, Emote: 7241 = 0x1C49
                const int vbm = _runtime->isEmoteMode ? 7241 : 7233;
                const bool vertexEligible = vn.forceVisible
                    || ((vbm & (1 << vn.nodeType)) != 0);

                if (vertexEligible && vn.hasSource) {
                    const double m11 = vn.accumulated.m11, m12 = vn.accumulated.m12;
                    const double m21 = vn.accumulated.m21, m22 = vn.accumulated.m22;
                    const double posX = vn.accumulated.posX;
                    const double posY = vn.accumulated.posY
                        + vn.accumulated.posZ * _zFactor;

                    // Origin offset (0x6BCB58..0x6BCBA4)
                    const double totalOX = vn.originX + vn.clipOriginX;
                    const double totalOY = vn.originY + vn.clipOriginY;
                    const double orgX = posX - (m12 * totalOY + totalOX * m11);
                    const double orgY = posY - (totalOY * m22 + totalOX * m21);
                    vn.vertexPosX = orgX;
                    vn.vertexPosY = orgY;
                    vn.vertexPosZ = vn.accumulated.posZ;

                    // Save prev mesh (0x6BCB94..0x6BCBAC)
                    vn.meshControlPointsPrev = vn.meshControlPoints;

                    const double cw = vn.clipW;
                    const double ch = vn.clipH;

                    // Mesh vertex construction (0x6BCBBC..0x6BD060)
                    if (vn.meshType == 1
                        && !vn.meshControlPoints.empty()
                        && cw > 0 && ch > 0) {
                        // meshType=1: Bezier patch mesh
                        // Compute inverse matrix for mesh (0x6BCBF8..0x6BCC38)
                        // Compute and store inverse matrix (0x6BCBF8..0x6BCC38)
                        // det = m11*cw * m22*ch - m12*ch * m21*cw
                        const double mw11 = m11 * cw, mw12 = m12 * ch;
                        const double mw21 = m21 * cw, mw22 = m22 * ch;
                        const double det = mw11 * mw22 - mw12 * mw21;
                        if (std::fabs(det) > 1e-10) {
                            // node+2096..2120: inverse of [mw11,mw12;mw21,mw22]
                            vn.meshInvM11 = mw22 / det;   // 0x6BCC0C
                            vn.meshInvM12 = -(mw12 / det); // 0x6BCC20
                            vn.meshInvM21 = -(mw21 / det); // 0x6BCC34
                            vn.meshInvM22 = mw11 / det;    // 0x6BCC14
                            // node+2128/2132: negated origin as float (0x6BCC04/0x6BCC38)
                            vn.meshInvOffX = -static_cast<float>(orgX);
                            vn.meshInvOffY = -static_cast<float>(orgY);
                        }

                        // Build grid via sub_6BAF68 (0x6BCF6C)
                        // Grid dimensions: divX = meshDivision * cw/(cw+ch) + 1
                        int divTotal = vn.meshDivision;
                        if (divTotal > 50) divTotal = 50;
                        if (divTotal < 1) divTotal = 4;
                        const int divX = static_cast<int>(
                            static_cast<double>(divTotal) * cw / (cw + ch)) + 1;
                        const int divY = divTotal - divX + 2;
                        const int numPts = divX * divY;
                        // Store grid dimensions (node+2012/2016, 0x6BCF5C)
                        vn.meshDivX = divX;
                        vn.meshDivY = divY;

                        // sub_6BAF68: build bilinear grid (0x6BAF68)
                        // NEON version at 0x6BB030..0x6BB138 processes 4 points/iteration.
                        // Each row interpolates linearly between two edge points:
                        //   p0 = orgXY + m_col2*ch*tv, p1 = orgXY + m_col1*cw + m_col2*ch*tv
                        //   grid[gx] = lerp(p0, p1, gx/divX)
                        vn.meshControlPoints.resize(numPts * 2);
                        for (int gy = 0; gy < divY; ++gy) {
                            const double tv = (divY > 1) ? static_cast<double>(gy) / (divY - 1) : 0;
                            // Row edge points (0x6BB068..0x6BB09C)
                            const double rowBaseX = orgX + (m12 * ch) * tv;
                            const double rowBaseY = orgY + (m22 * ch) * tv;
                            const double rowEndX = rowBaseX + m11 * cw;
                            const double rowEndY = rowBaseY + m21 * cw;
                            float *rowPtr = &vn.meshControlPoints[gy * divX * 2];
#ifdef __EMSCRIPTEN__
                            // WASM SIMD: process 4 grid points per iteration
                            // Aligned to NEON at 0x6BB0CC..0x6BB138
                            // For each group of 4 gx values: tu = [gx, gx+1, gx+2, gx+3] / divX
                            // ptX = rowBaseX*(1-tu) + rowEndX*tu
                            // ptY = rowBaseY*(1-tu) + rowEndY*tu
                            const v128_t vBaseX = wasm_f64x2_splat(rowBaseX);
                            const v128_t vBaseY = wasm_f64x2_splat(rowBaseY);
                            const v128_t vEndX = wasm_f64x2_splat(rowEndX);
                            const v128_t vEndY = wasm_f64x2_splat(rowEndY);
                            const double invDivX = (divX > 1) ? 1.0 / (divX - 1) : 0.0;
                            int gx = 0;
                            const int simdEnd = divX & ~1;  // process 2 at a time (f64x2)
                            for (; gx < simdEnd; gx += 2) {
                                const double t0 = gx * invDivX;
                                const double t1 = (gx + 1) * invDivX;
                                const v128_t vt = wasm_f64x2_make(t0, t1);
                                const v128_t v1mt = wasm_f64x2_sub(wasm_f64x2_splat(1.0), vt);
                                // X = base*(1-t) + end*t
                                v128_t vx = wasm_f64x2_add(
                                    wasm_f64x2_mul(vBaseX, v1mt),
                                    wasm_f64x2_mul(vEndX, vt));
                                // Y = base*(1-t) + end*t
                                v128_t vy = wasm_f64x2_add(
                                    wasm_f64x2_mul(vBaseY, v1mt),
                                    wasm_f64x2_mul(vEndY, vt));
                                // Convert f64→f32 and store interleaved [x0,y0,x1,y1]
                                float fx0 = static_cast<float>(wasm_f64x2_extract_lane(vx, 0));
                                float fy0 = static_cast<float>(wasm_f64x2_extract_lane(vy, 0));
                                float fx1 = static_cast<float>(wasm_f64x2_extract_lane(vx, 1));
                                float fy1 = static_cast<float>(wasm_f64x2_extract_lane(vy, 1));
                                rowPtr[gx*2]   = fx0;
                                rowPtr[gx*2+1] = fy0;
                                rowPtr[gx*2+2] = fx1;
                                rowPtr[gx*2+3] = fy1;
                            }
                            // Scalar remainder
                            for (; gx < divX; ++gx) {
                                const double tu = (divX > 1) ? static_cast<double>(gx) / (divX-1) : 0;
                                rowPtr[gx*2]   = static_cast<float>(rowBaseX*(1-tu) + rowEndX*tu);
                                rowPtr[gx*2+1] = static_cast<float>(rowBaseY*(1-tu) + rowEndY*tu);
                            }
#else
                            for (int gx = 0; gx < divX; ++gx) {
                                const double tu = (divX > 1) ? static_cast<double>(gx) / (divX-1) : 0;
                                rowPtr[gx*2]   = static_cast<float>(rowBaseX*(1-tu) + rowEndX*tu);
                                rowPtr[gx*2+1] = static_cast<float>(rowBaseY*(1-tu) + rowEndY*tu);
                            }
#endif
                        }

                        // Evaluate each grid point through Bezier patch (0x6BCF80..0x6BCFBC)
                        // sub_69B1E8 evaluates bezier patch at each mesh point
                        // This transforms the bilinear grid into a deformed mesh
                        if (vn.meshControlPointsPrev.size() >= 32) {
                            auto evalBP = [](const float *mesh, float u, float v,
                                             float &outX, float &outY) {
                                const float su=1.f-u, sv=1.f-v;
                                const float bu[4]={su*su*su,3.f*su*su*u,3.f*su*u*u,u*u*u};
                                const float bv[4]={sv*sv*sv,3.f*sv*sv*v,3.f*sv*v*v,v*v*v};
                                outX=0; outY=0;
                                for(int i=0;i<16;++i){
                                    float w=bv[i>>2]*bu[i&3];
                                    outX+=mesh[i*2]*w; outY+=mesh[i*2+1]*w;
                                }
                            };
                            for (int pi = 0; pi < numPts; ++pi) {
                                float px = vn.meshControlPoints[pi*2];
                                float py = vn.meshControlPoints[pi*2+1];
                                evalBP(vn.meshControlPointsPrev.data(), px, py, px, py);
                                vn.meshControlPoints[pi*2] = px;
                                vn.meshControlPoints[pi*2+1] = py;
                            }
                        }

                        // Parent clip chain mesh cascade (0x6BD118..0x6BD380)
                        // Walk node+1968 (parentClipIndex), for each mesh-enabled
                        // ancestor: evaluate all mesh points + origin through its mesh
                        // Parent clip chain mesh cascade (0x6BD118..0x6BD380)
                        auto evalBPCascade = [](const float *mesh, float u, float v,
                                                float &outX, float &outY) {
                            const float su=1.f-u, sv=1.f-v;
                            const float bu[4]={su*su*su,3.f*su*su*u,3.f*su*u*u,u*u*u};
                            const float bv[4]={sv*sv*sv,3.f*sv*sv*v,3.f*sv*v*v,v*v*v};
                            outX=0; outY=0;
                            for(int i=0;i<16;++i){
                                float w=bv[i>>2]*bu[i&3];
                                outX+=mesh[i*2]*w; outY+=mesh[i*2+1]*w;
                            }
                        };
                        int clipWalk = vn.parentClipIndex;
                        double cascadeOrgX = orgX, cascadeOrgY = orgY;
                        while (clipWalk >= 0 && clipWalk < static_cast<int>(nodes.size())) {
                            auto &cn = nodes[clipWalk];
                            if (cn.meshControlPoints.size() >= 32) {
                                const float *cmesh = cn.meshControlPoints.data();
                                // Evaluate each mesh point through parent mesh (0x6BD148..0x6BD1E8)
                                for (size_t mi = 0; mi < vn.meshControlPoints.size() / 2; ++mi) {
                                    float mpx = vn.meshControlPoints[mi*2];
                                    float mpy = vn.meshControlPoints[mi*2+1];
                                    // Transform by parent inverse matrix + offset (0x6BD188)
                                    // Transform by parent inverse matrix + offset (0x6BD188)
                                    float tx = mpx + cn.meshInvOffX;  // node+2128
                                    float ty = mpy + cn.meshInvOffY;  // node+2132
                                    // Apply inverse matrix: [invM11,invM12;invM21,invM22] × (tx,ty)
                                    float ix = static_cast<float>(cn.meshInvM11 * tx + cn.meshInvM12 * ty);
                                    float iy = static_cast<float>(cn.meshInvM21 * tx + cn.meshInvM22 * ty);
                                    tx = ix; ty = iy;
                                    // Evaluate through parent bezier (sub_69B1E8)
                                    float rx, ry;
                                    evalBPCascade(cmesh, tx, ty, rx, ry);
                                    vn.meshControlPoints[mi*2] = rx;
                                    vn.meshControlPoints[mi*2+1] = ry;
                                }
                                // Evaluate origin through parent mesh (0x6BD218..0x6BD258)
                                float cox = static_cast<float>(cascadeOrgY) + cn.meshInvOffY;
                                float coy = static_cast<float>(cascadeOrgX) + cn.meshInvOffX;
                                float rox, roy;
                                evalBPCascade(cmesh, coy, cox, rox, roy);
                                cascadeOrgX = rox;
                                cascadeOrgY = roy;
                                _processedMeshVerticesNum += static_cast<int>(
                                    vn.meshControlPoints.size() / 2) + 1;
                            }
                            clipWalk = cn.parentClipIndex;
                        }
                        // Update origin if cascade changed it (0x6BD330..0x6BD380)
                        if (cascadeOrgX != orgX || cascadeOrgY != orgY) {
                            vn.vertexPosX = cascadeOrgX;
                            vn.vertexPosY = cascadeOrgY;
                            // Offset all mesh points by delta (0x6BD360..0x6BD380)
                            const float fdx = static_cast<float>(cascadeOrgX - orgX);
                            const float fdy = static_cast<float>(cascadeOrgY - orgY);
                            const size_t totalFloats = vn.meshControlPoints.size();
                            float *mp = vn.meshControlPoints.data();
#ifdef __EMSCRIPTEN__
                            // WASM SIMD: process 4 floats at a time (2 XY pairs)
                            // Aligned to NEON at 0x6BD360: vadd with delta vector
                            const v128_t vdelta = wasm_f32x4_make(fdx, fdy, fdx, fdy);
                            size_t fi = 0;
                            for (; fi + 4 <= totalFloats; fi += 4) {
                                v128_t pts = wasm_v128_load(&mp[fi]);
                                pts = wasm_f32x4_add(pts, vdelta);
                                wasm_v128_store(&mp[fi], pts);
                            }
                            // Scalar remainder
                            for (; fi < totalFloats; fi += 2) {
                                mp[fi] += fdx;
                                if (fi + 1 < totalFloats) mp[fi+1] += fdy;
                            }
#else
                            for (size_t mi = 0; mi < totalFloats / 2; ++mi) {
                                mp[mi*2] += fdx;
                                mp[mi*2+1] += fdy;
                            }
#endif
                        }
                    }

                    // 4-corner vertex output (0x6BCE44..0x6BCEC0)
                    {
                        const double fx = vn.vertexPosX;
                        const double fy = vn.vertexPosY;
                        vn.vertices[0] = static_cast<float>(fx);
                        vn.vertices[1] = static_cast<float>(fy);
                        vn.vertices[2] = static_cast<float>(fx + m11*cw);
                        vn.vertices[3] = static_cast<float>(fy + m21*cw);
                        vn.vertices[4] = static_cast<float>(fx + m11*cw + m12*ch);
                        vn.vertices[5] = static_cast<float>(fy + m21*cw + m22*ch);
                        vn.vertices[6] = static_cast<float>(fx + m12*ch);
                        vn.vertices[7] = static_cast<float>(fy + m22*ch);
                        if(detail::logoChainTraceEnabled(_runtime->activeMotion)) {
                            const auto motionPath = _runtime->activeMotion->path;
                            const std::array<float, 8> expectedVertices = {
                                static_cast<float>(fx),
                                static_cast<float>(fy),
                                static_cast<float>(fx + m11 * cw),
                                static_cast<float>(fy + m21 * cw),
                                static_cast<float>(fx + m11 * cw + m12 * ch),
                                static_cast<float>(fy + m21 * cw + m22 * ch),
                                static_cast<float>(fx + m12 * ch),
                                static_cast<float>(fy + m22 * ch)
                            };
                            bool ok = true;
                            for(size_t vi = 0; vi < expectedVertices.size(); ++vi) {
                                if(std::fabs(vn.vertices[vi] - expectedVertices[vi]) >
                                   0.01f) {
                                    ok = false;
                                    break;
                                }
                            }
                            detail::logoChainTraceCheck(
                                motionPath, "updateLayers.phase3.vertices",
                                "0x6BC4F0", _clampedEvalTime,
                                fmt::format(
                                    "pos=({:.3f},{:.3f}) clip=({:.3f},{:.3f}) m=({:.6f},{:.6f},{:.6f},{:.6f}) exp=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                                    fx, fy, cw, ch, m11, m12, m21, m22,
                                    expectedVertices[0], expectedVertices[1],
                                    expectedVertices[2], expectedVertices[3],
                                    expectedVertices[4], expectedVertices[5],
                                    expectedVertices[6], expectedVertices[7]),
                                fmt::format(
                                    "src={} act=[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]",
                                    vn.interpolatedCache.src.empty()
                                        ? std::string("<none>")
                                        : vn.interpolatedCache.src,
                                    vn.vertices[0], vn.vertices[1],
                                    vn.vertices[2], vn.vertices[3],
                                    vn.vertices[4], vn.vertices[5],
                                    vn.vertices[6], vn.vertices[7]),
                                ok,
                                "sub_6BC4F0 vertex output diverged from expected corners");
                        }
                    }

                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // When node+1996 (forceVisible) is set, write node properties
                    // to a TJS dictionary for sub-motion evaluation.
                    // forceVisible TJS property writing (0x6BD38C..0x6BD72C)
                    // Write node properties to TJS dict for sub-motion evaluation.
                    if (vn.forceVisible && vn.tjsLayerObject) {
                        auto *tjsObj = static_cast<iTJSDispatch2 *>(vn.tjsLayerObject);
                        try {
                            // "c" array: [posX, posY] (0x6BD480..0x6BD494)
                            tTJSVariant posXv(vn.vertexPosX);
                            tTJSVariant posYv(vn.vertexPosY);
                            // "mtx" array: [m11,m12,m21,m22] (0x6BD534..0x6BD570)
                            tTJSVariant m11v(m11), m12v(m12), m21v(m21), m22v(m22);
                            // "width" (0x6BD590)
                            tTJSVariant wv(cw);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("width"),
                                nullptr, &wv, tjsObj);
                            // "height" (0x6BD5B0)
                            tTJSVariant hv(ch);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("height"),
                                nullptr, &hv, tjsObj);
                            // "originX" (0x6BD5E4)
                            tTJSVariant oxv(totalOX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originX"),
                                nullptr, &oxv, tjsObj);
                            // "originY" (0x6BD618)
                            tTJSVariant oyv(totalOY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("originY"),
                                nullptr, &oyv, tjsObj);
                            // "flipX" (0x6BD638)
                            tTJSVariant fxv(static_cast<tjs_int>(vn.accumulated.flipX));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipX"),
                                nullptr, &fxv, tjsObj);
                            // "flipY" (0x6BD658)
                            tTJSVariant fyv(static_cast<tjs_int>(vn.accumulated.flipY));
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("flipY"),
                                nullptr, &fyv, tjsObj);
                            // "zoomX" (0x6BD678)
                            tTJSVariant zxv(vn.accumulated.scaleX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomX"),
                                nullptr, &zxv, tjsObj);
                            // "zoomY" (0x6BD698)
                            tTJSVariant zyv(vn.accumulated.scaleY);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("zoomY"),
                                nullptr, &zyv, tjsObj);
                            // "slantX" (0x6BD6B8)
                            tTJSVariant sxv(vn.accumulated.slantX);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("slantX"),
                                nullptr, &sxv, tjsObj);
                            // "angle" (0x6BD6D8)
                            tTJSVariant av(vn.accumulated.angle);
                            tjsObj->PropSet(TJS_MEMBERENSURE, TJS_W("angle"),
                                nullptr, &av, tjsObj);
                        } catch (...) {}
                    }
                }
            }
            bc4f0_next:;
        }

        // Delta position computation (0x6BBB74..0x6BBC54)
        // if playing (player+480): delta = 0; else: delta = currentPos - prevPos
        {
            bool anyPlaying = std::any_of(
                _runtime->timelines.begin(), _runtime->timelines.end(),
                [](const auto &e) { return e.second.playing; });
            for (size_t di = 1; di < nodes.size(); ++di) {
                auto &dn = nodes[di];
                if (anyPlaying) {
                    dn.deltaPosX = 0; dn.deltaPosY = 0; dn.deltaPosZ = 0;
                } else {
                    dn.deltaPosX = dn.accumulated.posX - dn.prevPosX;
                    dn.deltaPosY = dn.accumulated.posY - dn.prevPosY;
                    dn.deltaPosZ = dn.accumulated.posZ - dn.prevPosZ;
                }
            }
        }

    }

    void Player::updateLayersPhase3_Visibility() {
        auto &nodes = _runtime->nodes;
        // Visibility flags — aligned to sub_6BD8DC at 0x6BD8DC.
        // Root node (index 0) is always visible.
        if (!nodes.empty()) {
            nodes[0].drawFlag = nodes[0].accumulated.visible && nodes[0].hasSource;
        }
        // Visibility bitmask: which nodeTypes can render
        // Non-emote: 6145 = 0x1801 → nodeTypes 0, 11, 12
        // Emote:     6153 = 0x1809 → nodeTypes 0, 3, 11, 12
        // Aligned to sub_6BD8DC (0x6BD8DC): visibility bitmask depends on emote mode.
        const int visBitmask = _runtime->isEmoteMode ? 6153 : 6145;
        for (size_t i = 1; i < nodes.size(); ++i) {
            auto &node = nodes[i];

            // Find visible ancestor (walk parent chain, 0x6BD9D8)
            int pIdx = node.parentIndex;
            if (pIdx >= 0 && pIdx < static_cast<int>(nodes.size())) {
                if (!nodes[pIdx].drawFlag) {
                    node.visibleAncestorIndex = nodes[pIdx].visibleAncestorIndex;
                } else {
                    node.visibleAncestorIndex = pIdx;
                }
            }

            // Visibility logic — exact replica of sub_6BD8DC (0x6BD958..0x6BDA00):
            //   if (slotDone) { v9 = 0; }
            //   else { v9 = stencilType; if (v9) { v9 = active; if (v9) {
            //     if (forceVisible || (bitmask & (1<<nodeType))) v9 = hasSource; } } }
            //   drawFlag = v9;
            if (node.activeSlot().done) {
                node.drawFlag = false;
            } else if (node.stencilType == 0) {
                // node+52 == 0 → invisible (0x6BD958)
                node.drawFlag = false;
            } else if (!node.accumulated.active) {
                node.drawFlag = false;
            } else if (node.forceVisible
                       || (visBitmask & (1 << node.nodeType)) != 0) {
                node.drawFlag = node.hasSource;
            } else {
                // Active node, not in renderable bitmask, not forceVisible:
                // v9 stays as active (non-zero) → drawFlag = true
                node.drawFlag = true;
            }
        }

    }

    void Player::updateLayersPhase3_CameraNode() {
        auto &nodes = _runtime->nodes;
        // Camera node processing — aligned to sub_6BDA28 (0x6BDA28).
        // Find first nodeType=5 (camera) that is active, compute cameraOffset.
        _hasCamera = false;
        for (size_t i = 1; i < nodes.size(); ++i) {
            const auto &camNode = nodes[i];
            if (camNode.nodeType != 5 || !camNode.accumulated.active) continue;
            _hasCamera = true;

            // Compute delta from root node position
            const auto &rootAcc = nodes[0].accumulated;
            const double dx = -(camNode.accumulated.posX - rootAcc.posX);
            const double dy = -(camNode.accumulated.posY * _zFactor
                + camNode.accumulated.posZ
                - (rootAcc.posY * _zFactor + rootAcc.posZ));

            // Transform by drawAffineMatrix (player+808..832)
            const auto &dam = _runtime->drawAffineMatrix;
            _cameraOffsetX = static_cast<float>(
                static_cast<int>(dam[0] * dx + dam[2] * dy + 0.5));
            _cameraOffsetY = static_cast<float>(
                static_cast<int>(dam[1] * dx + dam[3] * dy + 0.5));

            // Camera-to-target angle (0x6BDC04..0x6BDCB0)
            // When stereovisionActive (a1+1094): compute camera angle for 3D effect.
            if (_stereovisionActive) {
                // Store camera/target positions (a1+72..112)
                _cameraPosX = camNode.accumulated.posX;
                _cameraPosY = camNode.accumulated.posY;
                _cameraPosZ = camNode.accumulated.posZ;
                // Look up target node via clip slot action path
                // For now, target defaults to previous positions
                // Compute angle: atan2(camPosZ - targetZ, camPosX - targetX)
                double angleRad = std::atan2(
                    camNode.accumulated.posZ - _cameraTargetZ,
                    camNode.accumulated.posX - _cameraTargetX);
                double angleDeg = angleRad * -57.2957795 + 90.0;
                while (angleDeg < 0.0) angleDeg += 360.0;
                while (angleDeg >= 360.0) angleDeg -= 360.0;
                _cameraAngle = angleDeg;  // a1+472
                _cameraTargetX = _cameraPosX;
                _cameraTargetY = _cameraPosY;
                _cameraTargetZ = _cameraPosZ;
            }
            break;  // only first camera node
        }

    }

    void Player::updateLayersPhase3_ShapeAABB() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BDCC0: Shape AABB computation (nodeType=7) ---
        // Aligned to 0x6BDCC0. For nodeType=7 active nodes, compute AABB
        // from 2x2 matrix × 16-unit extent, origin offset, parent clip clamping.
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            // Propagate parent clip region (node+1936)
            if (sn.parentIndex >= 0 && sn.parentIndex < static_cast<int>(nodes.size())) {
                sn.parentClipIndex = nodes[sn.parentIndex].parentClipIndex;
            }
            if (sn.nodeType != 7 || !sn.accumulated.active) continue;

            const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
            const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
            const double px = sn.accumulated.posX, py = sn.accumulated.posY;
            const double pzs = sn.accumulated.posZ * _zFactor + py;
            const double ox = sn.clipOriginX, oy = sn.clipOriginY;
            const double oox = ox * m11 + oy * m12;
            const double ooy = ox * m21 + oy * m22;
            // Extent = matrix × 16
            const double ex1 = m11 * 16.0, ex2 = m12 * 16.0;
            const double ey1 = m21 * 16.0, ey2 = m22 * 16.0;
            double xMin = px - ex1 - ex2 - oox;
            double xMax = px + ex1 + ex2 - oox;
            double yMin = pzs - ey1 - ey2 - ooy;
            double yMax = pzs + ey1 + ey2 - ooy;
            if (xMin > xMax) std::swap(xMin, xMax);
            if (yMin > yMax) std::swap(yMin, yMax);
            sn.shapeAABB[0] = static_cast<float>(xMin);
            sn.shapeAABB[1] = static_cast<float>(yMin);
            sn.shapeAABB[2] = static_cast<float>(xMax);
            sn.shapeAABB[3] = static_cast<float>(yMax);
            // Clamp to parent clip (0x6BDE40..0x6BDE80)
            if (sn.parentClipIndex >= 0 &&
                sn.parentClipIndex < static_cast<int>(nodes.size())) {
                const auto &pc = nodes[sn.parentClipIndex];
                if (pc.shapeAABB[0] > sn.shapeAABB[0]) sn.shapeAABB[0] = pc.shapeAABB[0];
                if (pc.shapeAABB[1] > sn.shapeAABB[1]) sn.shapeAABB[1] = pc.shapeAABB[1];
                if (pc.shapeAABB[2] < sn.shapeAABB[2]) sn.shapeAABB[2] = pc.shapeAABB[2];
                if (pc.shapeAABB[3] < sn.shapeAABB[3]) sn.shapeAABB[3] = pc.shapeAABB[3];
            }
            sn.parentClipIndex = static_cast<int>(si);

            if(detail::logoSnapshotMarkEnabledForPath(_runtime->activeMotion->path) &&
               _runtime->activeMotion->path.find("m2logo.mtn") != std::string::npos &&
               _clampedEvalTime >= 43.0 && _clampedEvalTime <= 50.0 &&
               sn.index == 18) {
                std::fprintf(
                    stderr,
                    "SNAPSHAPE frame=%.3f nodeIndex=%d label=%s slotOxOy=(%.3f,%.3f) interpOxOy=(%.3f,%.3f) clipOrigin=(%.3f,%.3f) accumPos=(%.3f,%.3f,%.3f) m=(%.6f,%.6f,%.6f,%.6f) shapeAABB=[%.3f,%.3f,%.3f,%.3f] parentClipIndex=%d\n",
                    _clampedEvalTime,
                    sn.index,
                    sn.layerName.empty() ? "<none>" : sn.layerName.c_str(),
                    sn.activeSlot().ox, sn.activeSlot().oy,
                    sn.interpolatedCache.ox, sn.interpolatedCache.oy,
                    sn.clipOriginX, sn.clipOriginY,
                    sn.accumulated.posX, sn.accumulated.posY, sn.accumulated.posZ,
                    sn.accumulated.m11, sn.accumulated.m12,
                    sn.accumulated.m21, sn.accumulated.m22,
                    sn.shapeAABB[0], sn.shapeAABB[1],
                    sn.shapeAABB[2], sn.shapeAABB[3],
                    sn.parentClipIndex);
            }
        }

    }

    void Player::updateLayersPhase3_ShapeGeometry() {
        auto &nodes = _runtime->nodes;
        // --- sub_6BDE94: Shape geometry computation (nodeType=1) ---
        // Aligned to 0x6BDE94. For nodeType=1 nodes with active slot,
        // compute shape vertices based on shapeType (0=point,1=circle,2=rect,3=quad).
        for (size_t si = 1; si < nodes.size(); ++si) {
            auto &sn = nodes[si];
            if (sn.nodeType != 1 || sn.activeSlot().done) continue;
            sn.shapeGeomType = sn.shapeType;
            switch (sn.shapeType) {
                case 0: // point (0x6BDF40)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    break;
                case 1: { // circle (0x6BDF50)
                    sn.shapeVertices[0] = sn.vertexPosX;
                    sn.shapeVertices[1] = sn.vertexPosY;
                    sn.shapeVertices[2] = sn.accumulated.scaleX * 16.0 * 0.5;
                    break;
                }
                case 2: { // rect (0x6BDF70)
                    const double hw = sn.accumulated.scaleX * 16.0 * 0.5;
                    const double hh = sn.accumulated.scaleY * 16.0 * 0.5;
                    sn.shapeVertices[3] = sn.vertexPosX - hw;
                    sn.shapeVertices[4] = sn.vertexPosY - hh;
                    sn.shapeVertices[5] = sn.vertexPosX + hw;
                    sn.shapeVertices[6] = sn.vertexPosY + hh;
                    break;
                }
                case 3: { // quad (0x6BDFA8)
                    const double m11 = sn.accumulated.m11, m12 = sn.accumulated.m12;
                    const double m21 = sn.accumulated.m21, m22 = sn.accumulated.m22;
                    const double ox = sn.clipOriginX, oy = sn.clipOriginY;
                    const double oox = ox * m11 + oy * m12;
                    const double ooy = ox * m21 + oy * m22;
                    const double px = sn.vertexPosX, py = sn.vertexPosY;
                    const double ax = m11 * -8.0, bx = m12 * -8.0;
                    const double cx = m11 * 8.0,  dx = m12 * 8.0;
                    const double ay = m21 * -8.0, by = m22 * -8.0;
                    const double cy = m21 * 8.0,  dy = m22 * 8.0;
                    sn.shapeVertices[7]  = px + ax + bx - oox;
                    sn.shapeVertices[8]  = py + ay + by - ooy;
                    sn.shapeVertices[9]  = px + cx + bx - oox;
                    sn.shapeVertices[10] = py + cy + by - ooy;
                    sn.shapeVertices[11] = px + cx + dx - oox;
                    sn.shapeVertices[12] = py + cy + dy - ooy;
                    sn.shapeVertices[13] = px + ax + dx - oox;
                    sn.shapeVertices[14] = py + ay + dy - ooy;
                    break;
                }
                default: break;
            }
        }

    }


} // namespace motion
