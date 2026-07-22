//
// Created by LiDon on 2025/9/15.
//
#pragma once
#include <array>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "tjs.h"
#include "internal/ttstr_hash.h"
#include "psbfile/PSBRawFile.h"
#include "SourceCache.h"

class iTVPTexture2D;

namespace motion {

    namespace detail {
        // Win source-cache mapped value in Player_findSource @0x6948E8.
        // Assignment retains the texture and destruction releases it, matching
        // 0x694F7C..0x694FBC and the nested-map destructor @0x6DB4FC.
        struct WinSourceTextureEntry {
            WinSourceTextureEntry() = default;
            ~WinSourceTextureEntry();
            WinSourceTextureEntry(const WinSourceTextureEntry &) = delete;
            WinSourceTextureEntry &operator=(const WinSourceTextureEntry &) = delete;
            WinSourceTextureEntry(WinSourceTextureEntry &&other) noexcept;
            WinSourceTextureEntry &operator=(WinSourceTextureEntry &&other) noexcept;

            void setTexture(iTVPTexture2D *value);

            iTVPTexture2D *texture = nullptr;
        };

        // KRKR source descriptor stored in the second nested map constructed
        // by sub_6EBCFC @0x6EBCFC and populated by sub_695DE8 @0x695DE8.
        struct PackedSourceAtlasEntry {
            PackedSourceAtlasEntry() = default;
            ~PackedSourceAtlasEntry();
            PackedSourceAtlasEntry(const PackedSourceAtlasEntry &) = delete;
            PackedSourceAtlasEntry &operator=(const PackedSourceAtlasEntry &) = delete;
            PackedSourceAtlasEntry(PackedSourceAtlasEntry &&other) noexcept;
            PackedSourceAtlasEntry &operator=(PackedSourceAtlasEntry &&other) noexcept;

            void setTexture(iTVPTexture2D *value);

            iTVPTexture2D *texture = nullptr;
            int originX = 0;
            int originY = 0;
            std::array<int, 4> textureRect{0, 0, 0, 0};
            std::array<double, 4> clip{0.0, 0.0, 1.0, 1.0};
        };

        // Mapped value of ResourceManager's outer ttstr unordered_map.
        // sub_6EBCFC @0x6EBCFC constructs exactly PSB root + Win map + KRKR
        // map. Declaration order deliberately makes ordinary C++ destruction
        // run KRKR map -> Win map -> PSBFile, as sub_6DB3E8 @0x6DB3E8 does.
        struct LoadedResourceRecord {
            LoadedResourceRecord();

            PSB::PSBFile file;
            std::unordered_map<ttstr, WinSourceTextureEntry,
                               ttstr_hash, ttstr_equal> winSourceTextures;
            std::unordered_map<ttstr, PackedSourceAtlasEntry,
                               ttstr_hash, ttstr_equal> krkrSourceEntries;
        };
    } // namespace detail

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
    // bake), not by findSource. The texture-cache topology and lifetime are now
    // restored on each LoadedResourceRecord; both Win/spec=2 and KRKR/spec=1
    // now read the record's raw PSBRawNode graph. The former decoded
    // MotionSnapshot helper was removed with the compatibility subsystem.
    // Binary libkrkr2.so ResourceManager (0xE8 bytes, NCB registered at 0x6AB8BC)
    // exposes 12 TJS members and holds 3 internal containers + bufLayer +
    // spec int. CORRECTION 2026-07-18: the earlier phase-1 note that these
    // fields were not wired is now false. The inline _loadedModules member is
    // HashMap A whose mapped record owns the raw PSBFile plus the two nested
    // source maps constructed by sub_6EBCFC; load/unload/unloadAll
    // and the isExistMotion/findMotion walks use it. The former eager
    // MotionSnapshot registries and local _lastLoadedPath/_lastLoadedModule
    // fields were disproved and removed; Player now owns only the raw +528
    // content and +1012 matched-key variants. The map's key/value, owner
    // operations, and inline lifetime are no longer the gap.
    //
    // TWO ARCHITECTURE FACTS the binary makes the eventual target (phase D):
    //  (1) C-1 RE-CORRECTED 2026-06-07: ResourceManager : public SourceCache.
    //      The prior 06-04 note ("two unrelated classes that merely SHARE the 3
    //      callback addresses = method-sharing, not class-identity") was WRONG and
    //      is replaced. Fresh decompile proves C++ public inheritance:
    //        * RM ctor sub_6A88CC @0x6A88CC first invokes SourceCache ctor
    //          sub_6A78F4 on the base subobject. That constructor retains the
    //          owner, creates primary/buffer Layers, stores the byte budget and
    //          initializes the intrusive cache list; only then does RM initialize
    //          its module map, RandomGenerator, layer-id set/counter and spec.
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
    //  (2) ObjSource is a 0x18-byte raw-node facade in the Android ABI:
    //      qword[0..1] are the retained PSBRawOwner/node pair and qword[2] is
    //      the lazy texture. Its members call sub_598C58/sub_599554 directly;
    //      no tTJSVariant dispatch owner exists inside ObjSource.
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
    // getBufLayer() serve the RM NCB bindings, so the former RM-own
    // overrides (loadSource(ttstr)->load forward, clearCache()->module-map clear,
    // getBufLayer()->ttstr) are removed — they were the pre-inheritance
    // two-class artifacts. The local generic loadSource body is still a
    // by-name facade rather than Android's `(source, descriptor) -> Layer`
    // boundary; the exact tuple is currently restored only on the production
    // prepared-item route.
    class ResourceManager : public SourceCache {
    public:
        ResourceManager();
        ~ResourceManager();

        ResourceManager(const ResourceManager &) = delete;
        ResourceManager &operator=(const ResourceManager &) = delete;

        explicit ResourceManager(tTJSVariant kag, tjs_int cacheSize);

        tTJSVariant load(ttstr path);
        void unload(ttstr path) const;
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

        // Binary ResourceManager NCB members (ncb_registerMembers @0x6AB8BC,
        // 12 members total). The former STUB annotations are obsolete: the
        // mapped-record implementation now carries the corresponding raw
        // HashMap-A behavior without reproducing ARM64 ABI byte offsets.
        // C-1: bufLayer (prop-ro @0x6A84FC = base +40 Layer variant) is now the
        // INHERITED SourceCache::getBufLayer() — the RM-own ttstr getBufLayer()
        // was the pre-inheritance two-class artifact and is removed.
        void unloadAll() const;                         // @0x6A8CF8
        [[nodiscard]] bool isExistMotion(tTJSVariant projectKey,
                                         ttstr path) const;     // @0x6A96F8
        [[nodiscard]] tTJSVariant findMotion(tTJSVariant projectKey,
                                              ttstr path) const; // @0x6A9ED4
        static tjs_error random(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                iTJSDispatch2 *obj);            // @0x6AB56C

        [[nodiscard]] static tjs_int getEmotePSBDecryptSeed();

        static tjs_error setEmotePSBDecryptSeed(tTJSVariant *r, tjs_int count,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

        static tjs_error setEmotePSBDecryptFunc(tTJSVariant *r, tjs_int n,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

    private:
        friend class Player;

        // HashMap A is an inline ResourceManager member in ctor sub_6A88CC,
        // keyed by the raw, case-sensitive ttstr path with the recovered FNV
        // hash/equality functors. It is not shared through a State object.
        using LoadedModuleMap =
            std::unordered_map<ttstr, detail::LoadedResourceRecord,
                               detail::ttstr_hash,
                               detail::ttstr_equal>;
        mutable LoadedModuleMap _loadedModules;

        // ResourceManager ctor sub_6A88CC constructs the random variant before
        // the std::set and explicitly inserts the sentinel id 0. requireLayerId
        // @0x6AB694 starts from 1, so 0 is never returned; release(0) is a no-op.
        tTJSVariant _randomGenerator;
        std::set<tjs_int> _usedLayerIds{0};
        tjs_int _nextLayerId = 1;
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
        // which is precisely `_loadedModules` above (migrated to ttstr key in
        // P3-A). So HashMap A is NOT a separate parked field — _loadedModules IS it.
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
        // ResourceManager_ctor @0x6A8988 evaluates `new Math.RandomGenerator()`
        // into _randomGenerator above; field order follows map -> random -> set
        // -> counter -> spec, with the two marked Web-only fields interposed.
        tjs_int _spec = 0;
    };
} // namespace motion
