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
        // Current mapped-record construction is 6E8DC4->6E8EEC->6E90DC,
        // 5A7488->5A751C->5A762C, inline in 100101798, and inline in FE940.
        // Each path initializes the retained PSB owner first, then asks its STL
        // policy for ten buckets independently for both nested maps.
        winSourceTextures.rehash(10);
        krkrSourceEntries.rehash(10);
    }
} // namespace motion::detail

namespace {
    void initializeRandomGenerator(tTJSVariant &generator) {
        TVPExecuteExpression(TJS_W("new Math.RandomGenerator()"), &generator);
    }

    // Process-wide owner filter; static destruction releases its target.
    PSB::PSBFile::OwnerFilter emotePSBDecryptFilter;

    void replaceEmotePSBDecryptFilter_guess(
        const PSB::PSBFile::OwnerFilter &filter) {
        // Assignment copy-constructs the replacement target and destroys the
        // former target after the swap.
        emotePSBDecryptFilter = filter;
    }

    PSB::PSBFile::OwnerFilter makeEmotePSBDecryptSeedFilter(
        tjs_int64 seed) {
        // The current seed callbacks are 683110/564EC0/1001B8D68/1B83AC;
        // their filter invokers are 6837AC/56522E/1001B92E8/1B8992. All four
        // decrypt [header.encryptData, header.chunkOffsets) with the same
        // four-word xorshift stream, seeded by the low word of the captured
        // 64-bit TJS Integer.
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

    // The current function callbacks 683240/564F58/1001B8E50/1B84D0 retain
    // exactly the Object and ObjThis dispatch pointers in a 0x10/0x08-byte
    // closure (64/32-bit). Final control-block release releases both pointers.
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
            // Current invokers 683994/5652C0/1001B94A8/1B8AB0 construct the
            // same CBinaryAccessor shape, create {object,size} variants, call
            // the closure with two arguments, and ignore its result.
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
    // The native map is initialized with ten buckets before the generator.
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
    // Clear the module map in the derived destructor body before automatic
    // member and SourceCache-base teardown.
    _loadedModules.clear();
}

tjs_error motion::ResourceManager::setEmotePSBDecryptSeed(tTJSVariant *,
                                                          tjs_int count,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *) {
    // 683110/564EC0/1001B8D68/1B83AC accept extra arguments and apply ordinary
    // TJS integer conversion only to p[0] before installing the seed filter.
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
    // 683240/564F58/1001B8E50/1B84D0 accept extra arguments and convert only
    // p[0] to an object closure. The process-wide replacement helpers are
    // 6A5BB0/57B174/1001010B0/FE1E0.
    if(count < 1) {
        return TJS_E_BADPARAMCOUNT;
    }
    const auto filter = makeEmotePSBDecryptFuncFilter(*p[0]);
    replaceEmotePSBDecryptFilter_guess(filter);
    return TJS_S_OK;
}

tTJSVariant motion::ResourceManager::load(ttstr path) {
    // The four current load/cache boundaries are:
    // Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_6A616C,
    // Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_57B338,
    // Kirikiroid2_1.3.9_iOS_arm64!sub_1001012D8, and
    // Kirikiroid2_1.3.9_iOS_armv7!sub_FE40C. All four first replace the input
    // with TVPGetPlacedPath(path), then use that exact ttstr for lookup and
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

// The four ResourceManager registrars re-list SourceCache::loadSource at
// 6A4F88/57ACC8/1001009AC/FDB50. It is inherited `(source, descriptor)` cache
// materialization, not a by-name forward to ResourceManager::load.

void motion::ResourceManager::unload(ttstr path) const {
    LOGGER->debug("ResourceManager::unload({})", path.AsStdString());
    // Current unload callbacks 6A697C/57B6F8/100101A28/FEC04 normalize before
    // performing the same case-sensitive map lookup used by load.
    path = TVPGetPlacedPath(path);
    _loadedModules.erase(path);
}

// The four ResourceManager registrars also re-list inherited clearCache at
// 6A5818/57B018/100100F10/FE0D4. It clears only SourceCache's Layer list;
// load/unload/unloadAll govern the PSB module map.

// The four current icon/source lookup boundaries are:
// Kirikiroid2_1.3.9_Android_arm64-v8a.so!sub_6A7F1C,
// Kirikiroid2_1.3.9_Android_armabi-v7a.so!sub_57BDE0,
// Kirikiroid2_1.3.9_iOS_arm64!sub_100102594, and
// Kirikiroid2_1.3.9_iOS_armv7!sub_FF890. Their shared flow is:
//   1. split path by "/" (detail::splitTtstr_guess); an empty first element
//      produces a void result.
//      The helper always appends its final remainder, so no vector-size gate
//      exists here or at the direct pieces[1]/pieces[2] consumers.
//   2. if pieces[0] != "src": if "blank", split pieces[1] by
//      ":" and write width/height/originX/originY as String Variants plus
//      Integer blank=1 through ncbDictionaryAccessor::SetValue; otherwise void.
//   3. for "src": look up the module map by moduleKey. The requested source
//      path is the separate argument. Player_findSource
//      Player::findSourceForNode_guess supplies Player+1012 as moduleKey and
//      the resolved src path as
//      the path argument; Player's play path fills that key from the matched
//      module-map entry.
//   4. navigate the mapped record's raw root["source"][group]["icon"][icon];
//      only the dynamic group/icon keys have hasKey gates.
//   5. on hit: allocate an ObjSource facade holding the icon raw node and a
//      null lazy texture, then wrap it through the NCB class adaptor. The
//      object is 0x18 bytes on both 64-bit references and 0x0c bytes on both
//      32-bit references; those sizes are ABI consequences, not source layout
//      constants. Port: new ObjSource(iconNode) + CreateAdaptor.
tTJSVariant motion::ResourceManager::findSource(ttstr moduleKey,
                                                ttstr path) const {
    // 1. split name by "/" with detail::splitTtstr_guess.
    const std::vector<ttstr> pieces =
        detail::splitTtstr_guess(path, TJS_W("/"));
    if(pieces[0].IsEmpty()) {
        return {}; // LABEL_11: *(a4+16)=0 -> void
    }

    // 2. All four compare the first component with "src", then "blank".
    if(pieces[0] != TJS_W("src")) {
        if(pieces[0] != TJS_W("blank")) {
            return {}; // LABEL_11
        }

        // The inlined dictionary-accessor path gives each SetValue a fresh
        // temporary Variant and a distinct hint slot, then destroys that
        // Variant immediately.
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
        return {}; // !v27 || !*v27 -> LABEL_71 result void
    }
    detail::LoadedResourceRecord *record = &recordIt->second;

    // 4. Raw root navigation is strict for fixed keys. ContainsDictionaryKey
    // gates only the two dynamic keys before their strict reads.
    const PSB::PSBRawNode root(record->file);
    const PSB::PSBRawNode source = root.GetDictionaryValueStrict("source");
    if(!source.ContainsDictionaryKey(groupKey.c_str())) {
        return {}; // LABEL_64 -> result void
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
    // then attach it to the NCB native instance.
    using ObjSourceAdaptor = ncbInstanceAdaptor<motion::ObjSource>;
    motion::ObjSource *src = new motion::ObjSource(iconEntry);
    if(iTJSDispatch2 *dispatch = ObjSourceAdaptor::CreateAdaptor(src)) {
        tTJSVariant result(dispatch, dispatch);
        dispatch->Release();
        return result;
    }
    // The null-adaptor branch returns void without reclaiming the newly
    // allocated ObjSource in all four references.
    return {};
}

tjs_int motion::ResourceManager::requireLayerId() {
    // Current callbacks: 6A8A74/57C258/100102D40/100240. The counter is
    // monotone, skips occupied ids, inserts the selected id, and never rewinds
    // when an id is released. Sentinel 0 is never returned.
    while(_usedLayerIds.find(_nextLayerId) != _usedLayerIds.end()) {
        ++_nextLayerId;
    }
    const auto id = _nextLayerId;
    _usedLayerIds.insert(id);
    ++_nextLayerId;
    return id;
}

// Current callbacks: 6A8B30/57C2C8/100102DB8/10028A. Only the id-set entry is
// erased; id 0 remains reserved.
void motion::ResourceManager::releaseLayerId(tjs_int id) {
    if(id == 0) {
        return;
    }
    _usedLayerIds.erase(id);
}

// bufLayer is likewise inherited: the current callbacks are
// 6A58DC/57B060/100100F84/FE11A in both SourceCache and ResourceManager tables.

void motion::ResourceManager::unloadAll() const {
    // Current callbacks 6A60D8/57B32C/1001012CC/FE3FE clear only the module map.
    LOGGER->debug("ResourceManager::unloadAll()");
    _loadedModules.clear();
}

bool motion::ResourceManager::isExistMotion(tTJSVariant projectKey,
                                            ttstr path) const {
    const std::vector<ttstr> pieces =
        detail::splitTtstr_guess(path, TJS_W("/"));
    const std::string chara = detail::narrow(pieces[1]);
    const std::string motionName = detail::narrow(pieces[2]);
    // Current callbacks 6A6AD8/57B780/100101AC8/FECF4 keep the direct module
    // hit and fallback map walk as separate raw-node paths. Void skips the
    // direct lookup; every other project key must already be String.
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
    // Current callbacks 6A72B4/57B9F8/100101E84/FF11C mirror isExistMotion's
    // direct/fallback split. Only the final motion node is wrapped in a dispatch.
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

tjs_error motion::ResourceManager::random(tTJSVariant *r, tjs_int,
                                          tTJSVariant **,
                                          iTJSDispatch2 *objthis) {
    // Current callbacks 6A894C/57C1CC/100102C90/1000F0 call the retained
    // generator with no arguments and numerically convert a non-void result.
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
