//
// Created by LiDon on 2025/9/15.
//
#pragma once
#include <list>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "tjs.h"
#include "internal/ttstr_hash.h"

namespace motion {

    // ------------------------------------------------------------------
    // R-M9 Phase 1 scaffolding (M9 spike 2026-05-31; architecture confirmed
    // 2026-06-03 by full findSource-chain decompile, cluster K):
    //
    // PLATFORM_BOUNDARY (phase D parked) — color-consumer LOCATED 2026-06-03
    // (fresh decompile of the full draw->color chain):
    //
    // The 4-corner color consumer is now found — it is NOT per-vertex vertex
    // color. The chain is:
    //   1. Anchor 0x6C0528 damps + writes the 4 corner RGBA quads to
    //      node+100/104/108/112.
    //   2. 0x6C7440 @0x6c7944 (and identically 0x6C4E28 @0x6c5528) writes those
    //      4 colors as index-properties 0..3 onto the source-resolver object
    //      player+716 (via vtbl+56). They are NOT passed to any draw primitive.
    //   3. Source resolver 0x6C1B70 reads them back into v41[0..3] and calls
    //      sub_6A7518(v41, bitmap, &dstRect, (blendMode&0xF0)==16).
    //   4. sub_6A7518 = per-PIXEL 4-corner bilinear gradient MULTIPLY baked into
    //      the source bitmap (divisor 128 if default-blend (a4&1) else 255); a
    //      hasGPUAccel branch does the same bake on the locked GPU texture.
    // After the bake the texture is drawn with positions + single blendMode +
    // single opacity only (the vertex builder sub_6C715C appends only (x,y)
    // pairs, tTJSVariant type 5, 20B stride; 0x6C7440/0x6C4E28 carry NO
    // color/opacity/rgba scalar to any operate*/copy primitive).
    //
    // The port is FAITHFUL to this mechanism, NOT a parked deviation: the local
    // render stack exposes color only as a single scalar RGBA (no per-vertex),
    // and SourceCache::applyPackedCornerTintLike_0x6A7518 (SourceCache.cpp:82)
    // reproduces the per-pixel bilinear bake incl. the 128/255 divisor, keyed by
    // (name, blendMode, packedColors[4]) (SourceCache.cpp:489 == 0x6C1B70). So
    // the earlier "per-vertex vertex colors" justification was WRONG in
    // mechanism (corrected), but the single-scalar-RGBA platform boundary itself
    // is genuine and is justified by this located consumer (sub_6A7518 per-pixel
    // bake), not by findSource. What remains parked under phase D is only the
    // texture-topology + RM/SourceCache class merge, not the color path.
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
    //  (1) CORRECTED 2026-06-04 (was: "there is NO separate SourceCache class").
    //      The binary DOES register a separate `SourceCache` NCB class. Evidence:
    //      Motion_SourceCache_ncb_register @0x6FE124 builds its own NCB class
    //      object (sub_6FE288: operator new(0xB0) + ncb_classInit with class name
    //      ttstr, registers a "finalize" member) and then calls
    //      SourceCache_ncb_registerMembers @0x6A85A8, which registers exactly 3
    //      members on it: loadSource (sub_6A7BA8), clearCache (sub_6A8438),
    //      bufLayer property (sub_6A84FC). ResourceManager is a *separate* class:
    //      ResourceManager_ncb_registerMembers @0x6AB8BC registers its 12 members
    //      on a different object. Both class registrations are invoked from
    //      Motion_namespace_ncb_register @0x6D9B08. SourceCache and RM merely
    //      SHARE the same callback addresses for the 3 overlapping members
    //      (loadSource/clearCache/bufLayer operate on the same +72 intrusive
    //      list) — that is method-sharing, not class-identity. Therefore the
    //      port's two-class split (SourceCache.h 3 members + ResourceManager 12
    //      members) is ARCHITECTURALLY CORRECT, not an invention. There is no
    //      phase-D "merge back into one class" to do. (The prior 06-03 memory
    //      m9_source_subsystem_verdict.md "RM==SourceCache same class" was wrong,
    //      direction-reversed; corrected per CLAUDE.md 证伪即就地纠正.)
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
        // P3-B (2026-06-05): binary RM exposes ONLY requireLayerId (no-arg) and
        //   releaseLayerId(id) — NCB registrar @0x6AB8BC, bodies sub_6AB694 /
        //   sub_6AB750. The string "requireLayerIdForName" has ZERO hits in the
        //   entire libkrkr2.so (cross-verified full-binary scan); it was a local
        //   invention. requireLayerId@0x6AB694 takes NO name (all 3 call sites —
        //   buildNodeTree@0x6B4A6C / emitRenderItem@0x6C4E28 /
        //   RenderMotionFrame@0x6DE738 — call it numparams=0). Removed the
        //   by-name variant + the name<->id maps below.
        tjs_int requireLayerId();
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
            // P3-A: HashMap A container-selection alignment with libkrkr2.so
            // ResourceManager (RM ctor sub_6A88CC @0x6A88CC: this+88 =
            // operator new(8 * _M_next_bkt(10)), this+96 = bucketCount — a
            // libstdc++ std::unordered_map<ttstr, V> with the KiriKiri FNV
            // hash functor). findOrInsert sub_6EB9E4 @0x6EB9E4 keys by the
            // RAW PATH ttstr (no case-fold: the FNV at 0x6eba2c-0x6eba78 hashes
            // raw UTF-16 code units; the node compare sub_9B1ED0 @0x9B1ED0 is a
            // case-SENSITIVE ordinal wcscmp). So the key is ttstr + the
            // ttstr_hash/ttstr_equal functors already used by the four Player
            // HashMaps (internal/ttstr_hash.h), NOT std::string/lowercase.
            std::unordered_map<ttstr, tTJSVariant, detail::ttstr_hash,
                               detail::ttstr_equal>
                loadedModules;
            ttstr lastLoadedPath;
            tTJSVariant lastLoadedModule;
            // P3-B (2026-06-05): binary RM layer-id allocator (ctor sub_6A88CC)
            //   = std::set<unsigned int> @+168 (std::_Rb_tree<unsigned int,
            //   _Identity, std::less> — type signature literal in
            //   _M_insert_unique/_M_erase_aux callers 0x6AB694/0x6AB750) + a
            //   next-id counter @+216. NO name<->id maps exist in the binary
            //   (the by-name machinery was a local invention; see header note).
            //   Container selection aligned: std::set (ordered RB-tree), not
            //   std::unordered_set.
            std::set<tjs_int> usedLayerIds;
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

        // +88/+96 "HashMap A" — P3-A CORRECTION 2026-06-05 (prior note FALSIFIED):
        // fresh decompile of the RM ctor sub_6A88CC @0x6A88CC proves +88/+96 IS a
        // libstdc++ std::unordered_map, NOT a "KiriKiri inline bucket hashmap":
        //   * a1+96 = _M_next_bkt(10)  (sub_149EDF8, libstdc++ bucket-count helper)
        //   * a1+88 = operator new(8 * bucketCount)  (the bucket-array of node-chain
        //     head pointers; ==&single-bucket when count==1)
        //   * lookup sub_6EB8F4 = _M_find_before_node walk: bucket = hash % a1[1],
        //     node[0]=next, node[1]=key ttstr, node+136=cached hash, node+16=value
        //   * findOrInsert sub_6EB9E4 @0x6EB9E4 returns node+16; hash functor is the
        //     KiriKiri FNV (0x6eba2c-0x6eba78, cached at key+68), equal functor is
        //     the case-SENSITIVE ordinal wcscmp sub_9B1ED0 @0x9B1ED0.
        // The container-selection-aligned form is therefore exactly
        //   std::unordered_map<ttstr, V, ttstr_hash, ttstr_equal>
        // which is precisely `State::loadedModules` above (migrated to ttstr key in
        // P3-A). So HashMap A is NOT a separate parked field — loadedModules IS it.
        // The earlier `_psbDictCache` scaffolding (an empty stand-in awaiting a
        // "phase-D inline bucket map") was based on the FALSIFIED "not std container"
        // premise and is removed; no consumer ever read it.

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
