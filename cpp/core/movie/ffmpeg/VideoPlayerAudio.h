#pragma once

#include "Thread.h"
#include "IVideoPlayer.h"
#include "AudioCodec.h"
#include "BitstreamStats.h"
#include "MessageQueue.h"
#include "Timer.h"
#include "Clock.h"
#include "AudioDevice.h"

NS_KRMOVIE_BEGIN
class CDVDClock;

class CVideoPlayerAudio : public CThread, public IDVDStreamPlayerAudio {
public:
    CVideoPlayerAudio(CDVDClock *pClock, CDVDMessageQueue &parent,
                      CProcessInfo &processInfo);

    ~CVideoPlayerAudio() override;

    bool OpenStream(CDVDStreamInfo &hints) override;

    void CloseStream(bool bWaitForBuffers) override;

    void SetSpeed(int speed) override;

    void Flush(bool sync) override;

    // waits until all available data has been rendered
    bool AcceptsData() override;

    // This is the queue's demux-packet byte counter; control nodes such as
    // GENERAL_EOF do not make HasData true.
    bool HasData() const override { return m_messageQueue.GetDataSize() > 0; }

    int GetLevel() override { return m_messageQueue.GetLevel(); }

    bool IsInited() const override { return m_messageQueue.IsInited(); }

    void SendMessage(CDVDMsg *pMsg, int priority = 0) override {
        m_messageQueue.Put(pMsg, priority);
    }

    // The default argument is DEMUXER_PACKET: this preserves control nodes and
    // is distinct from Flush(bool), which also publishes GENERAL_FLUSH and
    // aborts device packet addition.
    void FlushMessages() override { m_messageQueue.Flush(); }

    //	void SetDynamicRangeCompression(long drc)             {
    // m_dvdAudio.SetDynamicRangeCompression(drc); }
    // Exact constant +0.0f; no audio-device or stream query is performed.
    float GetDynamicRangeAmplification() const override { return 0.0f; }

    std::string GetPlayerInfo() override;

    int GetAudioBitrate() override;

    int GetAudioChannels() override;

    // holds stream information for current playing stream
    CDVDStreamInfo m_streaminfo;

    double GetCurrentPts() override {
        // Return the raw published double under its own lock transaction. No
        // NOPTS/NaN conversion is performed at this consumer boundary.
        CSingleLock lock(m_info_section);
        return m_info.pts;
    }

    bool IsStalled() const override { return m_stalled; }

    bool IsPassthrough() override;

    // Borrowed alias to the embedded value object. The child retains ownership
    // and the pointer becomes invalid with the CVideoPlayerAudio lifetime.
    CDVDAudio *GetOutputDevice() override { return &m_dvdAudio; }

protected:
    void OnStartup() override;

    void OnExit() override;

    void Process() override;

    void UpdatePlayerInfo();

    void OpenStream(CDVDStreamInfo &hints, CDVDAudioCodec *codec);

    //! Switch codec if needed. Called when the sample rate gotten
    //! from the codec changes, in which case we may want to switch
    //! passthrough on/off.
    bool SwitchCodecIfNeeded();
    //	float GetCurrentAttenuation()                         { return
    // m_dvdAudio.GetCurrentAttenuation(); }

    CDVDMessageQueue m_messageQueue;
    CDVDMessageQueue &m_messageParent;

    double m_audioClock;

    CDVDAudio m_dvdAudio; // audio output device
    CDVDClock *m_pClock; // dvd master clock
    CDVDAudioCodec *m_pAudioCodec; // audio codec
    BitstreamStats m_audioStats;

    int m_speed;
    bool m_stalled;
    bool m_silence;
    bool m_paused;
    IDVDStreamPlayer::ESyncState m_syncState;
    Timer m_syncTimer;

    bool OutputPacket(DVDAudioFrame &audioframe);

    // SYNC_DISCON, SYNC_SKIPDUP, SYNC_RESAMPLE
    int m_synctype;
    int m_setsynctype;
    int m_prevsynctype; // so we can print to the log

    void SetSyncType(bool passthrough);

    bool m_prevskipped;
    double m_maxspeedadjust;

    struct SInfo {
        SInfo() : pts(DVD_NOPTS_VALUE), passthrough(false) {}

        std::string info;
        double pts;
        bool passthrough;
    };

    CCriticalSection m_info_section;
    SInfo m_info;
};

NS_KRMOVIE_END
