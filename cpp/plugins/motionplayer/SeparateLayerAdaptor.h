//
// Created by LiDon on 2025/9/15.
//
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <vector>

#include "MeshPoint.h"
#include "tjs.h"

namespace motion {

    struct SeparateLayerPayload_guess {
        tTJSVariant layerVariant;
        tjs_int completionType = 0;
        bool hasOutlineOrMeshline = false;
        ttstr commandSrc;
        tjs_int blendMode = 0;
        std::array<tjs_uint32, 4> packedColors{};
        std::array<float, 8> paintAndViewport{};
        std::vector<detail::MeshPoint> compositeMeshPoints;
        std::vector<detail::MeshPoint> bezierPatchPoints;
        std::array<float, 8> corners{};

        bool requiresRefresh_guess(
            const SeparateLayerPayload_guess &other) const;
    };

    class SeparateLayerOrderedMap_guess {
    public:
        // All four native trees store the ordinal only in the pair key.  The
        // mapped object starts immediately after that key/alignment slot; no
        // second ordinal precedes the payload.
        using Map = std::map<tjs_uint32, SeparateLayerPayload_guess>;
        using iterator = Map::iterator;
        using const_iterator = Map::const_iterator;

        SeparateLayerOrderedMap_guess() = default;
        ~SeparateLayerOrderedMap_guess();

        SeparateLayerOrderedMap_guess(
            const SeparateLayerOrderedMap_guess &) =
            delete;
        SeparateLayerOrderedMap_guess &
        operator=(const SeparateLayerOrderedMap_guess &) = delete;

        iterator begin() { return _nodes.begin(); }
        iterator end() { return _nodes.end(); }
        const_iterator begin() const { return _nodes.begin(); }
        const_iterator end() const { return _nodes.end(); }
        bool empty() const { return _nodes.empty(); }

        SeparateLayerPayload_guess &ensure(tjs_uint32 ordinal);
        iterator find(tjs_uint32 ordinal) { return _nodes.find(ordinal); }
        const_iterator find(tjs_uint32 ordinal) const {
            return _nodes.find(ordinal);
        }
        void erase(iterator it);
        void clear(bool invalidateObjects);
        void swapWith(SeparateLayerOrderedMap_guess &other);

    private:
        Map _nodes;
    };

    class SeparateLayerAdaptor {
    public:
        explicit SeparateLayerAdaptor(tTJSVariant targetLayer);
        ~SeparateLayerAdaptor();

        // The four native callbacks copy arg0 when present, otherwise construct
        // from a Void Variant, and ignore later arguments.  ncbind invokes this
        // behind a pre-created non-sticky shell adaptor; its generated bridge
        // owns rollback (native dtor + delete) when class-ID lookup/attach fails.
        // No reference producer sets this class's adaptor sticky flag true.
        static tjs_error factory(SeparateLayerAdaptor **result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *) {
            tTJSVariant targetLayer;
            if(numparams >= 1) {
                targetLayer = *param[0];
            }
            *result = new SeparateLayerAdaptor(targetLayer);
            return TJS_S_OK;
        }

        iTJSDispatch2 *getOwner() const {
            return _owner.Type() == tvtObject ? _owner.AsObjectNoAddRef() : nullptr;
        }

        const tTJSVariant &getOwnerVariant() const {
            return _owner;
        }

        // Public property pair shared by all four NCB registrars.
        tjs_int getAbsolute() const { return _absolute; }
        void setAbsolute(tjs_int v) { _absolute = v; }
        tTJSVariant getTargetLayer() const { return _targetLayer; }
        void setTargetLayer(tTJSVariant v) { _targetLayer = v; }

        iTJSDispatch2 *getPrivateRenderTargetObject() const;
        void clear();
        void beginLayerPass_guess();
        tTJSVariant resolveLayerNode_guess(
            tjs_uint32 ordinal,
            const SeparateLayerPayload_guess &sourcePayload,
            bool &createdOrChanged);
        // Accurate rendering uses this payload-free overload only for its
        // optional intermediate masked Layer; the base item Layer goes through
        // resolveLayerNode_guess with the complete source payload. The sticky
        // shared-D3D draw route also uses this overload. It moves only the
        // retained Layer Variant between the same active/retired trees and
        // deliberately does not advance the per-pass sequence after publishing
        // absolute.
        tTJSVariant resolveLayerOrdinal_guess(tjs_uint32 ordinal);
        void endLayerPass_guess();
        static tjs_error assignCompat(tTJSVariant *result, tjs_int numparams,
                                      tTJSVariant **param,
                                      iTJSDispatch2 *objthis);
    private:
        friend iTJSDispatch2 *ensurePrivateMotionGLL_guess(
            SeparateLayerAdaptor &sla);

        void clearPrivateRenderState();
        tjs_error assignFromAdaptor_guess(const SeparateLayerAdaptor &source);
        tTJSVariant resolveLayerNodeInternal_guess(
            tjs_uint32 ordinal,
            const SeparateLayerPayload_guess &sourcePayload,
            bool &createdOrChanged);

        // Native declaration order is three owning Variants, the active and
        // retired ordered maps, then the absolute base and per-pass sequence.
        // Exact ABI offsets differ between the 64-bit and 32-bit references.
        tTJSVariant _owner;
        tTJSVariant _targetLayer;
        tTJSVariant _privateTarget;
        SeparateLayerOrderedMap_guess _activeLayers_guess;
        SeparateLayerOrderedMap_guess _retiredLayers_guess;
        tjs_int _absolute = 0;
        tjs_int _assignSequence;
    };
} // namespace motion
