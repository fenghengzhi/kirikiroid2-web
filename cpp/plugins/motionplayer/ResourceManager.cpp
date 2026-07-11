//
// Created by LiDon on 2025/9/15.
//

#include "ResourceManager.h"
#include "tjsDictionary.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include <spdlog/spdlog.h>

#include "RuntimeSupport.h"
#include "SourceCache.h"
#include "ncbind.hpp"

#define LOGGER spdlog::get("plugin")

namespace {
    std::string lowercase(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });
        return value;
    }

    // Aligned with libkrkr2.so sub_697D34 at 0x697D34: the binary tokenises a
    // ttstr by a single-char separator into a vector of ttstr pieces (each piece
    // empty-string for an empty span). The "src"/"blank" prefix gate and the
    // group/icon keys in findSource @0x6AAB3C are read out of this vector. The
    // separator ttstr (L"/" / L":") is created via ttstr_createFromWide and the
    // pieces are produced by repeated sub_A0CBEC (find) + sub_A0CA58 (substr).
    std::vector<ttstr> splitTtstr(const ttstr &input, tjs_char separator) {
        std::vector<ttstr> pieces;
        const tjs_char *p = input.c_str();
        if(!p) {
            // Mirrors sub_697D34's terminal push of the whole (empty) remainder.
            pieces.emplace_back();
            return pieces;
        }
        const tjs_char *start = p;
        for(;; ++p) {
            if(*p == separator || *p == 0) {
                pieces.emplace_back(start, static_cast<size_t>(p - start));
                if(*p == 0) {
                    break;
                }
                start = p + 1;
            }
        }
        return pieces;
    }
}

// C-1 (2026-06-07): RM : public SourceCache. The implicit SourceCache base
//   subobject ctor (`SourceCache::SourceCache() = default`) runs before the RM
//   body — mirroring binary RM ctor sub_6A88CC @0x6A88CC which calls the
//   SourceCache base ctor sub_6A78F4 FIRST (0x6a88f8), then initialises the
//   RM-own fields. GAP (oracle-inert, honest): the binary base ctor takes
//   (this, rmDispatch, layerType=0) and seeds the base _owner / +40 bufLayer
//   Layer from the RM dispatch; the local default base ctor still leaves _owner /
//   _bufLayer empty until the first native render call supplies its layer owner.
//   Player now aliases this inherited SourceCache directly (Player_ctor
//   @0x6CED30); the remaining difference is construction-time owner/bufLayer
//   materialisation, not SourceCache object identity or cache-container lifetime.
motion::ResourceManager::ResourceManager() : _state(std::make_shared<State>()) {}

motion::ResourceManager::ResourceManager(iTJSDispatch2 *kag,
                                         tjs_int cacheSize) :
    _state(std::make_shared<State>()) {
    LOGGER->info("kag: {}, cacheSize: {}", static_cast<void *>(kag), cacheSize);

    // Pre-define ShortCutInitialPadKeyMap on the KAG window if not already set.
    // The encrypted keybinder.tjs accesses .ShortCutInitialPadKeyMap on the
    // window object. If undefined, it crashes with "Invalid object context".
    if(kag) {
        const tjs_char *padKeys[] = {
            TJS_W("ShortCutInitialPadKeyMap"),
            TJS_W("ShortCutInitialGamePadKeyMap"),
            TJS_W("_proceedingKeyList"),
            nullptr
        };
        for(int i = 0; padKeys[i]; ++i) {
            tTJSVariant existing;
            if(TJS_FAILED(kag->PropGet(0, padKeys[i], nullptr, &existing, kag)) ||
               existing.Type() == tvtVoid) {
                iTJSDispatch2 *dict = TJSCreateDictionaryObject();
                if(dict) {
                    tTJSVariant v(dict, dict);
                    kag->PropSet(TJS_MEMBERENSURE, padKeys[i], nullptr,
                                 &v, kag);
                    dict->Release();
                }
            }
        }
    }
}

tjs_int motion::ResourceManager::getEmotePSBDecryptSeed() {
    return _decryptSeed;
}

tjs_error motion::ResourceManager::setEmotePSBDecryptSeed(tTJSVariant *,
                                                          tjs_int count,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *) {
    if(count != 1) {
        return TJS_E_BADPARAMCOUNT;
    }
    if((*p)->Type() != tvtInteger) {
        return TJS_E_INVALIDPARAM;
    }
    _decryptSeed = static_cast<tjs_int>(*p[0]);
    LOGGER->info("setEmotePSBDecryptSeed: {}", _decryptSeed);
    return TJS_S_OK;
}

tjs_error motion::ResourceManager::setEmotePSBDecryptFunc(tTJSVariant *r,
                                                          tjs_int n,
                                                          tTJSVariant **p,
                                                          iTJSDispatch2 *obj) {
    LOGGER->critical("setEmotePSBDecryptFunc no implement!");
    return TJS_S_OK;
}

tTJSVariant motion::ResourceManager::load(ttstr path) const {
    // ResourceManager_loadResource @0x6A8D8C first replaces its input with
    // TVPGetPlacedPath(path), then uses that exact normalized ttstr for both
    // HashMap-A lookup and insertion.
    const ttstr placedPath = TVPGetPlacedPath(path);
    if(!placedPath.IsEmpty()) {
        path = placedPath;
    }
    const auto rawPath = path.AsStdString();
    const auto loweredPath = lowercase(rawPath);
    if(loweredPath.find(".mtn") != std::string::npos) {
        LOGGER->warn("Motion resource manager load: {}", rawPath);
    }
    const auto loaded = detail::loadPSBVariant(path, _decryptSeed);
    if(loaded.Type() != tvtVoid && _state) {
        // P3-A: key by the RAW PATH ttstr, matching binary findOrInsert
        // sub_6EB9E4 @0x6EB9E4 (this+88 HashMap A keyed by the un-folded path
        // ttstr). rawPath here was already the raw, un-lowercased path; the
        // ttstr `path` is the identical key, now stored directly.
        _state->loadedModules[path] = loaded;
        _state->lastLoadedPath = path;
        _state->lastLoadedModule = loaded;
    }
    return loaded;
}

// C-1 (2026-06-07): RM-own loadSource(ttstr)->load(path) forward REMOVED. The
//   binary RM `loadSource` NCB member (sub_6A7BA8) is the INHERITED
//   SourceCache::loadSource(keyOrSource, currentSource) base method (the RM
//   registrar @0x6AB8BC re-lists the SAME callback address sub_6A7BA8 that the
//   SourceCache registrar @0x6A85A8 binds). It materialises a Layer into the
//   SourceCache base +72 list — it is NOT a thin forward to RM::load. The
//   inherited SourceCache::loadSource now serves the RM NCB binding.

void motion::ResourceManager::unload(ttstr path) const {
    LOGGER->debug("ResourceManager::unload({})", path.AsStdString());
    if(!_state) {
        return;
    }

    // P3-A: erase by the RAW PATH ttstr key (HashMap A is case-sensitive
    // wcscmp-keyed, sub_9B1ED0 @0x9B1ED0).
    _state->loadedModules.erase(path);
    if(_state->lastLoadedPath == path) {
        _state->lastLoadedPath.Clear();
        _state->lastLoadedModule.Clear();
    }
}

// C-1 (2026-06-07): RM-own clearCache() const REMOVED. The binary RM
//   `clearCache` NCB member (sub_6A8438) is the INHERITED
//   SourceCache::clearCache() base method (RM registrar @0x6AB8BC re-lists the
//   SAME callback address sub_6A8438 the SourceCache registrar @0x6A85A8 binds).
//   sub_6A8438 touches ONLY the SourceCache base +72 layer-list (releases each
//   Layer image via vtable+112, frees nodes, resets +72/+80 sentinels and +60=0);
//   it does NOT clear HashMap A / lastLoaded / the layer-id set — the prior
//   RM-own body that cleared _state->loadedModules/lastLoaded was a documented
//   deviation (see old NOTE), now correctly dropped: the inherited
//   SourceCache::clearCache() serves the RM NCB binding faithfully. The module
//   cache lifetime is governed by load/unload/unloadAll, not clearCache.

tTJSVariant motion::ResourceManager::getLastLoadedModule() const {
    return _state ? _state->lastLoadedModule : tTJSVariant{};
}

tTJSVariant motion::ResourceManager::findLoaded(ttstr path) const {
    if(!_state) {
        return {};
    }

    // P3-A: lookup by the RAW PATH ttstr key via the binary HashMap A functor
    // (ttstr_hash == FNV @0x6eba2c, ttstr_equal == wcscmp @0x9B1ED0).
    const auto it = _state->loadedModules.find(path);
    return it != _state->loadedModules.end() ? it->second : tTJSVariant{};
}

namespace {
    // PropGet helper mirroring the binary PSB dict member access sub_598C58
    // @0x598C58 (member-by-key) on a TJS dictionary `tTJSVariant`. Returns false
    // when the holder is not an object or the key is absent — equivalent to the
    // sub_5995D8 @0x5995D8 hasKey gate (the binary aborts the chain on a miss).
    bool psbGet(const tTJSVariant &holder, const tjs_char *key,
                tTJSVariant &out) {
        if(holder.Type() != tvtObject) {
            return false;
        }
        iTJSDispatch2 *obj = holder.AsObjectNoAddRef();
        if(!obj) {
            return false;
        }
        tTJSVariant v;
        if(TJS_FAILED(obj->PropGet(0, key, nullptr, &v, obj)) ||
           v.Type() == tvtVoid) {
            return false;
        }
        out = v;
        return true;
    }
}

// Aligned with libkrkr2.so ResourceManager::findSource (sub_6AAB3C) at 0x6AAB3C.
// The binary:
//   1. split path by "/" (sub_697D34); empty -> result void (LABEL_11).
//   2. if pieces[0] != "src" (sub_9B1ED0): if "blank" build a blank-Layer dict
//      (width/height/originX/originY from pieces[1] split by ":" + blank=1),
//      else result void.
//   3. for "src": HashMap A (this+88 buckets / this+96 count) lookup keyed by
//      moduleKey (a2, FNV hash cached in ttstr+68 via sub_6EB8F4). The requested
//      source path is the separate a3 argument. Player_findSource @0x6948E8
//      supplies Player+1012 as moduleKey and the resolved src path as a3.
//      Player_playImpl @0x6B2284 fills +1012 from findMotion result[1], which
//      ResourceManager_findMotion @0x6A9ED4 copies from the matched map key.
//   4. navigate module["source"][group]["icon"][icon] with per-level hasKey
//      gates (sub_598C58 / sub_5995D8); miss at any level -> result void.
//   5. on hit: operator new(0x18) ObjSource facade holding the icon sub-dict
//      (qword[0]=dict variant, [1]=?, [2]=0), wrapped as a TJS object via the
//      NCB class object (sub_6EC124). Port: new ObjSource(iconEntry) +
//      ncbInstanceAdaptor<ObjSource>::CreateAdaptor.
tTJSVariant motion::ResourceManager::findSource(ttstr moduleKey,
                                                ttstr path) const {
    // 1. split name by "/" (sub_697D34 @0x697D34).
    const std::vector<ttstr> pieces = splitTtstr(path, TJS_W('/'));
    if(pieces.empty() || pieces[0].IsEmpty()) {
        return {}; // LABEL_11: *(a4+16)=0 -> void
    }

    // 2. prefix gate: "src" (sub_9B1ED0 @0x9B1ED0 == 0 means equal).
    if(pieces[0] != ttstr(TJS_W("src"))) {
        if(pieces[0] != ttstr(TJS_W("blank"))) {
            return {}; // LABEL_11
        }
        // blank branch (@0x6aac74): split pieces[1] by ":" into
        // width/height/originX/originY ints, build a blank-Layer dict + blank=1.
        const ttstr blankSpec = pieces.size() > 1 ? pieces[1] : ttstr();
        const std::vector<ttstr> dims = splitTtstr(blankSpec, TJS_W(':'));
        const auto dimInt = [&dims](std::size_t i) -> tjs_int {
            if(i >= dims.size() || dims[i].IsEmpty()) {
                return 0;
            }
            return static_cast<tjs_int>(tTJSVariant(dims[i]));
        };
        return detail::makeDictionary({
            { "width", tTJSVariant(dimInt(0)) },    // L"width"  @0x6aad0c
            { "height", tTJSVariant(dimInt(1)) },   // L"height" @0x6aad54
            { "originX", tTJSVariant(dimInt(2)) },  // L"originX"@0x6aad9c
            { "originY", tTJSVariant(dimInt(3)) },  // L"originY"@0x6aade4
            { "blank", tTJSVariant(static_cast<tjs_int>(1)) }, // L"blank" @0x6aae40
        });
    }

    // 3. HashMap A lookup keyed by moduleKey (a2), while path (a3) remains the
    // source descriptor key. Port equivalent: loaded-module registry lookup by
    // the same motion/project key used by ResourceManager::load.
    const tTJSVariant module = findLoaded(moduleKey);
    if(module.Type() != tvtObject) {
        return {}; // !v27 || !*v27 -> LABEL_71 result void
    }

    // 4. module["source"][group]["icon"][icon] with per-level hasKey gates.
    const ttstr group = pieces.size() > 1 ? pieces[1] : ttstr();
    const ttstr icon = pieces.size() > 2 ? pieces[2] : ttstr();

    tTJSVariant sourceDict;
    if(!psbGet(module, TJS_W("source"), sourceDict)) {
        return {};
    }
    tTJSVariant groupDict;
    if(!psbGet(sourceDict, group.c_str(), groupDict)) { // sub_5995D8 gate
        return {}; // LABEL_64 -> result void
    }
    tTJSVariant iconHolder;
    if(!psbGet(groupDict, TJS_W("icon"), iconHolder)) {
        return {};
    }
    tTJSVariant iconEntry;
    if(!psbGet(iconHolder, icon.c_str(), iconEntry)) { // sub_5995D8 gate
        return {};
    }

    // 5. construct the ObjSource dict facade (operator new(0x18) + sub_6EC124).
    using ObjSourceAdaptor = ncbInstanceAdaptor<motion::ObjSource>;
    motion::ObjSource *src = new motion::ObjSource(iconEntry);
    if(iTJSDispatch2 *dispatch = ObjSourceAdaptor::CreateAdaptor(src)) {
        tTJSVariant result(dispatch, dispatch);
        dispatch->Release();
        return result;
    }
    delete src;
    return {};
}

tjs_int motion::ResourceManager::requireLayerId() {
    if(!_state) {
        return 0;
    }

    // Aligned with sub_6AB694 @0x6AB694. Binary topology (cross-verified by fresh
    // decompile 2026-06-06, disasm 0x6ab694-0x6ab74c):
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
    // The only ctor-level nuance — binary pre-inserts {0} into the set (0x6a8a08)
    // — is functionally inert (counter starts at 1 so 0 is never returned, and
    // unloadAll's _M_erase clears it) and lives in the RM ctor, not here; the
    // port's default-constructed empty set is faithful, inserting a {0} sentinel
    // would be a port-invention.
    while(_state->usedLayerIds.find(_state->nextLayerId) !=
          _state->usedLayerIds.end()) {
        ++_state->nextLayerId;
    }
    const auto id = _state->nextLayerId;
    _state->usedLayerIds.insert(id);
    ++_state->nextLayerId;
    return id;
}

// P3-B (2026-06-05): releaseLayerId aligned to sub_6AB750 @0x6AB750 —
//   erase the id from the std::set<unsigned int> @+168 and nothing else. The
//   binary keeps NO name<->id maps (requireLayerIdForName removed, its string
//   has 0 hits in libkrkr2.so); the by-name cleanup that used to live here is
//   gone.
void motion::ResourceManager::releaseLayerId(tjs_int id) {
    if(!_state || id == 0) {
        return;
    }
    _state->usedLayerIds.erase(id);
}

// --- M9 brick B: binary ResourceManager members missing from the port surface
// (ncb_registerMembers @0x6AB8BC). See ResourceManager.h for the per-member
// fidelity notes; faithful where _state maps cleanly, STUB (with addr) where the
// real body needs the HashMap A / motion-list topology parked behind phase D. ---

// C-1 (2026-06-07): RM-own getBufLayer()->ttstr REMOVED. The binary RM
//   `bufLayer` prop-ro (sub_6A84FC) reads `a1+40` = the SourceCache base
//   bufLayer LAYER VARIANT (set by the SourceCache base ctor sub_6A78F4 via
//   sub_A0FB64(a1+40, newLayer)), NOT a ttstr name. The RM registrar @0x6AB8BC
//   re-lists the SAME callback address sub_6A84FC the SourceCache registrar
//   @0x6A85A8 binds. So the inherited SourceCache::getBufLayer() (returns the
//   base tTJSVariant _bufLayer) now serves the RM NCB binding faithfully; the
//   former RM ttstr getBufLayer() was a misattributed two-class artifact.

void motion::ResourceManager::unloadAll() const {
    // unloadAll @0x6A8BBC clears every RM container (layer-list +72, HashMap A
    // +88, motion-list +104, layerId Rb_tree +168, bufLayer +144). Port: clear
    // the _state caches (the live PSB/module + layerId backing). Distinct from
    // clearCache @0x6A8438, which in the binary clears only the +72 layer-list.
    LOGGER->debug("ResourceManager::unloadAll()");
    if(!_state) {
        return;
    }
    _state->loadedModules.clear();
    _state->lastLoadedPath.Clear();
    _state->lastLoadedModule.Clear();
    // P3-B (2026-06-05): unloadAll@0x6A8BBC DOES clear the layer-id set
    //   (`std::_Rb_tree<unsigned int>::_M_erase(a1+168, ...)` @0x6a8c04) — keep
    //   usedLayerIds.clear(). But it does NOT reset the next-id counter (+216 is
    //   never written in the function) — removed the non-faithful nextLayerId
    //   reset so the counter stays monotonic across unloadAll, matching binary.
    _state->usedLayerIds.clear();
}

bool motion::ResourceManager::isExistMotion(ttstr name) const {
    // isExistMotion @0x6A96F8 walks HashMap A (+88) by motion name then the +104
    // motion-list, returning whether dict["object"][name]["motion"] exists. The
    // port's _state->loadedModules is keyed by load PATH, not motion name, so it
    // is NOT a faithful substitute (CLAUDE.md: do not infer semantics from a
    // similar name) — STUB returns false. Real body deferred with the HashMap A /
    // motion-list topology.
    LOGGER->warn("ResourceManager::isExistMotion() stub called: {}",
                 name.AsStdString());
    return false;
}

tTJSVariant motion::ResourceManager::findMotion(ttstr name) const {
    // findMotion @0x6A9ED4 returns the motion-label array from
    // dict["object"][name]["motion"] via HashMap A (+88) / +104 list. No port
    // _state equivalent — STUB returns void. Real body deferred (HashMap A
    // topology).
    LOGGER->warn("ResourceManager::findMotion() stub called: {}",
                 name.AsStdString());
    return {};
}

tjs_error motion::ResourceManager::random(tTJSVariant *r, tjs_int,
                                          tTJSVariant **, iTJSDispatch2 *) {
    // random @0x6AB56C forwards a "random" PropGet to the KAG window object — a
    // host concern unrelated to source caching. STUB: no-op void.
    LOGGER->warn("ResourceManager::random() stub called");
    if(r) {
        r->Clear();
    }
    return TJS_S_OK;
}
