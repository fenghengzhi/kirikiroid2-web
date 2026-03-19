#ifdef __EMSCRIPTEN__

#include "tjsCommHead.h"
#include "tjsTypes.h"
#include "StorageIntf.h"
#include "GraphicsLoaderIntf.h"
#include "WaveIntf.h"
#include "combase.h"

extern "C" const IID IID_IUnknown = {0, 0, 0, {0}};

tTVPArchive *TVPOpen7ZArchive(const ttstr &name, tTJSBinaryStream *st,
                              bool normalizeFileName) {
    return nullptr;
}

void TVPLoadBPG(void *formatdata, void *callbackdata,
                tTVPGraphicSizeCallback sizecallback,
                tTVPGraphicScanLineCallback scanlinecallback,
                tTVPMetaInfoPushCallback metainfopushcallback,
                tTJSBinaryStream *src, tjs_int keyidx,
                tTVPGraphicLoadMode mode) {}

void TVPLoadJXR(void *formatdata, void *callbackdata,
                tTVPGraphicSizeCallback sizecallback,
                tTVPGraphicScanLineCallback scanlinecallback,
                tTVPMetaInfoPushCallback metainfopushcallback,
                tTJSBinaryStream *src, tjs_int keyidx,
                tTVPGraphicLoadMode mode) {}

void TVPLoadHeaderBPG(void *formatdata, tTJSBinaryStream *src,
                      iTJSDispatch2 **dic) {}

void TVPLoadHeaderJXR(void *formatdata, tTJSBinaryStream *src,
                      iTJSDispatch2 **dic) {}

void TVPSaveAsJXR(void *formatdata, tTJSBinaryStream *dst,
                  const iTVPBaseBitmap *image, const ttstr &mode,
                  iTJSDispatch2 *meta) {}

bool TVPAcceptSaveAsJXR(void *formatdata, const ttstr &type,
                        iTJSDispatch2 **dic) {
    return false;
}

#endif
