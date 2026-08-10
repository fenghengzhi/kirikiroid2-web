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
            std::array<int, 4> textureRect{0, 0, 0, 0};
            std::array<double, 4> clip{0.0, 0.0, 1.0, 1.0};
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

    // The four current ResourceManager registrars are sub_6A8C9C,
    // sub_57C3A8, sub_100102E88, and sub_1002FC. Each re-lists the exact
    // loadSource/clearCache/bufLayer callbacks registered by SourceCache:
    //   6A4F88/6A5818/6A58DC, 57ACC8/57B018/57B060,
    //   1001009AC/100100F10/100100F84, FDB50/FE0D4/FE11A.
    // That four-file registration shape is the evidence for the public base
    // used here. The inherited cache owns std::list<Entry>; ResourceManager
    // separately owns the PSB module map and each record's Win/KRKR texture
    // maps. Player's by-name helper is Web compatibility code and does not
    // create a second cache topology.
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
        // and releaseLayerId(id): 6A8A74/6A8B30, 57C258/57C2C8,
        // 100102D40/100102DB8, and 100240/10028A.
        tjs_int requireLayerId();
        void releaseLayerId(tjs_int id);

        // Current four-file member mapping is recorded beside the definitions;
        // the inherited SourceCache supplies bufLayer/clearCache/loadSource.
        void unloadAll() const;
        [[nodiscard]] bool isExistMotion(tTJSVariant projectKey,
                                         ttstr path) const;
        [[nodiscard]] tTJSVariant findMotion(tTJSVariant projectKey,
                                              ttstr path) const;
        static tjs_error random(tTJSVariant *r, tjs_int n, tTJSVariant **p,
                                iTJSDispatch2 *obj);

        static tjs_error setEmotePSBDecryptSeed(tTJSVariant *r, tjs_int count,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

        static tjs_error setEmotePSBDecryptFunc(tTJSVariant *r, tjs_int n,
                                                tTJSVariant **p,
                                                iTJSDispatch2 *obj);

    private:
        friend class Player;

        // Fresh four-file load/cache decompiles identify one inline
        // unordered_map at +0x58/+0x34/+0x60/+0x38 (A64/A32/i64/i32), keyed
        // by the raw case-sensitive ttstr path with the recovered hash/equality
        // functors. The mapped record retains the PSB owner and texture maps.
        using LoadedModuleMap =
            std::unordered_map<ttstr, detail::LoadedResourceRecord,
                               detail::ttstr_hash,
                               detail::ttstr_equal>;
        mutable LoadedModuleMap _loadedModules;

        // The id allocator starts after sentinel 0; release(0) is a no-op.
        tTJSVariant _randomGenerator;
        std::set<tjs_int> _usedLayerIds{0};
        tjs_int _nextLayerId = 1;
        // load maps spec "krkr"/"win" to 1/2; this port consumes the same flag.
        tjs_int _spec = 0;
    };
} // namespace motion
