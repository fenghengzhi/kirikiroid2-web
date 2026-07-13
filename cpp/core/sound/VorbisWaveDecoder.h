#pragma once

#include "WaveIntf.h"

class VorbisWaveDecoderCreator : public tTVPWaveDecoderCreator {
public:
    // VorbisWaveDecoderCreator() {
    // TVPRegisterWaveDecoderCreator(this); }
    tTVPWaveDecoder *Create(const ttstr &storagename,
                            const ttstr &extension) override;
#ifdef __EMSCRIPTEN__
    tTVPWaveDecoder *CreateFromStream(const ttstr &storagename,
                                      const ttstr &extension,
                                      tTJSBinaryStream *stream) override;
#endif
};

class OpusWaveDecoderCreator : public tTVPWaveDecoderCreator {
public:
    // VorbisWaveDecoderCreator() {
    // TVPRegisterWaveDecoderCreator(this); }
    tTVPWaveDecoder *Create(const ttstr &storagename,
                            const ttstr &extension) override;
#ifdef __EMSCRIPTEN__
    tTVPWaveDecoder *CreateFromStream(const ttstr &storagename,
                                      const ttstr &extension,
                                      tTJSBinaryStream *stream) override;
#endif
};
