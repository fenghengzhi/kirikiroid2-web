//
// Created by LiDon on 2025/9/15.
//
#pragma once
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "tjs.h"
#include "internal/ttstr_hash.h"
#include "SourceCache.h"

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
    //  (1) C-1 RE-CORRECTED 2026-06-07: ResourceManager : public SourceCache.
    //      The prior 06-04 note ("two unrelated classes that merely SHARE the 3
    //      callback addresses = method-sharing, not class-identity") was WRONG and
    //      is replaced. Fresh decompile proves C++ public inheritance:
    //        * RM ctor sub_6A88CC @0x6A88CC FIRST instruction (0x6a88f8) calls the
    //          SourceCache ctor sub_6A78F4 on the SAME `a1` at offset 0 (base
    //          subobject), which initialises +20 primaryLayer / +40 bufLayer
    //          (Layer variant) / +64 layerType / +72/+80 intrusive list head-tail
    //          sentinel; RM ctor THEN initialises its own fields from +88 onward
    //          (HashMap A, RandomGenerator@+144, layerId set@+168,
    //          counter@+216, spec@+224).
    //        * sub_6A78F4 (SourceCache ctor) is called from EXACTLY ONE site —
    //          0x6a88f8 inside the RM ctor (xrefs_to confirmed). There is NO
    //          standalone SourceCache instance anywhere in the binary; SourceCache
    //          exists only as the [0,88) base subobject of ResourceManager.
    //        * The 3 callbacks RM registrar @0x6AB8BC re-lists (loadSource
    //          sub_6A7BA8, clearCache sub_6A8438, bufLayer sub_6A84FC) are the
    //          EXACT SAME function addresses SourceCache registrar @0x6A85A8
    //          registers — i.e. inherited base members re-listed on the derived
    //          class's NCB table, the C++ inheritance signature (not coincidental
    //          method-sharing). bufLayer getter @0x6A84FC reads `a1+40`, the
    //          SourceCache base Layer variant.
    //      So `RM : public SourceCache` makes RM's loadSource/clearCache/bufLayer
    //      run on the inherited SourceCache base state (+72 layer-list etc).
    //      Player+656 is NOT a separate SourceCache: Player ctor @0x6CED30 copies
    //      the SAME RM dispatch into +636/+656/+992 (sub_A0F5E0 each) — three
    //      refs to one ResourceManager-which-IS-A-SourceCache. Restored in the
    //      port: Player::_sourceCacheNative aliases nativeRM(), while the Web-only
    //      native render helpers receive the current Player as a call argument
    //      instead of storing a persistent Player back-pointer in the shared RM.
    //      (Corrected per CLAUDE.md 证伪即就地纠正.)
    //  (2) ObjSource (SourceCache.h:116) is NOT a fields struct in the binary —
    //      ncb_registerMembers @0x69CCB8 builds a `operator new(0x18)` dict
    //      facade (qword[0] = tTJSVariant holding the PSB "source" dict) whose
    //      originX/originY/width/height/clip/drawLayer getters all read
    //      dict[key]. The port's _key/_src/_blendMode/_color fields are an
    //      invention. (MASTER's "ObjSource missing 6 members" is inverted.)
    // ------------------------------------------------------------------

    // C-1 (2026-06-07): the binary's RM +72/+80 intrusive layer-list is the
    // SourceCache BASE subobject's list (SourceCache::_entries, ctor sub_6A78F4
    // head/tail sentinel @+72/+80). It is NOT an RM-own container — the former
    // RM-local `SourceCacheEntry` + `_sourceCacheList` placeholder was the
    // pre-inheritance two-class split and is removed; the inherited
    // SourceCache::_entries (SourceCache.h) serves it.

    // C-1 (2026-06-07): RM derives from SourceCache (binary
    // `class ResourceManager : public SourceCache`, ctor sub_6A88CC @0x6A88CC
    // calls the SourceCache base ctor sub_6A78F4 @0x6A78F4 at offset 0; NCB
    // registrar @0x6AB8BC re-lists the SourceCache base members loadSource /
    // clearCache / bufLayer with the SAME callback addresses sub_6A7BA8 /
    // sub_6A8438 / sub_6A84FC that SourceCache registrar @0x6A85A8 binds).
    // The inherited loadSource(tTJSVariant,tTJSVariant) / clearCache() /
    // getBufLayer() now serve the RM NCB bindings, so the former RM-own
    // overrides (loadSource(ttstr)->load forward, clearCache()->_state clear,
    // getBufLayer()->ttstr) are removed — they were the pre-inheritance
    // two-class artifacts.
    class ResourceManager : public SourceCache {
    public:
        ResourceManager();

        explicit ResourceManager(iTJSDispatch2 *kag, tjs_int cacheSize);

        tTJSVariant load(ttstr path) const;
        void unload(ttstr path) const;
        tTJSVariant getLastLoadedModule() const;
        tTJSVariant findLoaded(ttstr path) const;
        tTJSVariant findSource(ttstr moduleKey, ttstr path) const;
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
        // where _state maps cleanly (unloadAll); STUB (cite addr) where the real
        // bodies walk HashMap A both through bucket lookup and through its
        // libstdc++ global node chain at +104.
        // C-1: bufLayer (prop-ro @0x6A84FC = base +40 Layer variant) is now the
        // INHERITED SourceCache::getBufLayer() — the RM-own ttstr getBufLayer()
        // was the pre-inheritance two-class artifact and is removed.
        void unloadAll() const;                         // @0x6A8CF8
        [[nodiscard]] bool isExistMotion(ttstr projectKey,
                                         ttstr path) const;     // @0x6A96F8
        [[nodiscard]] tTJSVariant findMotion(ttstr projectKey,
                                              ttstr path) const; // @0x6A9ED4
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

        // C-1 (2026-06-07): the binary RM +40 bufLayer (Layer variant) and the
        // +72/+80 intrusive layer-list both belong to the SourceCache BASE
        // subobject — now INHERITED (SourceCache::_bufLayer / SourceCache::_entries,
        // SourceCache.h). The former RM-own `ttstr _bufLayer` + `std::list<
        // SourceCacheEntry> _sourceCacheList` placeholders were the
        // pre-inheritance two-class split and are removed.

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

        // CORRECTION (2026-07-12): +104 is NOT a second container. It is
        // HashMap A's libstdc++ `_M_before_begin._M_nxt` field: the global
        // singly-linked node chain inside the same std::unordered_map that
        // starts at +88 (buckets@+88, bucketCount@+96, head@+104,
        // elementCount@+112). isExistMotion/findMotion first do a bucket lookup
        // and then walk this same map's node chain as their fallback.

        // +224 int32 spec flag — binary checks 1 (krkr) vs 2 (win) in
        // loadResource path. Web port runs the krkr branch.
        tjs_int _spec = 1;
    };
} // namespace motion
