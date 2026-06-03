//
// Created by LiDon on 2025/9/15.
//
#pragma once
#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "tjs.h"
#include "internal/ttstr_hash.h"

namespace motion {

    // ------------------------------------------------------------------
    // R-M9 Phase 1 scaffolding (M9 spike 2026-05-31; architecture confirmed
    // 2026-06-03 by full findSource-chain decompile, cluster K):
    //
    // PLATFORM_BOUNDARY (phase D parked) — CORRECTED 2026-06-03 (fresh decompile
    // of the draw path 0x6C7440 + vertex builder sub_6C715C):
    //
    // The earlier justification claimed the binary "recombines the 4-corner color
    // gradient as real PER-VERTEX vertex colors in its GPU draw." That specific
    // claim is NOT supported by the decompiled draw path: the vertex builder
    // sub_6C715C @0x6C715C appends ONLY (x,y) position pairs (tTJSVariant type 5,
    // 20B stride) into the geometry array; the operate*/copy primitives in
    // sub_6C7440 (operateRect/operateMesh/operateBezierPatch/affineCopy/meshCopy/
    // bezierPatchCopy) each take a single blend-op mode and a single source Layer,
    // with NO per-vertex color array, and the anchor-damped 4-corner colorBytes
    // (node+100..115) are not referenced anywhere in 0x6C7440. Where the 4-corner
    // gradient is actually consumed (if at all) is NOT yet located — so the
    // per-vertex-color rationale must NOT be cited as settled.
    //
    // What still holds for the boundary: the local render stack exposes color only
    // as a single scalar RGBA (SetParameterColor4B), not per-vertex, so the port's
    // CPU-bake of the gradient into the bitmap with a (name, blendMode,
    // packedColors) cache key remains a defensible adaptation. But the boundary
    // must be justified by the COLOR CONSUMER in the draw path (address TBD), not
    // by findSource (a pure name->single-texture cache that applies no color) and
    // not by the unverified per-vertex-color claim above. Phase D (texture
    // topology + RM/SourceCache class merge) stays parked pending that consumer
    // decompile; brick A (this scaffolding) is M9's faithful endpoint on the port
    // side.
    // Binary libkrkr2.so ResourceManager (~256B, NCB registered at 0x6AB8BC)
    // exposes 14 TJS members and holds 3 internal containers + bufLayer +
    // spec int. Phase 1 declares the binary-aligned C++ fields so phase 2
    // (atomic findSource topology refactor) is a pure data migration rather
    // than type discovery. None of the new fields are wired into the
    // load/findSource paths yet — port's existing State<shared_ptr> backing
    // store stays authoritative for behavior. Logo differential green is
    // preserved by construction (new fields default-initialized empty).
    //
    // TWO ARCHITECTURE FACTS the binary makes the eventual target (phase D):
    //  (1) There is NO separate SourceCache class. ResourceManager *is* the
    //      SourceCache: the single ncb_registerMembers @0x6AB8BC registers all
    //      14 members on one ~256B object — both the "RM" members (load/unload/
    //      unloadAll/findSource/findMotion/isExistMotion/random) and the
    //      "SourceCache" members (loadSource/clearCache/bufLayer). unloadAll
    //      @0x6A8B94 touches +72/+88/+104/+144/+168 in one body. The port's
    //      separate `SourceCache` class (SourceCache.h) is an invention; phase D
    //      merges it back into this one class.
    //  (2) ObjSource (SourceCache.h:116) is NOT a fields struct in the binary —
    //      ncb_registerMembers @0x69CCB8 builds a `operator new(0x18)` dict
    //      facade (qword[0] = tTJSVariant holding the PSB "source" dict) whose
    //      originX/originY/width/height/clip/drawLayer getters all read
    //      dict[key]. The port's _key/_src/_blendMode/_color fields are an
    //      invention. (MASTER's "ObjSource missing 6 members" is inverted.)
    // ------------------------------------------------------------------

    // Intrusive list3 entry — binary RM @+72/+80 keeps a doubly-linked
    // list of (PSB-path, target Layer name, color tag, Layer*, blendMode,
    // color[4]) tuples cached by loadSource / clearCache (`bufLayer` is
    // returned from the head entry). Per M9 spike Q1, binary node sizeof
    // is ~96B and field types are inferred from the loadSource/clearCache
    // pseudocode. Phase 1 uses std::list as placeholder — empty in phase 1,
    // so the heap-vs-inline node difference vs binary's intrusive list is
    // a zero-cost difference until phase 2 wires it.
    struct SourceCacheEntry {
        ttstr key;                    // node+16
        ttstr value;                  // node+36
        tjs_int32 colorTag = 0;       // node+52
        iTJSDispatch2 *layer = nullptr; // node+56 (owning Layer dispatch)
        tjs_int32 blendMode = 0;      // node+68
        tjs_int32 colorRGBA[4] = {0, 0, 0, 0}; // node+72..+84
    };

    class ResourceManager {
    public:
        ResourceManager();

        explicit ResourceManager(iTJSDispatch2 *kag, tjs_int cacheSize);

        tTJSVariant load(ttstr path) const;
        tTJSVariant loadSource(ttstr path) const;
        void unload(ttstr path) const;
        void clearCache() const;
        tTJSVariant getLastLoadedModule() const;
        tTJSVariant findLoaded(ttstr path) const;
        tTJSVariant findSource(ttstr path) const;
        tjs_int requireLayerId();
        tjs_int requireLayerIdForName(ttstr name);
        void releaseLayerId(tjs_int id);

        // M9 brick B: binary ResourceManager NCB members (ncb_registerMembers
        // @0x6AB8BC, 12 members total) missing from the port surface. Faithful
        // where _state maps cleanly (bufLayer/unloadAll); STUB (cite addr) where
        // the real body needs the HashMap A (+88) / motion-list (+104) walks that
        // are parked behind the phase-D texture-topology platform boundary.
        [[nodiscard]] ttstr getBufLayer() const;        // prop-ro; binary +40 @0x6A84FC
        void unloadAll() const;                         // @0x6A8BBC
        [[nodiscard]] bool isExistMotion(ttstr name) const;     // @0x6A96F8 (STUB)
        [[nodiscard]] tTJSVariant findMotion(ttstr name) const; // @0x6A9ED4 (STUB)
        static tjs_error random(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                iTJSDispatch2 *obj);            // @0x6AB56C (STUB)

        [[nodiscard]] static tjs_int getEmotePSBDecryptSeed();

        static tjs_error setEmotePSBDecryptSeed(tTJSVariant *r, tjs_int count,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

        static tjs_error setEmotePSBDecryptFunc(tTJSVariant *r, tjs_int n,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

    private:
        struct State {
            std::unordered_map<std::string, tTJSVariant> loadedModules;
            std::string lastLoadedPath;
            tTJSVariant lastLoadedModule;
            std::unordered_map<std::string, tjs_int> layerIdsByName;
            std::unordered_map<tjs_int, std::string> layerNamesById;
            std::unordered_set<tjs_int> usedLayerIds;
            tjs_int nextLayerId = 1;
        };

        std::shared_ptr<State> _state;
        inline static int _decryptSeed;

        // --- R-M9 Phase 1 binary-aligned scaffolding (empty in phase 1) ---

        // +40 ttstr — NCB property `bufLayer` returns this. Per M9 spike Q1,
        // populated by loadSource() to expose the most recent cached entry's
        // target Layer name. Phase 2 wires loadSource() to update it.
        ttstr _bufLayer;

        // +72/+80 intrusive doubly-linked list3 — loadSource / clearCache
        // / bufLayer facade. Phase 1 placeholder: std::list (heap-allocated
        // nodes vs binary's inline 96B intrusive nodes). Empty in phase 1.
        // Phase 2 (or 3) may switch to a true intrusive list if needed to
        // satisfy a sizeof-aligned PLATFORM_BOUNDARY contract.
        std::list<SourceCacheEntry> _sourceCacheList;

        // +88/+96 "HashMap A" — binary is NOT libstdc++ std::unordered_map but
        // a KiriKiri inline bucket hashmap: +88 = bucket-array ptr, +96 =
        // bucket count, bucket = FNV-variant-hash(key) % count (selection via
        // sub_6EB8F4; node walk Motion_ttstrHashMap_findNode @0x6E2060). key =
        // group/path ttstr, value = PSB-group dict dispatch. RM load/unload/
        // unloadAll/findSource/findMotion/isExistMotion read/write it; clearCache
        // does NOT touch it (layer-list only). The std::unordered_map here is the
        // port's STL stand-in (container-implementation divergence, ❌ systemic);
        // phase D replaces it with the inline FNV bucket map. Owning pointers
        // (Release each value on erase). Empty in phase 1 — `_state->loadedModules`
        // stays authoritative until phase D migrates data + ownership.
        std::unordered_map<ttstr, iTJSDispatch2 *, detail::ttstr_hash,
                           detail::ttstr_equal>
            _psbDictCache;

        // +104 second container — singly-linked node list, confirmed layout
        // node[0]=next, node[1]=key ttstr, node[2]=PSB dict (`for (i =
        // *(a1+104); i; i = *i)`). RM findMotion @0x6A9ED4 / isExistMotion
        // @0x6A96F8 walk it as the motion-cache fallback after HashMap A misses.
        // Phase 1 placeholder: std::list. Empty.
        std::list<iTJSDispatch2 *> _motionCacheList;

        // +224 int32 spec flag — binary checks 1 (krkr) vs 2 (win) in
        // loadResource path. Web port runs the krkr branch.
        tjs_int _spec = 1;
    };
} // namespace motion
