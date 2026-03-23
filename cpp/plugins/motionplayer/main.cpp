//
// Created by LiDon on 2025/9/13.
// TODO: implement emoteplayer.dll plugin
//
#include <spdlog/spdlog.h>
#include "tjs.h"
#include "ncbind.hpp"
#include "psbfile/PSBFile.h"

#include "ResourceManager.h"
#include "EmotePlayer.h"
#include "Player.h"
#include "SeparateLayerAdaptor.h"

using namespace motion;

#define NCB_MODULE_NAME TJS_W("motionplayer.dll")
#define LOGGER spdlog::get("plugin")

NCB_REGISTER_SUBCLASS(SeparateLayerAdaptor) { NCB_CONSTRUCTOR(()); }

NCB_REGISTER_SUBCLASS(D3DAdaptor) { NCB_CONSTRUCTOR(()); }

NCB_REGISTER_SUBCLASS(Player) { NCB_CONSTRUCTOR(()); }

NCB_REGISTER_SUBCLASS(EmotePlayer) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *));
    NCB_PROPERTY(useD3D, getUseD3D, setUseD3D);
    NCB_METHOD(getVariable);
    NCB_METHOD(setVariable);
}

NCB_REGISTER_SUBCLASS(ResourceManager) {
    NCB_CONSTRUCTOR((iTJSDispatch2 *, tjs_int));
    NCB_METHOD(load);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptSeed,
                            &ResourceManager::setEmotePSBDecryptSeed,
                            TJS_STATICMEMBER);
    NCB_METHOD_RAW_CALLBACK(setEmotePSBDecryptFunc,
                            &ResourceManager::setEmotePSBDecryptFunc,
                            TJS_STATICMEMBER);
}

class Motion {
public:
    static tjs_error setEnableD3D(tTJSVariant *, tjs_int count, tTJSVariant **p,
                                  iTJSDispatch2 *) {
        if(count == 1 && (*p)->Type() == tvtInteger) {
            _enableD3D = static_cast<bool>(**p);
            return TJS_S_OK;
        }
        return TJS_E_INVALIDPARAM;
    }

    static tjs_error getEnableD3D(tTJSVariant *r, tjs_int, tTJSVariant **,
                                  iTJSDispatch2 *) {
        *r = tTJSVariant{ _enableD3D };
        return TJS_S_OK;
    }

private:
    inline static bool _enableD3D;
};

NCB_REGISTER_CLASS(Motion) {
    Variant("MaskModeAlpha", static_cast<int>(MaskMode::MaskModeAlpha));
    Variant("PlayFlagForce", 1);
    // NOTE: enableD3D is intentionally NOT registered. The APK does not expose it.
    // Game scripts use `typeof Motion.enableD3D` to detect D3D support;
    // returning (int)0 instead of void makes them think D3D is available.
    NCB_SUBCLASS(ResourceManager, ResourceManager);
    NCB_SUBCLASS(Player, Player);
    NCB_SUBCLASS(EmotePlayer, EmotePlayer);
    NCB_SUBCLASS(SeparateLayerAdaptor, SeparateLayerAdaptor);
    NCB_SUBCLASS(D3DAdaptor, D3DAdaptor);
}

static void PreRegistCallback() {}

static void PostUnregistCallback() {}

NCB_PRE_REGIST_CALLBACK(PreRegistCallback);
NCB_POST_UNREGIST_CALLBACK(PostUnregistCallback);
