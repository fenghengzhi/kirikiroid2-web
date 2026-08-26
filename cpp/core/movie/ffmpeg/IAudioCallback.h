#pragma once

#include "KRMovieDef.h"

NS_KRMOVIE_BEGIN
// The four current products emit no attributable implementation, vtable/RTTI,
// registration, or audio-delivery call for this interface.  Preserve the
// declaration as a dead/unobserved compatibility surface rather than inventing
// an active callback owner or delivery lifetime.
class IAudioCallback {
public:
    IAudioCallback() {};

    virtual ~IAudioCallback() {};

    virtual void OnInitialize(int iChannels, int iSamplesPerSec,
                              int iBitsPerSample) = 0;

    virtual void OnAudioData(const float *pAudioData, int iAudioDataLength) = 0;
};

NS_KRMOVIE_END
