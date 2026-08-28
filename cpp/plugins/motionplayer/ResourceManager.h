//
// Created by LiDon on 2025/9/15.
//
#pragma once
#include <array>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "tjs.h"
#include "../../core/visual/ComplexRect.h"
#include "internal/ttstr_hash.h"
#include "psbfile/PSBRawFile.h"
#include "SourceCache.h"

class iTVPTexture2D;

namespace motion {

    namespace detail {
        // Win source-cache mapped value in Player::findSourceForNode_guess.
        // Assignment retains the texture and destruction releases it.
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

        // KRKR source descriptor stored in the second nested map and populated
        // by Player::loadKrkrAtlasSource_guess.
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
            // The native publisher passes this exact in-map subobject to
            // iTVPTexture2D::Update; it is not merely four copied coordinates.
            tTVPRect textureRect{0, 0, 0, 0};
            // operator[] value-initializes the entire descriptor to zero.  The
            // atlas publisher writes {0,0,1,1} only when the source has no
            // explicit clip; an exception before that point leaves zero clip.
            std::array<double, 4> clip{0.0, 0.0, 0.0, 0.0};
        };

        // Mapped value of ResourceManager's outer ttstr unordered_map.
        // Current insertion paths construct exactly PSB owner + Win map + KRKR
        // map. Declaration order deliberately makes ordinary C++ destruction
        // run KRKR map -> Win map -> PSBFile.
        struct LoadedResourceRecord {
            LoadedResourceRecord();

            PSB::PSBFile file;
            std::unordered_map<ttstr, WinSourceTextureEntry,
                               ttstr_hash, ttstr_equal> winSourceTextures;
            std::unordered_map<ttstr, PackedSourceAtlasEntry,
                               ttstr_hash, ttstr_equal> krkrSourceEntries;
        };
    } // namespace detail

    // In all four current binaries the ResourceManager registrar re-lists the
    // exact loadSource/clearCache/bufLayer callbacks from SourceCache; the
    // bufLayer setter and member-adjustment slots remain zero in both tables.
    // Constructor/destructor paths also operate on SourceCache at native offset
    // zero, which is the evidence for the public first base used here. This C++
    // layout fact does not merge their script identities: ResourceManager and
    // SourceCache have different ClassInfo tuples, class IDs, class objects and
    // adaptor types, and the ResourceManager subclass item records no parent.
    // The inherited cache owns std::list<Entry>; ResourceManager
    // separately owns the PSB module map and each record's Win/KRKR texture
    // maps. The former port-only Player by-name helper was removed; no second
    // cache topology exists.
    //
    // ObjSource is independently proven as a 0x18-byte facade on both 64-bit
    // references and a 0x0c-byte facade on both 32-bit references: retained
    // PSBRawOwner/node pair followed by one retained lazy texture pointer.
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
        // All four current registrars expose requireLayerId() without a name
        // and releaseLayerId(id).
        tjs_int requireLayerId();
        tjs_int releaseLayerId(tjs_int id);

        // Current four-file member mapping is recorded beside the definitions;
        // the inherited SourceCache supplies bufLayer/clearCache/loadSource.
        void unloadAll() const;
        [[nodiscard]] bool isExistMotion(tTJSVariant projectKey,
                                         ttstr path) const;
        [[nodiscard]] tTJSVariant findMotion(tTJSVariant projectKey,
                                              ttstr path) const;
        [[nodiscard]] double random();
        // Unregistered test-only owner injection. It keeps the production
        // constructor expression and public NCB surface unchanged while
        // making random()'s borrowed-dispatch lifetime directly observable.
        void setRandomGeneratorForDifferentialTest_guess(
            const tTJSVariant &generator) {
            _randomGenerator = generator;
        }

        static tjs_error setEmotePSBDecryptSeed(tTJSVariant *r, tjs_int count,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

        static tjs_error setEmotePSBDecryptFunc(tTJSVariant *r, tjs_int n,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

    private:
        friend class Player;

        // Fresh four-file load/cache decompiles identify one default-constructed
        // unordered_map keyed by the raw case-sensitive ttstr path. The older
        // Android libstdc++ implementation eagerly materializes its default
        // bucket policy; iOS libc++ leaves zero buckets until first insertion.
        // That is an STL ABI distinction, not an explicit source rehash.
        using LoadedModuleMap =
            std::unordered_map<ttstr, detail::LoadedResourceRecord,
                               detail::ttstr_hash,
                               detail::ttstr_equal>;
        mutable LoadedModuleMap _loadedModules;

        // The original stores unsigned 32-bit ids. Construction first leaves
        // the set empty, evaluates the RandomGenerator script expression,
        // then inserts sentinel zero and writes the two following ones. A
        // successful release(id) lookup erases [id,end), including later ids,
        // while the monotone/wrapping counter is retained.
        tTJSVariant _randomGenerator;
        std::set<tjs_uint32> _usedLayerIds;
        tjs_uint32 _nextLayerId;
        // All four constructors contain this independent 32-bit state slot
        // between nextLayerId and spec and initialize it to one. A complete
        // four-reference plugin-range audit finds no post-construction read,
        // write, or destructor cleanup; keep the unknown original name.
        tjs_uint32 _layerIdState_guess;
        // load maps spec "krkr"/"win" to 1/2; zero rejects an unknown mode.
        tjs_int _spec = 0;
    };
} // namespace motion
