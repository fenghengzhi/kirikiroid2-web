#pragma once

#include "Codecs.h"
#include "StreamInfo.h"
#include "VideoCodec.h"
#include "Ref.h"

extern "C" {
#include "libavfilter/avfilter.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
}

NS_KRMOVIE_BEGIN

class CDVDVideoCodecFFmpeg : public CDVDVideoCodec {
public:
    // The four shipped mobile references retain the consumer-side virtual
    // dispatch contract below, but their Open paths only publish SW_SINGLE or
    // SW_MULTI and no build publishes a non-null hardware owner.  No concrete
    // decoder vtable can therefore be attributed to this interface in those
    // binaries.  Keep the abstract source hierarchy: dead emission is not
    // evidence that the header tokens were absent.
    class IHardwareDecoder : public IRef<IHardwareDecoder> {
    public:
        IHardwareDecoder() {}

        ~IHardwareDecoder() override {};

        virtual bool Open(AVCodecContext *avctx, AVCodecContext *mainctx,
                          const enum AVPixelFormat, unsigned int surfaces) = 0;

        virtual int Decode(AVCodecContext *avctx, AVFrame *frame) = 0;

        virtual bool GetPicture(AVCodecContext *avctx, AVFrame *frame,
                                DVDVideoPicture *picture) = 0;

        virtual int Check(AVCodecContext *avctx) = 0;

        virtual void Reset() {}

        virtual unsigned GetAllowedReferences() { return 0; }

        virtual bool CanSkipDeint() { return false; }

        virtual const std::string Name() = 0;

        virtual void SetCodecControl(int flags) {};
    };

    CDVDVideoCodecFFmpeg(CProcessInfo &processInfo);

    ~CDVDVideoCodecFFmpeg() override;

    bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options) override;

    int Decode(uint8_t *pData, int iSize, double dts, double pts) override;

    void Reset() override;

    void Reopen() override;

    bool GetPictureCommon(DVDVideoPicture *pDvdVideoPicture);

    bool GetPicture(DVDVideoPicture *pDvdVideoPicture) override;

    void SetDropState(bool bDrop) override;

    const char *GetName() override {
        return m_name.c_str();
    }; // UpdateName may invalidate this pointer after a hardware owner change.
    unsigned GetConvergeCount() override;

    unsigned GetAllowedReferences() override;

    bool GetCodecStats(double &pts, int &droppedFrames,
                       int &skippedPics) override;

    void SetCodecControl(int flags) override;

    IHardwareDecoder *GetHardware() { return m_pHardware; };

    // Ownership-transfer setter: it releases the old pointer and stores the
    // candidate without AddRef or a same-pointer guard.  Android retains a
    // zero-xref standalone body; the iOS builds retain only the inlined null
    // release path in GetFormat.
    void SetHardware(IHardwareDecoder *hardware);

protected:
    void Dispose();

    static enum AVPixelFormat GetFormat(struct AVCodecContext *avctx,
                                        const AVPixelFormat *fmt);

    int FilterOpen(const std::string &filters, bool scale);

    void FilterClose();

    int FilterProcess(AVFrame *frame);

    void SetFilters();

    void UpdateName();

    AVFrame *m_pFrame;
    AVFrame *m_pDecodedFrame;
    AVCodecContext *m_pCodecContext;

    std::string m_filters;
    std::string m_filters_next;
    AVFilterGraph *m_pFilterGraph;
    AVFilterContext *m_pFilterIn;
    AVFilterContext *m_pFilterOut;
    AVFrame *m_pFilterFrame;
    // Intentionally absent from the constructor initializer list.  It is
    // dormant while m_pFilterGraph is null; successful FilterOpen is its
    // first required write, and FilterProcess may later leave it sticky true.
    bool m_filterEof;

    int m_iPictureWidth;
    int m_iPictureHeight;

    int m_iScreenWidth;
    int m_iScreenHeight;
    int m_iOrientation; // orientation of the video in degress counter
                        // clockwise

    unsigned int m_uSurfacesCount;

    std::string m_name;
    int m_decoderState;
    IHardwareDecoder *m_pHardware;
    int m_iLastKeyframe;
    double m_dts;
    bool m_started;
    std::vector<AVPixelFormat> m_formats;
    double m_decoderPts;
    int m_skippedDeint;
    int m_droppedFrames;
    bool m_requestSkipDeint;
    int m_codecControlFlags;
    bool m_interlaced;
    double m_DAR;
    CDVDStreamInfo m_hints;
    // The implicit CDVDCodecOptions constructor builds the two vectors but
    // leaves m_opaque_pointer indeterminate until the first Open copy.
    CDVDCodecOptions m_options;

    struct CDropControl {
        CDropControl();

        void Reset(bool init);

        void Process(int64_t pts, bool drop);

        int64_t m_lastPTS;
        int64_t m_diffPTS;
        int m_count;
        enum { INIT, VALID } m_state;
    } m_dropCtrl;
};
NS_KRMOVIE_END
