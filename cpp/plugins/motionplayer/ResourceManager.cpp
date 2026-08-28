//
// Created by LiDon on 2025/9/15.
//

#include "ResourceManager.h"
#include "ScriptMgnIntf.h"
#include "tjsDictionary.h"

#include <cstdint>
#include <cstring>
#include <iterator>
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
        if(texture) {
            texture->Release();
        }
        texture = value;
        if(value) {
            value->AddRef();
        }
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
        if(texture) {
            texture->Release();
        }
        texture = value;
        if(value) {
            value->AddRef();
        }
    }

    LoadedResourceRecord::LoadedResourceRecord() = default;
} // namespace motion::detail

namespace {
    void initializeRandomGenerator(tTJSVariant &generator) {
        TVPExecuteExpression(TJS_W("new Math.RandomGenerator()"), &generator);
    }

    // Process-wide, default-empty owner filter.  All four references register
    // its std::function destructor during static initialization, so the last
    // installed seed/function target survives every ResourceManager instance
    // and is released only by a later replacement or process teardown.  The
    // native boundary has no lock: replacement concurrent with load is a data
    // race and must not be hidden behind a portable mutex or snapshot.
    PSB::PSBFile::OwnerFilter emotePSBDecryptFilter;

    void replaceEmotePSBDecryptFilter_guess(
        const PSB::PSBFile::OwnerFilter &filter) {
        // Both native standard-library implementations copy-construct a
        // temporary before swapping it with the process-global object, then
        // destroy the former target.  A throwing copy therefore leaves the old
        // filter intact; successful replacement destroys it before return.
        emotePSBDecryptFilter = filter;
    }

    PSB::PSBFile::OwnerFilter makeEmotePSBDecryptSeedFilter(
        tjs_int64 seed) {
        // All four current references decrypt
        // [header.encryptData, header.chunkOffsets) with the same four-word
        // xorshift stream, seeded by the low word of the captured 64-bit TJS
        // Integer. A zero remaining word is the native refill sentinel, so a
        // generated word with a zero high-byte suffix advances early instead
        // of unconditionally lasting four bytes.
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
            std::uint32_t w = static_cast<std::uint32_t>(seed);
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

    // The four current function callbacks retain exactly the Object and ObjThis
    // dispatch pointers in a two-pointer closure. Final control-block release
    // releases both pointers.
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
        // The closure is owned by a pointer+RefCount tRefHolder control block,
        // then copied as the sole pointer-sized std::function capture.
        TJS::tRefHolder<EmotePSBDecryptClosure> closure(
            new EmotePSBDecryptClosure(callable));
        return [closure](PSB::PSBRawOwner &owner) {
            // All four invokers construct the same CBinaryAccessor shape,
            // create {object,size} variants, call the closure with two
            // arguments, and ignore its result.
            auto *accessor = new CBinaryAccessor(
                owner.GetData(), static_cast<unsigned int>(owner.GetSize()));
            tTJSVariant accessorValue(accessor);
            // None of the four bodies releases the constructor's initial
            // accessor reference after the variant AddRef. Preserve that
            // observable leak boundary.
            tTJSVariant sizeValue(static_cast<tjs_int64>(owner.GetSize()));
            tTJSVariant *params[] = { &accessorValue, &sizeValue };
            closure->invoke(params);
        };
    }

} // namespace

// SourceCache is the base subobject; it owns the Layers and list cache before
// ResourceManager initializes its module map and random generator.
motion::ResourceManager::ResourceManager() {
    // The module map is ordinary default-constructed state. The Android
    // references' older libstdc++ policy eagerly chooses/allocates from a
    // ten-bucket hint; libc++ on both iOS references leaves it bucketless
    // until the first insertion. No plugin-source rehash call exists.
    initializeRandomGenerator(_randomGenerator);
    // All four native constructors evaluate the script expression before the
    // set allocates its sentinel node. Keep these writes in the constructor
    // body: moving `{0}` or the two ones into member initializers changes the
    // allocation/unwind boundary when evaluation or insertion throws.
    _usedLayerIds.insert(0);
    _nextLayerId = 1;
    _layerIdState_guess = 1;
}

motion::ResourceManager::ResourceManager(tTJSVariant kag,
                                         tjs_int cacheSize) :
    SourceCache(kag, cacheSize) {
    initializeRandomGenerator(_randomGenerator);
    _usedLayerIds.insert(0);
    _nextLayerId = 1;
    _layerIdState_guess = 1;
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
    // Clear the module map in the derived destructor body before automatic
    // derived-member and SourceCache-base teardown.  The base list destructor
    // releases entry Layers directly; it does not call public clearCache and
    // therefore does not send Invalidate during object destruction.
    _loadedModules.clear();
}

tjs_error motion::ResourceManager::setEmotePSBDecryptSeed(tTJSVariant *,
                                                          tjs_int count,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *) {
    // All four callbacks accept extra arguments and apply ordinary TJS integer
    // conversion only to p[0] before installing the seed filter.
    if(count < 1) {
        return TJS_E_BADPARAMCOUNT;
    }
    const tjs_int64 seed = static_cast<tjs_int64>(*p[0]);
    const auto filter = makeEmotePSBDecryptSeedFilter(seed);
    replaceEmotePSBDecryptFilter_guess(filter);
    return TJS_S_OK;
}

tjs_error motion::ResourceManager::setEmotePSBDecryptFunc(tTJSVariant *,
                                                          tjs_int count,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *) {
    // All four callbacks accept extra arguments and convert only p[0] to an
    // object closure before replacing the process-wide filter target.
    if(count < 1) {
        return TJS_E_BADPARAMCOUNT;
    }
    const auto filter = makeEmotePSBDecryptFuncFilter(*p[0]);
    replaceEmotePSBDecryptFilter_guess(filter);
    return TJS_S_OK;
}

tTJSVariant motion::ResourceManager::load(ttstr path) {
    // All four current loaders first replace the input with
    // TVPGetPlacedPath(path), then use that exact ttstr for lookup and
    // insertion in the module map.
    path = TVPGetPlacedPath(path);
    PSB::PSBFile selected;
    // A cache hit copies the one-pointer PSBFile holder, then falls through to
    // the common fresh-dispatch return block.
    if(const auto cached = _loadedModules.find(path);
       cached != _loadedModules.end()) {
        selected = cached->second.file;
    } else {
        if(!TVPIsExistentStorage(path)) {
            TVPThrowExceptionMessage(
                TJS_W("Motion::ResourceManager: file not found '%1'."), path);
        }

        PSB::PSBFile loaded;
        // The references pass the process-global lvalue directly.  There is no
        // per-load std::function copy, atomic snapshot, or synchronization.
        if(!loaded.LoadStorage(path, emotePSBDecryptFilter)) {
            TVPThrowExceptionMessage(TJS_W("cannot open psb file : %1"), path);
        }

        // Each reference performs the same strict raw-node validation before
        // transferring the loaded holder into the cache.
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
            // The label raw node stays alive while its ttstr is constructed;
            // the throwing call then unwinds the ttstr before the node.
            TVPThrowExceptionMessage(
                TJS_W(
                    "motion file '%1' has not adaptive spec. export psb again."),
                ttstr(root.GetDictionaryValueStrict("label").GetString()));
        }

        if(root.GetDictionaryValueStrict("version").GetDouble() > 3.0300001) {
            // All four construct the complete message before throwing. Keeping
            // the label lookup in this expression preserves the common
            // temporary ttstr/raw-node destruction order.
            ttstr message(TJS_W("motion file '"));
            message += ttstr(
                root.GetDictionaryValueStrict("label").GetString());
            message += TJS_W("' is too new.");
            TVPThrowExceptionMessage(message.c_str());
        }

        // Transfer returns a holder and clears loaded. The caller copy-assigns
        // that temporary into selected, destroys the temporary, default-
        // constructs the mapped record, then copies selected into its file.
        selected = loaded.Transfer_guess();
        _loadedModules[path].file = selected;
    }

    // Both hit and miss paths construct a fresh dispatch directly from
    // selected after the miss-path root and cleared loaded holder are gone.
    // There is no PSBFile::GetRoot call and no retained PSBRawNode temporary on
    // this boundary in any of the four references.
    PSB::PSBRawOwner *owner = selected.GetOwner();
    auto *dispatch = new PSB::PSBValueDispatch(
        selected, owner->GetHeader()->entries);
    tTJSVariant result(dispatch, dispatch);
    dispatch->Release();
    return result;
}

// The four ResourceManager registrars re-list the exact SourceCache::loadSource
// callback. It is inherited `(source, descriptor)` cache materialization, not
// a by-name forward to ResourceManager::load.

void motion::ResourceManager::unload(ttstr path) const {
    // Every current unload callback normalizes before performing the same
    // case-sensitive map lookup used by load.
    path = TVPGetPlacedPath(path);
    _loadedModules.erase(path);
}

// The four ResourceManager registrars also re-list the inherited clearCache
// callback. It clears only SourceCache's cached-entry Layer list;
// load/unload/unloadAll govern the PSB module map, while bufLayer persists.

// The four current icon/source lookup boundaries are:
// All four current reference implementations share this flow:
//   1. split path by "/" (detail::splitTtstr_guess); an empty first element
//      produces a void result.
//      The helper always appends its final remainder, so no vector-size gate
//      exists here or at the direct pieces[1]/pieces[2] consumers.
//   2. if pieces[0] != "src": if "blank", split pieces[1] by
//      ":" and write width/height/originX/originY as String Variants plus
//      Integer blank=1 through ncbDictionaryAccessor::SetValue; otherwise void.
//   3. for "src": look up the module map by moduleKey. The requested source
//      path is the separate argument. Public Player::findSource passes its
//      retained motion-context Variant and the caller's path through the
//      ResourceManager dispatch. The internal findSourceForNode path builds a
//      src/icon fallback path and uses the same dispatch boundary; its native
//      atlas routes remain separate from this script-visible facade.
//   4. navigate the mapped record's raw root["source"][group]["icon"][icon];
//      only the dynamic group/icon keys have hasKey gates.
//   5. on hit: allocate an ObjSource facade holding the icon raw node and a
//      null lazy texture, then wrap it through the NCB class adaptor. The
//      source structure is the raw-node owner plus lazy texture state; native
//      object sizes remain in analysis/. Port: new ObjSource(iconNode) +
//      CreateAdaptor.
tTJSVariant motion::ResourceManager::findSource(ttstr moduleKey,
                                                ttstr path) const {
    // 1. split name by "/" with detail::splitTtstr_guess.
    const std::vector<ttstr> pieces =
        detail::splitTtstr_guess(path, TJS_W("/"));
    if(pieces[0].IsEmpty()) {
        return {};
    }

    // 2. All four compare the first component with "src", then "blank".
    if(pieces[0] != TJS_W("src")) {
        if(pieces[0] != TJS_W("blank")) {
            return {};
        }

        // The inlined dictionary-accessor path gives each SetValue a fresh
        // temporary Variant and a distinct hint slot, then destroys that
        // Variant immediately. The blank slot itself is also reused by the
        // later MotionNode source-descriptor reader.
        const std::vector<ttstr> dims =
            detail::splitTtstr_guess(pieces[1], TJS_W(":"));
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

    // Both direct vector elements are converted before the module-map lookup.
    // Malformed paths therefore retain the common unchecked indexing boundary
    // even when moduleKey is absent.
    const std::string groupKey = detail::narrow(pieces[1]);
    const std::string iconKey = detail::narrow(pieces[2]);

    // 3. The module map is keyed directly by moduleKey. find()==end()
    // represents the null bucket/node result; no reference checks the mapped
    // PSB owner a second time.
    const auto recordIt = _loadedModules.find(moduleKey);
    if(recordIt == _loadedModules.end()) {
        return {};
    }
    detail::LoadedResourceRecord *record = &recordIt->second;

    // 4. Raw root navigation is strict for fixed keys. ContainsDictionaryKey
    // gates only the two dynamic keys before their strict reads.
    const PSB::PSBRawNode root(record->file);
    const PSB::PSBRawNode source = root.GetDictionaryValueStrict("source");
    if(!source.ContainsDictionaryKey(groupKey.c_str())) {
        return {};
    }
    // The group node is a full-expression temporary destroyed immediately
    // after constructing iconHolder, before the icon gate, in all four builds.
    const PSB::PSBRawNode iconHolder =
        source.GetDictionaryValueStrict(groupKey.c_str())
            .GetDictionaryValueStrict("icon");
    if(!iconHolder.ContainsDictionaryKey(iconKey.c_str())) {
        return {};
    }
    const PSB::PSBRawNode iconEntry =
        iconHolder.GetDictionaryValueStrict(iconKey.c_str());

    // 5. Copy the raw pair into ObjSource (owner AddRef), zero its texture,
    // then attach it to the NCB native instance. CreateAdaptor uses
    // sticky=false, so a compatible adaptor deletes ObjSource on invalidation.
    // Before that attachment there is deliberately no native-object guard:
    // a missing class/CreateNew failure returns Void and leaks src plus its
    // retained PSB owner; an incompatible adaptor still returns its script
    // object but also leaves the same native state unattached and leaked. The
    // four references preserve both boundaries.
    using ObjSourceAdaptor = ncbInstanceAdaptor<motion::ObjSource>;
    motion::ObjSource *src = new motion::ObjSource(iconEntry);
    if(iTJSDispatch2 *dispatch = ObjSourceAdaptor::CreateAdaptor(src)) {
        tTJSVariant result(dispatch, dispatch);
        dispatch->Release();
        return result;
    }
    // A null adaptor does not reclaim the newly allocated ObjSource.
    return {};
}

tjs_int motion::ResourceManager::requireLayerId() {
    // The unsigned counter advances until it reaches an unoccupied key, then
    // inserts that key before incrementing again. Arithmetic deliberately
    // wraps at 2^32; sentinel zero is skipped only while it remains in the set.
    while(_usedLayerIds.find(_nextLayerId) != _usedLayerIds.end()) {
        ++_nextLayerId;
    }
    _usedLayerIds.insert(_nextLayerId);
    return static_cast<tjs_int>(_nextLayerId++);
}

tjs_int motion::ResourceManager::releaseLayerId(tjs_int id) {
    // The four callbacks find the exact unsigned key and, on a hit, erase the
    // complete suffix rather than merely that node. A miss erases the empty
    // [end,end) range. release(0) clears the sentinel and all allocations; the
    // counter is never rewound. The NCB invoker publishes the signed 32-bit
    // number of erased nodes as a TJS Integer.
    const auto first = _usedLayerIds.find(static_cast<tjs_uint32>(id));
    const auto erased = std::distance(first, _usedLayerIds.end());
    _usedLayerIds.erase(first, _usedLayerIds.end());
    return static_cast<tjs_int>(erased);
}

// bufLayer is likewise inherited: both class tables use the exact same getter
// callback and a null setter, so there is one persistent SourceCache field.

void motion::ResourceManager::unloadAll() const {
    // Every current callback clears only the module map.
    _loadedModules.clear();
}

bool motion::ResourceManager::isExistMotion(tTJSVariant projectKey,
                                            ttstr path) const {
    const std::vector<ttstr> pieces =
        detail::splitTtstr_guess(path, TJS_W("/"));
    const std::string chara = detail::narrow(pieces[1]);
    const std::string motionName = detail::narrow(pieces[2]);
    // The four reference implementations keep the direct module hit and the
    // fallback map walk as separate raw-node paths. Void skips the direct
    // lookup; every other project key must already be String.
    if(projectKey.Type() != tvtVoid) {
        const tTJSVariantString *projectString =
            projectKey.AsStringNoAddRef();
        const auto direct = _loadedModules.find(ttstr(
            projectString
                ? projectString->operator const tjs_char *()
                : nullptr));
        if(direct != _loadedModules.end()) {
            const PSB::PSBRawNode root(direct->second.file);
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
        const PSB::PSBRawNode root(entry.second.file);
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
        detail::splitTtstr_guess(path, TJS_W("/"));
    const std::string chara = detail::narrow(pieces[1]);
    const std::string motionName = detail::narrow(pieces[2]);
    // findMotion mirrors isExistMotion's direct/fallback split. Only the final
    // motion node is wrapped in a dispatch.
    if(projectKey.Type() != tvtVoid) {
        const tTJSVariantString *projectString =
            projectKey.AsStringNoAddRef();
        const auto direct = _loadedModules.find(ttstr(
            projectString
                ? projectString->operator const tjs_char *()
                : nullptr));
        if(direct != _loadedModules.end()) {
            const PSB::PSBRawNode root(direct->second.file);
            const PSB::PSBRawNode objects =
                root.GetDictionaryValueStrict("object");
            if(objects.ContainsDictionaryKey(chara.c_str())) {
                const PSB::PSBRawNode motions =
                    objects.GetDictionaryValueStrict(chara.c_str())
                        .GetDictionaryValueStrict("motion");
                if(motions.ContainsDictionaryKey(motionName.c_str())) {
                    const PSB::PSBRawNode motion =
                        motions.GetDictionaryValueStrict(motionName.c_str());
                    // Preserve the shipped raw handoff: the initial dispatch
                    // reference is unguarded until the first Array element has
                    // retained both Object and ObjThis. Array creation or that
                    // first emplacement throwing after construction therefore
                    // leaks the dispatch; Release happens only after ownership
                    // has transferred successfully.
                    auto *dispatch = new PSB::PSBValueDispatch(
                        motion.GetFile_guess(), motion.GetNode());
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
        const PSB::PSBRawNode root(entry.second.file);
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
        // The fallback hit uses the same intentionally unguarded handoff and
        // exception boundary as the direct hit above.
        auto *dispatch = new PSB::PSBValueDispatch(
            motion.GetFile_guess(), motion.GetNode());
        auto result = detail::createTJSArrayWithItems_guess();
        result.items->emplace_back(dispatch, dispatch);
        dispatch->Release();
        result.items->emplace_back(entry.first);
        return result.value;
    }
    return {};
}

double motion::ResourceManager::random() {
    // All four registrars bind an ordinary no-argument native member returning
    // double. The member ignores FuncCall's status and always applies the TJS
    // Real conversion to the result slot; Void therefore converts to 0.0.
    auto *generator = _randomGenerator.AsObjectNoAddRef();
    tTJSVariant result;
    (void)generator->FuncCall(
        0, TJS_W("random"), &motion::detail::randomMemberHint_guess,
        &result, 0, nullptr, generator);
    return result.AsReal();
}
