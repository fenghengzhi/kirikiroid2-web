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

tTVPJPEGLoadPrecision TVPJPEGLoadPrecision = jlpMedium;


void TVPLoadJPEG(void *formatdata, void *callbackdata,
                 tTVPGraphicSizeCallback sizecallback,
                 tTVPGraphicScanLineCallback scanlinecallback,
                 tTVPMetaInfoPushCallback metainfopushcallback,
                 tTJSBinaryStream *src, tjs_int keyidx,
                 tTVPGraphicLoadMode mode) {}

void TVPLoadBPG(void *formatdata, void *callbackdata,
                tTVPGraphicSizeCallback sizecallback,
                tTVPGraphicScanLineCallback scanlinecallback,
                tTVPMetaInfoPushCallback metainfopushcallback,
                tTJSBinaryStream *src, tjs_int keyidx,
                tTVPGraphicLoadMode mode) {}

void TVPLoadWEBP(void *formatdata, void *callbackdata,
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

void TVPLoadHeaderJPG(void *formatdata, tTJSBinaryStream *src,
                      iTJSDispatch2 **dic) {}

void TVPLoadHeaderBPG(void *formatdata, tTJSBinaryStream *src,
                      iTJSDispatch2 **dic) {}

void TVPLoadHeaderWEBP(void *formatdata, tTJSBinaryStream *src,
                       iTJSDispatch2 **dic) {}

void TVPLoadHeaderJXR(void *formatdata, tTJSBinaryStream *src,
                      iTJSDispatch2 **dic) {}

void TVPSaveAsJPG(void *formatdata, tTJSBinaryStream *dst,
                  const iTVPBaseBitmap *image, const ttstr &mode,
                  iTJSDispatch2 *meta) {}

void TVPSaveAsJXR(void *formatdata, tTJSBinaryStream *dst,
                  const iTVPBaseBitmap *image, const ttstr &mode,
                  iTJSDispatch2 *meta) {}

bool TVPAcceptSaveAsJPG(void *formatdata, const ttstr &type,
                        iTJSDispatch2 **dic) {
    return false;
}

bool TVPAcceptSaveAsJXR(void *formatdata, const ttstr &type,
                        iTJSDispatch2 **dic) {
    return false;
}

class tTJSNI_VideoOverlay;
class iTVPVideoOverlay;

void GetVideoLayerObject(tTJSNI_VideoOverlay *callbackwin,
                         struct IStream *stream,
                         const tjs_char *streamname,
                         const tjs_char *type, uint64_t size,
                         iTVPVideoOverlay **out) {
    if (out) *out = nullptr;
}

void GetMixingVideoOverlayObject(tTJSNI_VideoOverlay *callbackwin,
                                 struct IStream *stream,
                                 const tjs_char *streamname,
                                 const tjs_char *type, uint64_t size,
                                 iTVPVideoOverlay **out) {
    if (out) *out = nullptr;
}

void GetMFVideoOverlayObject(tTJSNI_VideoOverlay *callbackwin,
                             struct IStream *stream,
                             const tjs_char *streamname,
                             const tjs_char *type, uint64_t size,
                             iTVPVideoOverlay **out) {
    if (out) *out = nullptr;
}

void GetVideoOverlayObject(tTJSNI_VideoOverlay *callbackwin,
                           struct IStream *stream,
                           const tjs_char *streamname,
                           const tjs_char *type, uint64_t size,
                           iTVPVideoOverlay **out) {
    if (out) *out = nullptr;
}

#endif
