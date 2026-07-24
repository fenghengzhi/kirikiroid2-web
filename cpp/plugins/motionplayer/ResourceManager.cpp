//
// Created by LiDon on 2025/9/15.
//

#include "ResourceManager.h"
#include "ScriptMgnIntf.h"
#include "tjsDictionary.h"

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "RuntimeSupport.h"
#include "RenderManager.h"
#include "MotionDispatch.h"
#include "psbfile/PSBDispatch.h"
#include "SourceCache.h"
#include "ncbind.hpp"
#include "tjsUtils.h"
#include "xp3filter.h"

#define LOGGER spdlog::get("plugin")

namespace motion::detail {
    WinSourceTextureEntry::~WinSourceTextureEntry() {
        if(texture) {
            texture->Release();
        }
    }

    WinSourceTextureEntry::WinSourceTextureEntry(
        WinSourceTextureEntry &&other) noexcept : texture(other.texture) {
        other.texture = nullptr;
    }

    WinSourceTextureEntry &WinSourceTextureEntry::operator=(
        WinSourceTextureEntry &&other) noexcept {
        if(this == &other) {
            return *this;
        }
        if(texture) {
            texture->Release();
        }
        texture = other.texture;
        other.texture = nullptr;
        return *this;
    }

    void WinSourceTextureEntry::setTexture(iTVPTexture2D *value) {
        if(value == texture) {
            return;
        }
        if(value) {
            value->AddRef();
        }
        if(texture) {
            texture->Release();
        }
        texture = value;
    }

    PackedSourceAtlasEntry::~PackedSourceAtlasEntry() {
        if(texture) {
            texture->Release();
        }
    }

    PackedSourceAtlasEntry::PackedSourceAtlasEntry(
        PackedSourceAtlasEntry &&other) noexcept :
        texture(other.texture), originX(other.originX), originY(other.originY),
        textureRect(other.textureRect), clip(other.clip) {
        other.texture = nullptr;
    }

    PackedSourceAtlasEntry &PackedSourceAtlasEntry::operator=(
        PackedSourceAtlasEntry &&other) noexcept {
        if(this == &other) {
            return *this;
        }
        if(texture) {
            texture->Release();
        }
        texture = other.texture;
        originX = other.originX;
        originY = other.originY;
        textureRect = other.textureRect;
        clip = other.clip;
        other.texture = nullptr;
        return *this;
    }

    void PackedSourceAtlasEntry::setTexture(iTVPTexture2D *value) {
        if(value == texture) {
            return;
        }
        if(value) {
            value->AddRef();
        }
        if(texture) {
            texture->Release();
        }
        texture = value;
    }

    LoadedResourceRecord::LoadedResourceRecord() {
        // sub_6EBCFC @0x6EBCFC asks the libstdc++ prime rehash policy for 10
        // buckets independently for both nested maps.
        winSourceTextures.rehash(10);
        krkrSourceEntries.rehash(10);
    }
} // namespace motion::detail

namespace {
    void initializeRandomGenerator(tTJSVariant &generator) {
        TVPExecuteExpression(TJS_W("new Math.RandomGenerator()"), &generator);
    }

    PSB::PSBFile::OwnerFilter &emotePSBDecryptFilter() {
        // Global std::function at xmmword_1AB82E0, replaced through
        // sub_6A87D0.  It is process-wide rather than a ResourceManager field.
        static PSB::PSBFile::OwnerFilter filter;
        return filter;
    }

    PSB::PSBFile::OwnerFilter makeEmotePSBDecryptSeedFilter(
        std::uint32_t seed) {
        // sub_6863CC @0x6863CC: decrypt [header.encryptData,
        // header.chunkOffsets) with a four-word xorshift stream seeded by the
        // captured integer installed at 0x685D30.
        return [seed](PSB::PSBRawOwner &owner) {
            auto *header = owner.GetHeader();
            auto *cursor = header->encryptData;
            const auto length = static_cast<std::int32_t>(
                header->chunkOffsets - header->encryptData);
            if(length <= 0) {
                return;
            }

            auto *end = cursor + length;
            std::uint32_t x = 123456789u;
            std::uint32_t y = 362436069u;
            std::uint32_t z = 521288629u;
            std::uint32_t w = seed;
            std::uint32_t bytes = 0;
            do {
                if(bytes == 0) {
                    const std::uint32_t t = x ^ (x << 11u);
                    x = y;
                    y = z;
                    z = w;
                    w = w ^ (w >> 19u) ^ t ^ (t >> 8u);
                    bytes = w;
                }
                *cursor++ ^= static_cast<std::uint8_t>(bytes);
                bytes >>= 8u;
            } while(cursor < end);
        };
    }

    // The first 0x10-byte allocation in
    // EmotePlayer_setEmotePSBDecryptFunc_callback @0x685E60 stores exactly the
    // two dispatch pointers. Its final release path @0x685F74..0x685FA0
    // releases Object and ObjThis before deleting the allocation.
    class EmotePSBDecryptClosure final {
    public:
        explicit EmotePSBDecryptClosure(tTJSVariant &value) :
            closure_(value.AsObjectClosure()) {}

        ~EmotePSBDecryptClosure() { closure_.Release(); }

        void invoke(tTJSVariant **params) const {
            closure_.FuncCall(0, nullptr, nullptr, nullptr, 2, params,
                              nullptr);
        }

    private:
        tTJSVariantClosure closure_;
    };

    PSB::PSBFile::OwnerFilter makeEmotePSBDecryptFuncFilter(
        tTJSVariant &callable) {
        // 0x685E90..0x685F3C: the closure allocation is owned by the
        // pointer+RefCount control block implemented by TJS::tRefHolder, then
        // copied as the sole pointer-sized std::function capture.
        TJS::tRefHolder<EmotePSBDecryptClosure> closure(
            new EmotePSBDecryptClosure(callable));
        return [closure](PSB::PSBRawOwner &owner) {
            // EmotePlayer_DecryptFunc_call_guess @0x6865B4 constructs the same
            // CBinaryAccessor used by xp3filter.dll (ctor sub_62C808), creates
            // {object,size} variants, and ignores the callback result.
            auto *accessor = new CBinaryAccessor(
                owner.GetData(), static_cast<unsigned int>(owner.GetSize()));
            tTJSVariant accessorValue(accessor);
            // The Android body does not Release the constructor's initial
            // accessor reference after the variant AddRef. Preserve that
            // observable leak boundary instead of applying the XP3 wrapper's
            // balancing Release.
            tTJSVariant sizeValue(static_cast<tjs_int64>(owner.GetSize()));
            tTJSVariant *params[] = { &accessorValue, &sizeValue };
            closure->invoke(params);
        };
    }

} // namespace

// ResourceManager_ctor @0x6A88CC invokes SourceCache_ctor @0x6A78F4 before
// initializing its own maps and RandomGenerator. The base receives the KAG
// owner and byte budget; it owns the primary/buffer Layers and cache list.
motion::ResourceManager::ResourceManager() {
    // ResourceManager_ctor @0x6A891C initializes HashMap A with 10 buckets
    // before constructing the random generator @0x6A8988..0x6A8994.
    _loadedModules.rehash(10);
    initializeRandomGenerator(_randomGenerator);
}

motion::ResourceManager::ResourceManager(tTJSVariant kag,
                                         tjs_int cacheSize) :
    SourceCache(kag, cacheSize) {
    _loadedModules.rehash(10);
    initializeRandomGenerator(_randomGenerator);
    const bool hasKagObject = kag.Type() == tvtObject &&
        kag.AsObjectNoAddRef();
    tTJSVariantClosure kagClosure;
    if(hasKagObject) {
        kagClosure = kag.AsObjectClosureNoAddRef();
    }
    LOGGER->info("kag: {}, cacheSize: {}",
                 hasKagObject ? static_cast<void *>(kagClosure.Object) : nullptr,
                 cacheSize);

    // Pre-define ShortCutInitialPadKeyMap on the KAG window if not already set.
    // The encrypted keybinder.tjs accesses .ShortCutInitialPadKeyMap on the
    // window object. If undefined, it crashes with "Invalid object context".
    if(hasKagObject) {
        const tjs_char *padKeys[] = { TJS_W("ShortCutInitialPadKeyMap"),
                                      TJS_W("ShortCutInitialGamePadKeyMap"),
                                      TJS_W("_proceedingKeyList"), nullptr };
        for(int i = 0; padKeys[i]; ++i) {
            tTJSVariant existing;
            if(TJS_FAILED(kagClosure.PropGet(
                   0, padKeys[i], nullptr, &existing, nullptr)) ||
               existing.Type() == tvtVoid) {
                iTJSDispatch2 *dict = TJSCreateDictionaryObject();
                if(dict) {
                    tTJSVariant v(dict, dict);
                    kagClosure.PropSet(TJS_MEMBERENSURE, padKeys[i], nullptr,
                                       &v, nullptr);
                    dict->Release();
                }
            }
        }
    }
}

motion::ResourceManager::~ResourceManager() {
    // Motion_ResourceManager_destructor_guess @0x6A8B94 first clears HashMap A
    // in the destructor body. Automatic teardown then runs set -> random -> map
    // before SourceCache.
    _loadedModules.clear();
}

tjs_int motion::ResourceManager::getEmotePSBDecryptSeed() {
    return _decryptSeed;
}

tjs_error motion::ResourceManager::setEmotePSBDecryptSeed(tTJSVariant *,
                                                          tjs_int count,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *) {
    // EmotePlayer_setEmotePSBDecryptSeed_callback @ 0x685D30 accepts one or
    // more arguments and applies the ordinary TJS integer conversion to only
    // the first argument before installing the captured decrypt filter.
    if(count < 1) {
        return TJS_E_BADPARAMCOUNT;
    }
    _decryptSeed = static_cast<tjs_int>(*p[0]);
    emotePSBDecryptFilter() = makeEmotePSBDecryptSeedFilter(
        static_cast<std::uint32_t>(_decryptSeed));
    LOGGER->info("setEmotePSBDecryptSeed: {}", _decryptSeed);
    return TJS_S_OK;
}

tjs_error motion::ResourceManager::setEmotePSBDecryptFunc(tTJSVariant *,
                                                          tjs_int count,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *) {
    // EmotePlayer_setEmotePSBDecryptFunc_callback @0x685E60 accepts extra
    // arguments, converts only p[0] to an object closure, and swaps the new
    // std::function into the process-wide filter through sub_6A87D0.
    if(count < 1) {
        return TJS_E_BADPARAMCOUNT;
    }
    emotePSBDecryptFilter() = makeEmotePSBDecryptFuncFilter(*p[0]);
    return TJS_S_OK;
}

tTJSVariant motion::ResourceManager::load(ttstr path) {
    // ResourceManager_loadResource @0x6A8D8C first replaces its input with
    // TVPGetPlacedPath(path), then uses that exact normalized ttstr for both
    // HashMap-A lookup and insertion.
    path = TVPGetPlacedPath(path);
    PSB::PSBFile selected;
    // 0x6A8E8C..0x6A8EBC: a cache hit copies the one-pointer PSBFile holder,
    // then falls through to the common fresh-dispatch return block.
    if(const auto cached = _loadedModules.find(path);
       cached != _loadedModules.end()) {
        selected = cached->second.file;
    } else {
        if(!TVPIsExistentStorage(path)) {
            TVPThrowExceptionMessage(
                TJS_W("Motion::ResourceManager: file not found '%1'."), path);
        }

        PSB::PSBFile loaded;
        if(!loaded.LoadStorage(path, emotePSBDecryptFilter())) {
            TVPThrowExceptionMessage(TJS_W("cannot open psb file : %1"), path);
        }

        // 0x6A8F20..0x6A9204 performs strict raw-node validation before
        // consuming the loaded holder through sub_598A64.
        const PSB::PSBRawNode root = loaded.GetRoot();
        const char *id = root.GetDictionaryValueStrict("id").GetString();
        if(id == nullptr || std::strcmp(id, "motion") != 0) {
            TVPThrowExceptionMessage(
                TJS_W("this psb file is not motion file: %1"), path);
        }

        const char *spec = root.GetDictionaryValueStrict("spec").GetString();
        if(spec != nullptr && std::strcmp(spec, "krkr") == 0) {
            _spec = 1;
        }
        if(spec != nullptr && std::strcmp(spec, "win") == 0) {
            _spec = 2;
        }
        if(_spec == 0) {
            const char *label =
                root.GetDictionaryValueStrict("label").GetString();
            TVPThrowExceptionMessage(
                TJS_W(
                    "motion file '%1' has not adaptive spec. export psb again."),
                ttstr(label != nullptr ? label : ""));
        }

        if(root.GetDictionaryValueStrict("version").GetDouble() > 3.0300001) {
            // ResourceManager_loadResource @0x6A905C..0x6A91F4 constructs
            // the complete ttstr before throwing.  Keeping the label lookup in
            // this expression also preserves the temporary ttstr/raw-node
            // destruction order visible at 0x6A9180..0x6A91C4.
            ttstr message(TJS_W("motion file '"));
            message += ttstr(
                root.GetDictionaryValueStrict("label").GetString());
            message += TJS_W("' is too new.");
            TVPThrowExceptionMessage(message.c_str());
        }

        // sub_598A64 @ 0x598A64 returns a transferred holder; the caller at
        // 0x6A9238..0x6A9258 copy-assigns that temporary into selected and
        // destroys the temporary. sub_6EBB0C/sub_6EBCFC then default-construct
        // the mapped record and 0x6A926C..0x6A92A8 copies selected into it.
        selected = loaded.Transfer_guess();
        _loadedModules[path].file = selected;
    }

    // 0x6A92AC..0x6A92F8 destroys root and the cleared loaded holder before
    // both hit/miss paths construct the fresh dispatch directly from selected
    // @0x6A92FC..0x6A9358.  There is no PSBFile::GetRoot call and thus no
    // retained PSBRawNode temporary on this boundary.
    PSB::PSBRawOwner *owner = selected.GetOwner();
    auto *dispatch = new PSB::PSBValueDispatch(
        selected.GetOwnerSlotAddress_guess(), owner->GetHeader()->entries);
    tTJSVariant result(dispatch, dispatch);
    dispatch->Release();
    return result;
}

// C-1 (2026-06-07): RM-own loadSource(ttstr)->load(path) forward REMOVED. The
//   binary RM `loadSource` NCB member (sub_6A7BA8) is the INHERITED
//   SourceCache::loadSource(source, descriptor) base method (the RM
//   registrar @0x6AB8BC re-lists the SAME callback address sub_6A7BA8 that the
//   SourceCache registrar @0x6A85A8 binds). It materialises a Layer into the
//   SourceCache base +72 list — it is NOT a thin forward to RM::load. The
//   inherited local method serves the RM NCB binding, but its remaining
//   by-name facade shape is explicitly tracked as an open source-structure gap;
//   only the prepared-item production wrapper currently restores the exact
//   source/descriptor cache tuple.

void motion::ResourceManager::unload(ttstr path) const {
    LOGGER->debug("ResourceManager::unload({})", path.AsStdString());
    // ResourceManager_unload @0x6A959C normalizes before the same FNV/wcscmp
    // lookup used by loadResource.
    path = TVPGetPlacedPath(path);
    _loadedModules.erase(path);
}

// C-1 (2026-06-07): RM-own clearCache() const REMOVED. The binary RM
//   `clearCache` NCB member (sub_6A8438) is the INHERITED
//   SourceCache::clearCache() base method (RM registrar @0x6AB8BC re-lists the
//   SAME callback address sub_6A8438 the SourceCache registrar @0x6A85A8
//   binds). sub_6A8438 touches ONLY the SourceCache base +72 layer-list
//   (releases each Layer image via vtable+112, frees nodes, resets +72/+80
//   sentinels and +60=0); it does NOT clear HashMap A or the layer-id set —
//   the prior RM-own body that cleared _loadedModules was a documented
//   deviation (see old NOTE),
//   now correctly dropped: the inherited SourceCache::clearCache() serves the
//   RM NCB binding faithfully. The module cache lifetime is governed by
//   load/unload/unloadAll, not clearCache.

// Aligned with libkrkr2.so ResourceManager::findSource (sub_6AAB3C) at
// 0x6AAB3C. The binary:
//   1. split path by "/" (sub_697D34); an empty first element -> result void.
//      The helper always appends its final remainder, so no vector-size gate
//      exists here or at the direct pieces[1]/pieces[2] consumers.
//   2. if pieces[0] != "src" (sub_9B1ED0): if "blank", split pieces[1] by
//      ":" and write width/height/originX/originY as String Variants plus
//      Integer blank=1 through ncbDictionaryAccessor::SetValue; otherwise void.
//   3. for "src": HashMap A (this+88 buckets / this+96 count) lookup keyed by
//      moduleKey (a2, FNV hash cached in ttstr+68 via sub_6EB8F4). The
//      requested source path is the separate a3 argument. Player_findSource
//      @0x6948E8 supplies Player+1012 as moduleKey and the resolved src path as
//      a3. Player_playImpl @0x6B2284 fills +1012 from findMotion result[1],
//      which ResourceManager_findMotion @0x6A9ED4 copies from the matched map
//      key.
//   4. navigate the mapped record's raw root["source"][group]["icon"][icon];
//      only the dynamic group/icon keys have hasKey gates.
//   5. on hit: operator new(0x18) ObjSource facade holding the icon raw node
//      (qword[0..1]=owner/node, qword[2]=0), wrapped as a TJS object via the
//      NCB class object (sub_6EC124). Port: new ObjSource(iconNode) +
//      ncbInstanceAdaptor<ObjSource>::CreateAdaptor.
tTJSVariant motion::ResourceManager::findSource(ttstr moduleKey,
                                                ttstr path) const {
    // 1. split name by "/" (sub_697D34 @0x697D34).
    const std::vector<ttstr> pieces =
        detail::splitTtstrLike_0x697D34(path, TJS_W('/'));
    if(pieces[0].IsEmpty()) {
        return {}; // LABEL_11: *(a4+16)=0 -> void
    }

    // 2. prefix gate: "src" (sub_9B1ED0 @0x9B1ED0 == 0 means equal).
    if(pieces[0] != TJS_W("src")) {
        if(pieces[0] != TJS_W("blank")) {
            return {}; // LABEL_11
        }

        // 0x6AAC74..0x6AAE84 is the inlined ncbDictionaryAccessor path:
        // each SetValue owns a fresh temporary Variant, calls PropSet with a
        // distinct hint slot, then destroys that Variant immediately.
        const std::vector<ttstr> dims =
            detail::splitTtstrLike_0x697D34(pieces[1], TJS_W(':'));
        ncbDictionaryAccessor dictionary;
        dictionary.SetValue(TJS_W("width"), dims[0], TJS_MEMBERENSURE,
                            &detail::widthMemberHint_guess);
        dictionary.SetValue(TJS_W("height"), dims[1], TJS_MEMBERENSURE,
                            &detail::heightMemberHint_guess);
        dictionary.SetValue(TJS_W("originX"), dims[2], TJS_MEMBERENSURE,
                            &detail::originXMemberHint_guess);
        dictionary.SetValue(TJS_W("originY"), dims[3], TJS_MEMBERENSURE,
                            &detail::originYMemberHint_guess);
        dictionary.SetValue(TJS_W("blank"), static_cast<tjs_int>(1),
                            TJS_MEMBERENSURE, &detail::blankMemberHint_guess);
        return tTJSVariant(dictionary.GetDispatch(), dictionary.GetDispatch());
    }

    // 0x6AAC10/0x6AAC20 converts both direct vector elements before touching
    // HashMap A.  Malformed paths therefore retain the binary's unchecked
    // indexing boundary even when moduleKey is absent.
    const std::string groupKey = detail::narrow(pieces[1]);
    const std::string iconKey = detail::narrow(pieces[2]);

    // 3. HashMap A lookup keyed directly by moduleKey (a2).  sub_6EB8F4's
    // null bucket/node return is represented completely by find()==end(); the
    // value's PSB owner is not checked a second time in 0x6AAF00.
    const auto recordIt = _loadedModules.find(moduleKey);
    if(recordIt == _loadedModules.end()) {
        return {}; // !v27 || !*v27 -> LABEL_71 result void
    }
    detail::LoadedResourceRecord *record = &recordIt->second;

    // 4. Raw root navigation. sub_598C58 is strict for the fixed keys;
    // sub_5995D8 gates only the two dynamic keys before their strict reads.
    PSB::PSBRawOwner *owner = record->file.GetOwner();
    const PSB::PSBRawNode root(owner, owner->GetHeader()->entries);
    const PSB::PSBRawNode source = root.GetDictionaryValueStrict("source");
    if(!source.ContainsDictionaryKey(groupKey.c_str())) { // sub_5995D8 gate
        return {}; // LABEL_64 -> result void
    }
    // The group node is a full-expression temporary: Android destroys it at
    // 0x6AAF74 immediately after constructing iconHolder, before the icon gate.
    const PSB::PSBRawNode iconHolder =
        source.GetDictionaryValueStrict(groupKey.c_str())
            .GetDictionaryValueStrict("icon");
    if(!iconHolder.ContainsDictionaryKey(iconKey.c_str())) { // sub_5995D8 gate
        return {};
    }
    const PSB::PSBRawNode iconEntry =
        iconHolder.GetDictionaryValueStrict(iconKey.c_str());

    // 5. copy the raw pair into ObjSource (owner AddRef), zero its texture,
    // then attach it to the NCB native instance (0x6AAFC0..0x6AB02C).
    using ObjSourceAdaptor = ncbInstanceAdaptor<motion::ObjSource>;
    motion::ObjSource *src = new motion::ObjSource(iconEntry);
    if(iTJSDispatch2 *dispatch = ObjSourceAdaptor::CreateAdaptor(src)) {
        tTJSVariant result(dispatch, dispatch);
        dispatch->Release();
        return result;
    }
    // 0x6AAFE0 passes sticky=false/err=false; the null-adaptor branch at
    // 0x6AB044 returns void without reclaiming the just-allocated ObjSource.
    return {};
}

tjs_int motion::ResourceManager::requireLayerId() {
    // Aligned with sub_6AB694 @0x6AB694. Binary topology (cross-verified by
    // fresh decompile 2026-06-06, disasm 0x6ab694-0x6ab74c):
    //   counter = (uint*)(this+216);            // ctor 0x6a8a3c seeds it to 1
    //   lower_bound(set@+168, *counter):        // 0x6ab6a4-0x6ab728
    //     while (*counter ∈ set) ++*counter;    // skip already-used ids, retry
    //   set._M_insert_unique(*counter);         // 0x6ab734
    //   ret = *counter; *counter += 1; return ret; // 0x6ab738-0x6ab74c
    // The counter (+216) is monotone and only ever holds a value NOT yet handed
    // out, so the skip-loop body never actually iterates and the function never
    // reuses a released id (the counter never rewinds). nextLayerId here is the
    // same persistent monotone counter, so this `while(find()!=end)++` mirrors
    // the binary's lower_bound-skip exactly — it is NOT a "search lowest free
    // slot / reuse released id" scheme. (2026-06-06 audit item #5 claimed local
    // reuses released ids while binary is a pure monotone counter; BOTH halves
    // were wrong — binary also has the skip-loop, local also never rewinds. No
    // behavioral divergence under any require/release interleave. Audit #5 is a
    // misjudgement; no change made.)
    // The ctor-level {0} insertion at 0x6A8A08 is a real source operation,
    // not an inert compiler artifact; _usedLayerIds is initialized with it.
    while(_usedLayerIds.find(_nextLayerId) != _usedLayerIds.end()) {
        ++_nextLayerId;
    }
    const auto id = _nextLayerId;
    _usedLayerIds.insert(id);
    ++_nextLayerId;
    return id;
}

// P3-B (2026-06-05): releaseLayerId aligned to sub_6AB750 @0x6AB750 —
//   erase the id from the std::set<unsigned int> @+168 and nothing else. The
//   binary keeps NO name<->id maps (requireLayerIdForName removed, its string
//   has 0 hits in libkrkr2.so); the by-name cleanup that used to live here is
//   gone.
void motion::ResourceManager::releaseLayerId(tjs_int id) {
    if(id == 0) {
        return;
    }
    _usedLayerIds.erase(id);
}

// Binary ResourceManager members from ncb_registerMembers @0x6AB8BC. HashMap A
// is represented by the mapped raw-file records in _loadedModules.

// C-1 (2026-06-07): RM-own getBufLayer()->ttstr REMOVED. The binary RM
//   `bufLayer` prop-ro (sub_6A84FC) reads `a1+40` = the SourceCache base
//   bufLayer LAYER VARIANT (set by the SourceCache base ctor sub_6A78F4 via
//   sub_A0FB64(a1+40, newLayer)), NOT a ttstr name. The RM registrar @0x6AB8BC
//   re-lists the SAME callback address sub_6A84FC the SourceCache registrar
//   @0x6A85A8 binds. So the inherited SourceCache::getBufLayer() (returns the
//   base tTJSVariant _bufLayer) now serves the RM NCB binding faithfully; the
//   former RM ttstr getBufLayer() was a misattributed two-class artifact.

void motion::ResourceManager::unloadAll() const {
    // unloadAll @0x6A8CF8 clears ONLY HashMap A. The preceding merged body at
    // 0x6A8B94 is the ResourceManager destructor, not unloadAll.
    LOGGER->debug("ResourceManager::unloadAll()");
    _loadedModules.clear();
}

bool motion::ResourceManager::isExistMotion(tTJSVariant projectKey,
                                            ttstr path) const {
    const std::vector<ttstr> pieces =
        detail::splitTtstrLike_0x697D34(path, TJS_W('/'));
    const std::string chara = detail::narrow(pieces[1]);
    const std::string motionName = detail::narrow(pieces[2]);
    // ResourceManager_isExistMotion keeps the direct map hit
    // @0x6A9870..0x6A9910 and fallback node-chain walk
    // @0x6A99A4..0x6A9A3C as two separately expanded raw-node paths.
    // 0x6A9798..0x6A9844: void skips the direct lookup.  Every other type
    // must already be String; GetString preserves the binary's strict type
    // error before the independent ttstr key copy is constructed.
    if(projectKey.Type() != tvtVoid) {
        const auto direct =
            _loadedModules.find(ttstr(projectKey.GetString()));
        if(direct != _loadedModules.end()) {
            PSB::PSBRawOwner *owner = direct->second.file.GetOwner();
            const PSB::PSBRawNode root(owner, owner->GetHeader()->entries);
            const PSB::PSBRawNode objects =
                root.GetDictionaryValueStrict("object");
            if(objects.ContainsDictionaryKey(chara.c_str())) {
                const PSB::PSBRawNode motions =
                    objects.GetDictionaryValueStrict(chara.c_str())
                        .GetDictionaryValueStrict("motion");
                if(motions.ContainsDictionaryKey(motionName.c_str())) {
                    return true;
                }
            }
        }
    }
    for(const auto &entry : _loadedModules) {
        PSB::PSBRawOwner *owner = entry.second.file.GetOwner();
        const PSB::PSBRawNode root(owner, owner->GetHeader()->entries);
        const PSB::PSBRawNode objects =
            root.GetDictionaryValueStrict("object");
        if(!objects.ContainsDictionaryKey(chara.c_str())) {
            continue;
        }
        const PSB::PSBRawNode motions =
            objects.GetDictionaryValueStrict(chara.c_str())
                .GetDictionaryValueStrict("motion");
        if(motions.ContainsDictionaryKey(motionName.c_str())) {
            return true;
        }
    }
    return false;
}

tTJSVariant motion::ResourceManager::findMotion(tTJSVariant projectKey,
                                                ttstr path) const {
    const std::vector<ttstr> pieces =
        detail::splitTtstrLike_0x697D34(path, TJS_W('/'));
    const std::string chara = detail::narrow(pieces[1]);
    const std::string motionName = detail::narrow(pieces[2]);
    // Motion_ResourceManager_findMotion keeps the direct hit
    // @0x6AA058..0x6AA124 and fallback walk @0x6AA360..0x6AA424 separately
    // expanded.  Only each path's final motion node is wrapped in a dispatch.
    // 0x6A9F80..0x6AA02C mirrors isExistMotion's Variant gate and strict
    // String extraction before the direct HashMap lookup.
    if(projectKey.Type() != tvtVoid) {
        const auto direct =
            _loadedModules.find(ttstr(projectKey.GetString()));
        if(direct != _loadedModules.end()) {
            PSB::PSBRawOwner *owner = direct->second.file.GetOwner();
            const PSB::PSBRawNode root(owner, owner->GetHeader()->entries);
            const PSB::PSBRawNode objects =
                root.GetDictionaryValueStrict("object");
            if(objects.ContainsDictionaryKey(chara.c_str())) {
                const PSB::PSBRawNode motions =
                    objects.GetDictionaryValueStrict(chara.c_str())
                        .GetDictionaryValueStrict("motion");
                if(motions.ContainsDictionaryKey(motionName.c_str())) {
                    const PSB::PSBRawNode motion =
                        motions.GetDictionaryValueStrict(motionName.c_str());
                    auto *dispatch = new PSB::PSBValueDispatch(
                        motion.GetOwnerSlotAddress_guess(), motion.GetNode());
                    auto result = detail::createTJSArrayWithItems_guess();
                    result.items->emplace_back(dispatch, dispatch);
                    dispatch->Release();
                    result.items->emplace_back(direct->first);
                    return result.value;
                }
            }
        }
    }
    for(const auto &entry : _loadedModules) {
        PSB::PSBRawOwner *owner = entry.second.file.GetOwner();
        const PSB::PSBRawNode root(owner, owner->GetHeader()->entries);
        const PSB::PSBRawNode objects =
            root.GetDictionaryValueStrict("object");
        if(!objects.ContainsDictionaryKey(chara.c_str())) {
            continue;
        }
        const PSB::PSBRawNode motions =
            objects.GetDictionaryValueStrict(chara.c_str())
                .GetDictionaryValueStrict("motion");
        if(!motions.ContainsDictionaryKey(motionName.c_str())) {
            continue;
        }
        const PSB::PSBRawNode motion =
            motions.GetDictionaryValueStrict(motionName.c_str());
        auto *dispatch = new PSB::PSBValueDispatch(
            motion.GetOwnerSlotAddress_guess(), motion.GetNode());
        auto result = detail::createTJSArrayWithItems_guess();
        result.items->emplace_back(dispatch, dispatch);
        dispatch->Release();
        result.items->emplace_back(entry.first);
        return result.value;
    }
    return {};
}

tjs_error motion::ResourceManager::random(tTJSVariant *r, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    // ResourceManager_random @0x6AB56C copies the ctor-created generator
    // variant at +144, calls random() with no arguments, converts every
    // non-void result through TJS numeric conversion, and otherwise keeps 0.
    auto *self =
        ncbInstanceAdaptor<ResourceManager>::GetNativeInstance(objthis, true);
    if(!self) {
        return TJS_E_INVALIDOBJECT;
    }

    double value = 0.0;
    auto *generator = self->_randomGenerator.AsObjectNoAddRef();
    tTJSVariant result;
    static tjs_uint32 hint = 0;
    if(TJS_SUCCEEDED(generator->FuncCall(0, TJS_W("random"), &hint, &result,
                                         0, nullptr, generator))) {
        if(result.Type() != tvtVoid) {
            value = static_cast<double>(result);
        }
    }
    if(r) {
        *r = value;
    }
    return TJS_S_OK;
}
