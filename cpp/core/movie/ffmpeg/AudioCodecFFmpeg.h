#pragma once

#include "KRMovieDef.h"
#include "AudioCodec.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswresample/swresample.h"
}

NS_KRMOVIE_BEGIN
class CProcessInfo;

class CDVDAudioCodecFFmpeg : public CDVDAudioCodec {
public:
    CDVDAudioCodecFFmpeg(CProcessInfo &processInfo);

    ~CDVDAudioCodecFFmpeg() override;

    bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options) override;

    void Dispose() override;

    int Decode(uint8_t *pData, int iSize, double dts, double pts) override;

    void GetData(DVDAudioFrame &frame) override;

    int GetData(uint8_t **dst) override;

    void Reset() override;

    AEAudioFormat GetFormat() override { return m_format; }

    const char *GetName() override { return "FFmpeg"; }

    enum AVMatrixEncoding GetMatrixEncoding() override;

    enum AVAudioServiceType GetAudioServiceType() override;

    int GetProfile() override;

protected:
    enum AEDataFormat GetDataFormat();

    int GetSampleRate();

    int GetChannels();

    CAEChannelInfo GetChannelMap();

    int GetBitRate() override;

    // Constructed once. Open, Reset and Dispose do not clear it; only a
    // successfully decoded frame refreshes dataFormat/channelLayout,
    // sampleRate and frameSize.
    AEAudioFormat m_format;
    AVCodecContext *m_pCodecContext;

    // Retained source field: all four references write only
    // AV_SAMPLE_FMT_NONE in the constructor and after a successful Open. No
    // emitted path reads it or publishes another value.
    enum AVSampleFormat m_iSampleFormat;

    // Fixed-array channel cache. Open resets m_channels but deliberately keeps
    // both this value and m_layout until BuildChannelMap runs.
    CAEChannelInfo m_channelLayout;

    // Deliberately not initialized by the constructor. Open first writes NONE
    // only after codec-context allocation succeeds.
    enum AVMatrixEncoding m_matrixEncoding;

    AVFrame *m_pFrame1;

    // Decoder output flag: constructor, raw GetData and Reset clear it. Open
    // does not, which is observable if the same codec object is reopened.
    int m_gotFrame;

    int m_channels;

    // Raw context layout cache. BuildChannelMap stores it before validating
    // popcount/fallback; Open does not reset it.
    uint64_t m_layout;

    void BuildChannelMap();

    // Declaration-only residue in all four current references: there is no
    // caller and no emitted definition in the complete codec TU surface.
    void ConvertToFloat();
};

NS_KRMOVIE_END
