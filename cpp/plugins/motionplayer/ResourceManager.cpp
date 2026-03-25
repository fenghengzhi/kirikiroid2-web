//
// Created by LiDon on 2025/9/15.
//

#include "ResourceManager.h"

#include <spdlog/spdlog.h>

#include "RuntimeSupport.h"

#define LOGGER spdlog::get("plugin")

motion::ResourceManager::ResourceManager(iTJSDispatch2 *kag,
                                         tjs_int cacheSize) {
    LOGGER->info("kag: {}, cacheSize: {}", static_cast<void *>(kag), cacheSize);
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
    return detail::loadPSBVariant(path, _decryptSeed);
}
