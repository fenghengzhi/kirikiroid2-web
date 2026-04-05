// PlayerRender.cpp — Drawing/rendering: renderToLayer, draw, frameProgress
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"

using namespace motion::internal;

namespace motion {

    // --- Drawing/rendering ---
    void Player::setClearColor(tjs_int color) { _runtime->clearColor = color; }

    void Player::setResizable(bool v) { _runtime->resizable = v; }

    void Player::removeAllTextures() { _runtime->sourcesByKey.clear(); }

    void Player::removeAllBg() { _runtime->backgrounds.clear(); }

    void Player::removeAllCaption() { _runtime->captions.clear(); }

    void Player::registerBg(tTJSVariant bg) { _runtime->backgrounds.push_back(bg); }

    void Player::registerCaption(tTJSVariant caption) {
        _runtime->captions.push_back(caption);
    }

    void Player::unloadUnusedTextures() {}

    tjs_int Player::alphaOpAdd() { return ++_runtime->alphaOpCounter; }

    tTJSVariant Player::captureCanvas() {
        if(_runtime->lastCanvas.Type() == tvtVoid) {
            draw();
        }
        return _runtime->lastCanvas;
    }

    ttstr Player::resolveCaptureSourcePath() const {
        if(!_runtime->activeMotion) {
            return {};
        }

        std::vector<ttstr> candidates;
        const auto motionPath = detail::widen(_runtime->activeMotion->path);
        const auto baseDir = TVPExtractStoragePath(motionPath);
        for(const auto &candidate : activeSourceCandidates()) {
            pushGraphicCandidates(candidates, detail::widen(candidate));
            detail::appendEmbeddedSourceCandidates(*_runtime->activeMotion,
                                                   candidate, candidates);
            if(!baseDir.IsEmpty()) {
                pushGraphicCandidates(candidates,
                                      baseDir + detail::widen(candidate));
            }
        }

        const auto stem =
            detail::widen(basenameWithoutExtension(_runtime->activeMotion->path));
        pushGraphicCandidates(candidates, stem);
        if(!baseDir.IsEmpty()) {
            pushGraphicCandidates(candidates, baseDir + stem);
        }

        ttstr resolved;
        detail::resolveExistingPath(candidates, resolved);
        return resolved;
    }

    bool Player::renderToD3DAdaptor(D3DAdaptor *adaptor) {
        if(!adaptor || adaptor->getWidth() <= 0 || adaptor->getHeight() <= 0) {
            return false;
        }
        // Guard against recursion: renderToLayer may trigger TJS callbacks
        // that call drawCompat again.
        static bool s_inRenderToD3D = false;
        if(s_inRenderToD3D) return false;
        s_inRenderToD3D = true;
        struct Guard { ~Guard() { s_inRenderToD3D = false; } } guard;

        ensureMotionLoaded();
        if(!_runtime->activeMotion) return false;

        // We need a scratch Layer for the rendering pipeline (LoadImages etc.).
        // Reuse _runtime->lastCanvas if available, otherwise create one via TJS.
        iTJSDispatch2 *scratchLayerObj = nullptr;
        if(_runtime->lastCanvas.Type() == tvtObject &&
           _runtime->lastCanvas.AsObjectNoAddRef()) {
            scratchLayerObj = _runtime->lastCanvas.AsObjectNoAddRef();
        }

        // Create a temporary Layer if we don't have one cached
        bool ownedLayer = false;
        if(!scratchLayerObj) {
            // Get the global Window object to use as parent
            iTJSDispatch2 *global = TVPGetScriptDispatch();
            if(!global) return false;
            tTJSVariant kagVar;
            if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("kag"), nullptr, &kagVar, global)) &&
               kagVar.Type() == tvtObject && kagVar.AsObjectNoAddRef()) {
                // Create: new Layer(kag, kag.primaryLayer)
                tTJSVariant primaryVar;
                kagVar.AsObjectNoAddRef()->PropGet(0, TJS_W("primaryLayer"),
                                                   nullptr, &primaryVar,
                                                   kagVar.AsObjectNoAddRef());
                if(primaryVar.Type() == tvtObject && primaryVar.AsObjectNoAddRef()) {
                    iTJSDispatch2 *layerClass = nullptr;
                    tTJSVariant lcVar;
                    if(TJS_SUCCEEDED(global->PropGet(0, TJS_W("Layer"), nullptr, &lcVar, global)) &&
                       lcVar.Type() == tvtObject) {
                        layerClass = lcVar.AsObjectNoAddRef();
                        tTJSVariant *args[] = { &kagVar, &primaryVar };
                        iTJSDispatch2 *newLayerDisp = nullptr;
                        if(TJS_SUCCEEDED(layerClass->CreateNew(0, nullptr, nullptr,
                                                                &newLayerDisp, 2, args, layerClass)) &&
                           newLayerDisp) {
                            tTJSVariant newLayer(newLayerDisp, newLayerDisp);
                            newLayerDisp->Release();
                            scratchLayerObj = newLayer.AsObjectNoAddRef();
                            _runtime->lastCanvas = newLayer;  // cache it
                            ownedLayer = true;
                        }
                    }
                }
            }
            global->Release();
        }

        if(!scratchLayerObj) return false;

        // Render to the scratch Layer
        if(!renderToLayer(scratchLayerObj, true)) return false;

        // Copy pixels from scratch Layer to D3DAdaptor buffer
        tTJSNI_BaseLayer *layer = nullptr;
        if(TJS_FAILED(scratchLayerObj->NativeInstanceSupport(
               TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
               reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer) {
            return false;
        }

        const int w = adaptor->getWidth();
        const int h = adaptor->getHeight();
        const int layerW = static_cast<int>(layer->GetImageWidth());
        const int layerH = static_cast<int>(layer->GetImageHeight());
        const auto *srcBuf = reinterpret_cast<const std::uint8_t *>(
            layer->GetMainImagePixelBuffer());
        auto srcPitch = layer->GetMainImagePixelBufferPitch();

        if(!srcBuf || srcPitch <= 0 || layerW <= 0 || layerH <= 0) return false;

        // Resize adaptor buffer if needed
        if(w != layerW || h != layerH) {
            adaptor->setSize(layerW, layerH);
        }
        adaptor->clearBuffer();

        auto *dstBuf = adaptor->getBuffer();
        const auto dstPitch = adaptor->getBufferPitch();
        const int copyH = std::min(layerH, adaptor->getHeight());
        const int copyRowBytes = std::min(
            static_cast<int>(layerW * 4), dstPitch);

        for(int y = 0; y < copyH; ++y) {
            std::memcpy(dstBuf + dstPitch * y,
                        srcBuf + srcPitch * y,
                        static_cast<size_t>(copyRowBytes));
        }

        return true;
    }

    bool Player::renderToLayer(iTJSDispatch2 *layerObject,
                               bool skipUpdate) {
        if(!layerObject) {
            return false;
        }

        ensureMotionLoaded();
        if(!_runtime->activeMotion) {
            return false;
        }


        tTJSNI_BaseLayer *layer = nullptr;
        {
            tjs_error nisResult = layerObject->NativeInstanceSupport(
                   TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                   reinterpret_cast<iTJSNativeInstance **>(&layer));
            if(TJS_FAILED(nisResult) || !layer) {
                // layerObject isn't a native Layer—try to find one through
                // TJS property chain (owner/_owner/targetLayer/layer)
                tTJSVariant wrapper(layerObject, layerObject);
                auto *resolved = tryResolveLayerDispatch(wrapper);
                if(resolved && resolved != layerObject) {
                    return renderToLayer(resolved);
                }

                // Last resort: try NativeInstanceSupport on layerObject directly
                // again (sometimes the first call can fail transiently)
                layer = nullptr;
                if(TJS_SUCCEEDED(layerObject->NativeInstanceSupport(
                       TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                       reinterpret_cast<iTJSNativeInstance **>(&layer))) &&
                   layer) {
                    // Fall through to rendering
                } else {
                    return false;
                }
            }
        }
        // Layer-API based rendering (no OpenCV)
        // libkrkr2.so sub_6C7440 does not gate on resourcesByPath —
        // motions can reference external image files without embedding
        // PSB resources (e.g. logo .mtn files).
        if(_runtime->activeMotion) {
            const auto *clip = selectActiveClip();
            const auto renderTime = activeClipTime(*_runtime, clip);
            // Use the target layer's own size if it's large enough (e.g.
            // motionWorkLayer at full screen resolution). Only fall back to
            // motion's native size if the layer is too small (e.g. SLA owner
            // at 64x64). libkrkr2.so renders at the target's size, not the
            // motion's intrinsic size.
            int canvasWidth = static_cast<int>(layer->GetWidth());
            int canvasHeight = static_cast<int>(layer->GetHeight());
            if(canvasWidth <= 0 || canvasHeight <= 0) {
                canvasWidth = static_cast<int>(layer->GetImageWidth());
                canvasHeight = static_cast<int>(layer->GetImageHeight());
            }
            // Fall back to motion size only if layer is very small
            if(canvasWidth < 128 || canvasHeight < 128) {
                int motionW = static_cast<int>(_runtime->activeMotion->width);
                int motionH = static_cast<int>(_runtime->activeMotion->height);
                if(motionW > canvasWidth || motionH > canvasHeight) {
                    canvasWidth = motionW;
                    canvasHeight = motionH;
                }
            }

            if(canvasWidth > 0 && canvasHeight > 0) {
                try {
                    // Ensure target layer has image buffer
                    if(!layer->GetHasImage()) {
                        layer->SetHasImage(true);
                    }
                    layer->SetImageSize(static_cast<tjs_uint>(canvasWidth),
                                        static_cast<tjs_uint>(canvasHeight));
                    // Always set display size to match canvas
                    layer->SetSize(canvasWidth, canvasHeight);
                    // libkrkr2.so Player_DrawSLA_guess sets visible=true
                    // on the resolved layer before rendering
                    if(!layer->GetVisible()) {
                        layer->SetVisible(true);
                    }

                    // Set clip rect to full canvas
                    layer->SetClip(0, 0, canvasWidth, canvasHeight);

                    // Clear with transparent black
                    tTVPRect clearRect;
                    clearRect.left = 0;
                    clearRect.top = 0;
                    clearRect.right = canvasWidth;
                    clearRect.bottom = canvasHeight;
                    layer->FillRect(clearRect, 0x00000000);

                    const auto &layerNamesList = activeLayerNames();

                    // Aligned to libkrkr2.so sub_6C2334 + Player_applyTranslateOffset_guess:
                    // 1. drawAffineMatrix (internal+808..844) transforms PSB native
                    //    coords to ownerLayer coords during render tree building
                    // 2. cameraOffset (Player+144/148) + rootOffset (Player+120/128)
                    //    are added to all vertices by applyTranslateOffset
                    // Our globalAffine combines both steps.
                    Affine2x3 globalAffine = {
                        _runtime->drawAffineMatrix[0],  // m11
                        _runtime->drawAffineMatrix[1],  // m21
                        _runtime->drawAffineMatrix[2],  // m12
                        _runtime->drawAffineMatrix[3],  // m22
                        _runtime->drawAffineMatrix[4] + _rootOffsetX + _cameraOffsetX,
                        _runtime->drawAffineMatrix[5] + _rootOffsetY + _cameraOffsetY
                    };
                    // Build persistent node tree if not yet built.
                    // Aligned to libkrkr2.so sub_6B51F0 → sub_6B4A6C.
                    if (!_runtime->nodesBuilt) {
                        std::string clipLabel;
                        if (clip) clipLabel = clip->label;
                        _runtime->nodes = detail::buildNodeTree(
                            *_runtime->activeMotion, clipLabel);
                        _runtime->nodesBuilt = true;
                        // Apply pending root position from TJS setter
                        // (player.x/y may be set before node tree exists)
                        if (_hasPendingRootPos && !_runtime->nodes.empty()) {
                            _runtime->nodes[0].accumulated.posX = _pendingRootX;
                            _runtime->nodes[0].accumulated.posY = _pendingRootY;
                        }
                        // Populate label→index map (aligned to binary's std::map at player+24)
                        _runtime->nodeLabelMap.clear();
                        for (size_t ni = 0; ni < _runtime->nodes.size(); ++ni) {
                            const auto &lbl = _runtime->nodes[ni].layerName;
                            if (!lbl.empty())
                                _runtime->nodeLabelMap.emplace(lbl, static_cast<int>(ni));
                        }
                    }

                    // Run 3-phase updateLayers pipeline on persistent nodes.
                    // Aligned to libkrkr2.so Player_updateLayers (0x6BB33C).
                    updateLayers(renderTime);

                    // Collect visible nodes into render list.
                    // Aligned to libkrkr2.so sub_6C2334 render tree building.
                    std::vector<FlatRenderNode> renderNodes;
                    const bool emo = _runtime->isEmoteMode;
                    buildRenderListFromNodes(_runtime->nodes, globalAffine,
                                            renderNodes, emo);

                    // Collect child Player render output for nodeType=3 (Motion).
                    // Aligned to sub_6BE0C0: child render merges into parent tree.
                    for (const auto &motionNode : _runtime->nodes) {
                        if (motionNode.nodeType != 3) continue;
                        auto *childP = motionNode.getChildPlayer();
                        if (!childP) continue;
                        auto &childRuntime = childP->_runtime;
                        if (!childRuntime || childRuntime->nodes.empty()) continue;
                        const auto &acc = motionNode.accumulated;
                        Affine2x3 childGlobal = {
                            globalAffine[0], globalAffine[1],
                            globalAffine[2], globalAffine[3],
                            globalAffine[0]*acc.posX + globalAffine[2]*acc.posY + globalAffine[4],
                            globalAffine[1]*acc.posX + globalAffine[3]*acc.posY + globalAffine[5]
                        };
                        buildRenderListFromNodes(childRuntime->nodes,
                                                childGlobal, renderNodes, emo);
                    }

                    // Collect particle child Player render output (nodeType=4).
                    // Aligned to sub_6BF0DC: particle children render into parent tree.
                    for (const auto &particleNode : _runtime->nodes) {
                        if (particleNode.nodeType != 4) continue;
                        for (int pci = 0; pci < particleNode.getParticleCount(); ++pci) {
                            auto *pChild = particleNode.getParticleChild(pci);
                            if (!pChild || !pChild->_runtime) continue;
                            auto &pNodes = pChild->_runtime->nodes;
                            if (pNodes.empty()) continue;
                            const auto &pacc = particleNode.accumulated;
                            Affine2x3 pGlobal = {
                                globalAffine[0], globalAffine[1],
                                globalAffine[2], globalAffine[3],
                                globalAffine[0]*pacc.posX + globalAffine[2]*pacc.posY + globalAffine[4],
                                globalAffine[1]*pacc.posX + globalAffine[3]*pacc.posY + globalAffine[5]
                            };
                            buildRenderListFromNodes(pNodes, pGlobal, renderNodes, emo);
                        }
                    }

                    // If node pipeline produced nothing, fall back to
                    // flattenLayerNodes for motion cross-references.
                    if (renderNodes.empty()) {
                        for (const auto &layerName : layerNamesList) {
                            const auto *layers = activeLayersByName();
                            if (!layers) break;
                            const auto it = layers->find(layerName);
                            if (it == layers->end()) continue;
                            flattenLayerNodes(it->second, renderTime,
                                              globalAffine, 1.0,
                                              false, false, renderNodes);
                        }
                    }

                    // Aligned to libkrkr2.so Player_calcBounds (0x6C3D04):
                    // Compute AABB from all render node affine corners.
                    _boundsMinX = 1e308;
                    _boundsMinY = 1e308;
                    _boundsMaxX = -1e308;
                    _boundsMaxY = -1e308;
                    for(const auto &node : renderNodes) {
                        // Estimate node bounds: use state.width/height or
                        // a default size. Exact source size isn't known yet.
                        const double nw = node.state.width > 0 ? node.state.width : 128.0;
                        const double nh = node.state.height > 0 ? node.state.height : 128.0;
                        const auto &a = node.affine;
                        // 4 corners: (0,0), (nw,0), (0,nh), (nw,nh)
                        double cx[4] = {a[4], a[0]*nw+a[4], a[2]*nh+a[4], a[0]*nw+a[2]*nh+a[4]};
                        double cy[4] = {a[5], a[1]*nw+a[5], a[3]*nh+a[5], a[1]*nw+a[3]*nh+a[5]};
                        for(int c = 0; c < 4; c++) {
                            if(cx[c] < _boundsMinX) _boundsMinX = cx[c];
                            if(cy[c] < _boundsMinY) _boundsMinY = cy[c];
                            if(cx[c] > _boundsMaxX) _boundsMaxX = cx[c];
                            if(cy[c] > _boundsMaxY) _boundsMaxY = cy[c];
                        }
                    }

                    // Step 2 (sub_6C7440): Flat loop — for each node,
                    // load source bitmap and call OperateAffine on target.
                    bool drewAny = false;
                    std::unordered_map<std::string, std::shared_ptr<tTVPBaseBitmap>> srcCache;
                    // Cache PSB source origin (anchor) offsets per source name.
                    // Aligned to libkrkr2.so sub_6BC4F0: originX/originY from
                    // PSB icon node define the image pivot point.
                    std::unordered_map<std::string, std::pair<double,double>> originCache;

                    for(const auto &node : renderNodes) {
                        // Resolve source bitmap (with cache)
                        std::shared_ptr<tTVPBaseBitmap> srcBmp;
                        if(auto cit = srcCache.find(node.state.src);
                           cit != srcCache.end()) {
                            srcBmp = cit->second;
                        } else {
                            // Try file source via storage system
                            const auto resolvedPath = resolveMotionSourcePath(
                                *_runtime->activeMotion, node.state.src);
                            if(!resolvedPath.IsEmpty()) {
                                ttstr loadPath = resolvedPath;
                                const auto ps = detail::narrow(resolvedPath);
                                if(ps.rfind('.') == std::string::npos ||
                                   ps.rfind('.') < ps.rfind('/')) {
                                    loadPath = resolvedPath + TJS_W(".png");
                                }
                                try {
                                    auto bmp = std::make_shared<tTVPBaseBitmap>(1, 1, 32);
                                    TVPLoadGraphic(bmp.get(), loadPath,
                                                   TVP_clNone, 0, 0,
                                                   glmNormal, nullptr, nullptr);
                                    if(bmp->GetWidth() > 0 && bmp->GetHeight() > 0)
                                        srcBmp = bmp;
                                } catch(...) {}
                            }
                            // Try PSB embedded resource
                            if(!srcBmp) {
                                int rw = 0, rh = 0;
                                double srcOriginX = 0, srcOriginY = 0;
                                std::vector<std::uint8_t> decompressed;
                                const auto *res = findPSBResourceBySourceName(
                                    *_runtime->activeMotion, node.state.src,
                                    rw, rh, decompressed,
                                    srcOriginX, srcOriginY);
                                originCache[node.state.src] = {srcOriginX, srcOriginY};
                                if(res && rw > 0 && rh > 0 && !res->data.empty()) {
                                    // Use decompressed data if RL was applied,
                                    // otherwise use raw resource data
                                    const auto &pixelData = decompressed.empty()
                                        ? res->data : decompressed;
                                    auto bmp = std::make_shared<tTVPBaseBitmap>(
                                        static_cast<tjs_uint>(rw),
                                        static_cast<tjs_uint>(rh), 32);
                                    tTVPRect fr(0, 0, rw, rh);
                                    bmp->Fill(fr, 0x00000000);
                                    // RL-decompressed data is RGBA interleaved.
                                    // KiKiRi2 internal format is BGRA. Swap R and B.
                                    const auto *sd = pixelData.data();
                                    for(int y = 0; y < rh; ++y) {
                                        auto *row = static_cast<std::uint8_t *>(
                                            bmp->GetScanLineForWrite(
                                                static_cast<tjs_uint>(y)));
                                        for(int x = 0; x < rw; ++x) {
                                            const size_t si =
                                                (static_cast<size_t>(y) * rw + x) * 4;
                                            if(si + 3 >= pixelData.size()) break;
                                            auto *dp = row + x * 4;
                                            dp[0] = sd[si + 2]; // B <- R
                                            dp[1] = sd[si + 1]; // G
                                            dp[2] = sd[si + 0]; // R <- B
                                            dp[3] = sd[si + 3]; // A
                                        }
                                    }
                                    srcBmp = bmp;
                                }
                            }
                            srcCache.emplace(node.state.src, srcBmp);
                        }

                        if(!srcBmp || srcBmp->GetWidth() == 0) {
                            continue;
                        }

                        // Aligned to sub_6C7440 at 0x6C7F54:
                        // src rect = (0, 0, bitmapWidth, bitmapHeight)
                        // dst points = pre-computed corners[0,1,3] - 0.5
                        const tjs_int srcW = static_cast<tjs_int>(srcBmp->GetWidth());
                        const tjs_int srcH = static_cast<tjs_int>(srcBmp->GetHeight());
                        tTVPRect sr(0, 0, srcW, srcH);

                        const tjs_int opa = static_cast<tjs_int>(
                            std::clamp(node.accumulatedOpacity * 255.0, 0.0, 255.0));

                        tTVPPointD pts[3];
                        if (node.hasCorners) {
                            // Use pre-computed corner vertices (aligned to binary).
                            // Binary passes corners 0, 1, 3 (not 2) to affineCopy.
                            pts[0] = {node.corners[0] - 0.5, node.corners[1] - 0.5};
                            pts[1] = {node.corners[2] - 0.5, node.corners[3] - 0.5};
                            pts[2] = {node.corners[6] - 0.5, node.corners[7] - 0.5};
                        } else {
                            // Fallback for nodes without pre-computed vertices
                            const auto &a = node.affine;
                            double srcOX = 0, srcOY = 0;
                            if(auto oit = originCache.find(node.state.src);
                               oit != originCache.end()) {
                                srcOX = oit->second.first;
                                srcOY = oit->second.second;
                            }
                            const double atx = a[4] - (a[0]*srcOX + a[2]*srcOY);
                            const double aty = a[5] - (a[1]*srcOX + a[3]*srcOY);
                            pts[0] = {atx - 0.5, aty - 0.5};
                            pts[1] = {a[0]*srcW + atx - 0.5, a[1]*srcW + aty - 0.5};
                            pts[2] = {a[2]*srcH + atx - 0.5, a[3]*srcH + aty - 0.5};
                        }

                        try {
                            layer->OperateAffine(pts, srcBmp.get(), sr,
                                                 omAlpha, opa, stNearest);
                            drewAny = true;
                        } catch(const eTJS &) {
                        } catch(...) {
                        }
                    }

                    if(drewAny) {
                        if(!skipUpdate) layer->Update(false);
                        _runtime->lastCanvas =
                            tTJSVariant(layerObject, layerObject);
                        return true;
                    }
                } catch(const std::exception &e) {
                    LOGGER->warn("Motion.Player LayerAPI render failed for {}: {}",
                                 _runtime->activeMotion->path, e.what());
                } catch(...) {
                    LOGGER->warn("Motion.Player LayerAPI render failed for {}",
                                 _runtime->activeMotion->path);
                }
            }
        }
        const auto sourcePath = resolveCaptureSourcePath();
        if(sourcePath.IsEmpty()) {
            LOGGER->warn("Motion.Player draw fallback could not resolve source "
                         "for {}",
                         _runtime->activeMotion->path);
            return false;
        }

        try {
            if(!layer->GetHasImage()) {
                layer->SetHasImage(true);
            }

            if(auto *meta = layer->LoadImages(sourcePath, TVP_clNone)) {
                meta->Release();
            }


            if(!skipUpdate) layer->Update(false);
            _runtime->lastCanvas = tTJSVariant(layerObject, layerObject);
            return true;
        } catch(...) {
            LOGGER->warn("Motion.Player draw fallback failed for {}",
                         sourcePath.AsStdString());
            return false;
        }
    }

    tTJSVariant Player::findSource(ttstr name) {
        loadSource(name);
        const auto key = detail::narrow(name);
        if(const auto it = _runtime->sourcesByKey.find(key);
           it != _runtime->sourcesByKey.end()) {
            return it->second;
        }
        return {};
    }

    void Player::loadSource(ttstr name) {
        const auto requestKey = detail::narrow(name);
        if(requestKey.empty() ||
           _runtime->sourcesByKey.find(requestKey) !=
               _runtime->sourcesByKey.end()) {
            return;
        }

        ttstr resolved;
        if(!detail::resolveExistingPath(buildSourceCandidates(*_runtime, name),
                                        resolved)) {
            return;
        }

        const auto resolvedKey = detail::narrow(resolved);
        if(const auto existing = _runtime->sourcesByKey.find(resolvedKey);
           existing != _runtime->sourcesByKey.end()) {
            _runtime->sourcesByKey.emplace(requestKey, existing->second);
            return;
        }

        const auto source = _resourceManagerNative.load(resolved);
        if(source.Type() == tvtVoid) {
            return;
        }

        _runtime->sourcesByKey.emplace(requestKey, source);
        _runtime->sourcesByKey.emplace(resolvedKey, source);
    }

    void Player::clearCache() {
        _runtime->sourcesByKey.clear();
        _runtime->lastCanvas.Clear();
    }

    void Player::setSize(tjs_int w, tjs_int h) {
        _runtime->width = w;
        _runtime->height = h;
    }

    void Player::copyRect(tTJSVariant) {}

    void Player::adjustGamma(tTJSVariant) {}

    void Player::draw() {
        if(!_runtime->visible) {
            _runtime->lastCanvas = detail::makeDictionary({
                { "visible", false },
                { "tickCount", getTickCount() },
            });
            return;
        }

        ensureMotionLoaded();

        if(_runtime->width == 0 && _runtime->activeMotion) {
            _runtime->width = static_cast<tjs_int>(_runtime->activeMotion->width);
        }
        if(_runtime->height == 0 && _runtime->activeMotion) {
            _runtime->height = static_cast<tjs_int>(_runtime->activeMotion->height);
        }

        calcViewParam();

        const auto activeLayers = activeLayerNames();
        const auto layerNames =
            detail::makeArray(detail::stringsToVariants(activeLayers));
        const auto sourceCount = static_cast<tjs_int>(_runtime->sourcesByKey.size());
        _processedMeshVerticesNum = static_cast<int>(activeLayers.size());

        std::vector<std::pair<std::string, tTJSVariant>> entries = {
            { "width", _runtime->width },
            { "height", _runtime->height },
            { "visible", _runtime->visible },
            { "opacity", _runtime->opacity },
            { "flip", _runtime->flip },
            { "slant", _runtime->slant },
            { "zoom", _runtime->zoom },
            { "clearColor", _runtime->clearColor },
            { "tickCount", getTickCount() },
            { "frameTickCount", _frameTickCount },
            { "backgroundCount",
              static_cast<tjs_int>(_runtime->backgrounds.size()) },
            { "captionCount", static_cast<tjs_int>(_runtime->captions.size()) },
            { "sourceCount", sourceCount },
            { "layers", layerNames },
        };

        if(_runtime->activeMotion) {
            entries.emplace_back("motionPath",
                                 detail::widen(_runtime->activeMotion->path));
            entries.emplace_back("layerCount",
                                 static_cast<tjs_int>(activeLayers.size()));
        }

        _runtime->lastCanvas = detail::makeDictionary(entries);
    }

    void Player::frameProgress(double dt) {
        // Aligned to libkrkr2.so Player_progress_inner (0x6C106C):
        // _speed is a bool flag (play/pause). When false, skip progress entirely.
        if(!_speed) {
            return;
        }
        const double actualDelta = dt;
        _frameLastTime = actualDelta;
        _frameLoopTime += actualDelta;
        _loopTime += actualDelta;
        _frameTickCount += actualDelta;

        // Camera velocity/friction moved to updateLayers pre-loop (0x6BB360..0x6BB42C)

        // Save prevTime per timeline for action scanning
        std::unordered_map<std::string, double> prevTimes;
        for(const auto &[name, state] : _runtime->timelines) {
            prevTimes[name] = state.currentTime;
        }

        detail::stepTimelines(_runtime->timelines, actualDelta,
                              &_runtime->pendingEvents);

        // Set clamped eval time (player+456) from main timeline's currentTime.
        // Binary writes this during timeline evaluation; sub_6C1540 reads it for ratio.
        if (!_runtime->timelines.empty()) {
            _clampedEvalTime = _runtime->timelines.begin()->second.currentTime;
        }

        // Scan PSB layers for action/sync events crossed this frame
        // Aligned to libkrkr2.so: updateLayers queues events during evaluation
        if(_runtime->activeMotion && actualDelta > 0) {
            for(const auto &[name, state] : _runtime->timelines) {
                double prev = 0.0;
                if(auto it = prevTimes.find(name); it != prevTimes.end())
                    prev = it->second;
                if(state.currentTime > prev) {
                    detail::scanLayerActions(*_runtime->activeMotion,
                                            prev, state.currentTime,
                                            _runtime->pendingEvents);
                }
            }
        }

        _allplaying = std::any_of(
            _runtime->timelines.begin(), _runtime->timelines.end(),
            [](const auto &entry) { return entry.second.playing; });
        _syncActive = _syncWaiting && _allplaying;
    }


} // namespace motion
