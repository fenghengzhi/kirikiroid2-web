#pragma once

#include "Demux.h"
#include <map>
#include <vector>

extern "C" {
#include "libavformat/avformat.h"
}

#include "Thread.h"
#include "Timer.h"

struct IStream;

NS_KRMOVIE_BEGIN

class CDVDDemuxFFmpeg;

class InputStream;

class CDemuxStreamVideoFFmpeg : public CDemuxStreamVideo {
    // Both pointers are borrowed.  The demuxer's stream map owns this object;
    // neither pointer participates in its destruction.
    CDVDDemuxFFmpeg *m_parent;
    AVStream *m_stream;

public:
    CDemuxStreamVideoFFmpeg(CDVDDemuxFFmpeg *parent, AVStream *stream) :
        m_parent(parent), m_stream(stream) {}

    std::string m_description;

    std::string GetStreamName() override;
};

class CDemuxStreamAudioFFmpeg : public CDemuxStreamAudio {
    // Both pointers are borrowed.  The demuxer's stream map owns this object;
    // neither pointer participates in its destruction.
    CDVDDemuxFFmpeg *m_parent;
    AVStream *m_stream;

public:
    CDemuxStreamAudioFFmpeg(CDVDDemuxFFmpeg *parent, AVStream *stream) :
        m_parent(parent), m_stream(stream) {}

    std::string m_description;

    std::string GetStreamName() override;
};

struct StereoModeConversionMap;

class CDVDDemuxFFmpeg : public IDemux {
public:
    CDVDDemuxFFmpeg();

    ~CDVDDemuxFFmpeg() override;

    bool Open(InputStream *pInput, bool streaminfo = true,
              bool fileinfo = false);

    void Dispose();

    void Reset() override;

    void Flush() override;

    void Abort() override;

    void SetSpeed(int iSpeed) override;
    //	virtual std::string GetFileName() override;

    DemuxPacket *Read() override;

    bool SeekTime(int time, bool backwords = false,
                  double *startpts = nullptr) override;

    bool SeekByte(int64_t pos);

    int GetStreamLength() override;

    CDemuxStream *GetStream(int iStreamId) const override;

    std::vector<CDemuxStream *> GetStreams() const override;

    int GetNrOfStreams() const override;

    std::string GetStreamCodecName(int iStreamId) override;

    bool Aborted();

    AVFormatContext *m_pFormatContext;
    InputStream *m_pInput = nullptr;

protected:
    friend class CDemuxStreamAudioFFmpeg;

    friend class CDemuxStreamVideoFFmpeg;

    friend class CDemuxStreamSubtitleFFmpeg;

    // No standalone body survives in the reference artifacts: Read emits a
    // direct/inlined av_read_frame edge.  That cannot distinguish a fully
    // inlined source helper from a direct source call, so this otherwise
    // unused declaration is intentionally not given a speculative body.
    int ReadFrame(AVPacket *packet);

    CDemuxStream *AddStream(int streamIdx);

    void AddStream(int streamIdx, CDemuxStream *stream);

    void CreateStreams(unsigned int program = UINT_MAX);

    void DisposeStreams();

    void ParsePacket(AVPacket *pkt);

    bool IsVideoReady();

    void ResetVideoStreams();

    AVDictionary *GetFFMpegOptionsFromInput();

    double ConvertTimestamp(int64_t pts, int den, int num);

    void UpdateCurrentPTS();

    bool IsProgramChange();

    std::string GetStereoModeFromMetadata(AVDictionary *pMetadata);

    std::string ConvertCodecToInternalStereoMode(
        const std::string &mode, const StereoModeConversionMap *conversionMap);

    //	void GetL16Parameters(int &channels, int &samplerate);
    double SelectAspect(AVStream *st, bool &forced);

    CCriticalSection m_critSection;
    // The map owns its pointer values.  GetStream/GetStreams return borrowed
    // pointers, and the reference implementation does not lock this map.
    std::map<int, CDemuxStream *> m_streams;

    AVIOContext *m_ioContext;

    double m_currentPts; // used for stream length estimation
    bool m_bMatroska;
    bool m_bAVI;
    int m_speed;
    unsigned m_program;
    Timer m_timeout;

    // Due to limitations of ffmpeg, we only can detect a program
    // change with a packet. This struct saves the packet for the next
    // read and signals STREAMCHANGE to player
    struct {
        AVPacket pkt; // packet ffmpeg returned
        int result; // result from av_read_packet
    } m_pkt;

    bool m_streaminfo;
    bool m_checkvideo;
    int m_displayTime;
    double m_dtsAtDisplayTime;
};

NS_KRMOVIE_END
