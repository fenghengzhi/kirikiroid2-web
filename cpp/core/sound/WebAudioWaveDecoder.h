#ifndef WebAudioWaveDecoderH
#define WebAudioWaveDecoderH

#ifdef __EMSCRIPTEN__

#include "tjsCommHead.h"

#include <functional>
#include <memory>
#include <vector>

class tTVPWaveDecoder;

using tTVPWebAudioWaveDecoderCallback =
    std::function<void(tTVPWaveDecoder *)>;

// Web platform boundary for TVPCreateWaveDecoder@0x95FB7C. Android probes its
// registered native decoder creators synchronously. Web first asks the browser
// to decode the already materialized storage bytes; a nullptr result means the
// caller must continue Android's original reverse creator traversal unchanged.
void TVPTryCreateWebAudioWaveDecoderAsync(
    const ttstr &storagename,
    std::shared_ptr<std::vector<tjs_uint8>> encoded,
    tTVPWebAudioWaveDecoderCallback completion);

#endif

#endif
