#include "KRMovieLayer.h"
#include "VideoCodec.h"
#include "LayerBitmapIntf.h"
#include "Application.h"
#include "VideoOvlImpl.h"

extern "C" {
#include "libswscale/swscale.h"
}

NS_KRMOVIE_BEGIN

VideoPresentLayer::~VideoPresentLayer() {
    // Remove zeroes every matching raw-hook slot before base destruction.
    TVPRemoveContinuousEventHook(this);
}

tTVPBaseTexture *VideoPresentLayer::GetFrontBuffer() {
    BitmapPicture pic;
    // This fast path deliberately reads the plain count without the picture
    // mutex.  There is no locked recheck: a concurrent Flush after this test
    // can leave the following path consuming a cleared slot and decrementing
    // the count below zero.
    if(!m_usedPicture) {
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf = m_picture[m_curPicture];
        // swap transfers the four owned pointers plus width/height only.  The
        // format and pts fields are intentionally not part of the transfer.
        picbuf.swap(pic);
        m_curPicture = (m_curPicture + 1) & (MAX_BUFFER_COUNT - 1);
        --m_usedPicture;
        assert(m_usedPicture >= 0);
        m_condPicture.notify_all();
    }
    FrameMove();
    int n = m_nCurBmpBuff;
    m_nCurBmpBuff = !m_nCurBmpBuff;
    // Both texture pointers are borrowed and unchecked.  The slot is already
    // consumed, waiters notified and the selector flipped if Update throws.
    m_BmpBits[n]->Update(pic.data[0], pic.width * 4, 0, 0, pic.width,
                         pic.height);
    return m_BmpBits[n];
}

void VideoPresentLayer::SetVideoBuffer(tTVPBaseTexture *buff1,
                                       tTVPBaseTexture *buff2, long size) {
    // The two pointers are borrowed raw storage; there is no AddRef.  The size
    // argument is intentionally ignored, and every call resets the front/back
    // selector to buffer zero.
    m_BmpBits[0] = buff1;
    m_BmpBits[1] = buff2;
    m_nCurBmpBuff = 0;
    //	TVPAddContinuousEventHook(this);
}

void VideoPresentLayer::OnContinuousCallback(tjs_uint64 tick) {
    // tick is intentionally unused.  At most one due Update is synchronously
    // forwarded through the derived OnPlayEvent; GetFrontBuffer later consumes
    // the picture when layerExMovie handles the queued event.
    // Like GetFrontBuffer, the initial plain-count read is intentionally
    // unlocked and is not repeated after taking m_mtxPicture.
    if(!m_usedPicture)
        return;
    double m_curpts = m_pPlayer->GetClock() / DVD_TIME_BASE;
    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf = m_picture[m_curPicture];
        // check pts
        if(picbuf.pts > m_curpts) { // present in future
            return;
        }
    }
#if 0
        do { // skip frame
            pic.Clear();
            picbuf.swap(pic);
            m_curPicture = (m_curPicture + 1) & (MAX_BUFFER_COUNT - 1);
            --m_usedPicture;
        } while (m_usedPicture > 0 && m_curpts >= m_picture[m_curPicture].pts);
        assert(m_usedPicture >= 0);
#endif
    OnPlayEvent(KRMovieEvent::Update, nullptr);
}

int VideoPresentLayer::AddVideoPicture(DVDVideoPicture &pic, int index) {
    // from other thread
    if(pic.format != RENDER_FMT_YUV420P)
        return -2;
    if(pic.pts == DVD_NOPTS_VALUE)
        return 0;

    // The full check is unlocked, then performs exactly one unconditional
    // wait.  Spurious wakeup or another producer can therefore reach the
    // second check still full and return -1.
    if(m_usedPicture >= MAX_BUFFER_COUNT) {
        std::unique_lock<std::mutex> lk(m_mtxPicture);
        m_condPicture.wait(lk);
    }
    if(m_usedPicture >= MAX_BUFFER_COUNT)
        return -1;

    int width = pic.iWidth, height = pic.iHeight;

    // Allocation and conversion happen outside the ring mutex.  A null
    // allocation is not checked, and publication is not transactional.
    uint8_t *data = (uint8_t *)TJSAlignedAlloc(width * height * 4, 4);
    int datasize = width * 4;

    // All four references pass numeric value 28.  Their bundled descriptor
    // table maps 28 to RGBA (ARGB/RGBA/ABGR/BGRA are 27..30); the Web port's
    // libavutil-55 headers retain that same layout.  Keep the source symbol
    // instead of baking the ABI-specific enum value into this call.
    img_convert_ctx = sws_getCachedContext(
        img_convert_ctx, width, height, AV_PIX_FMT_YUV420P, width, height,
        AV_PIX_FMT_RGBA, /*sws_flags*/ SWS_FAST_BILINEAR, nullptr, nullptr,
        nullptr);
    assert(img_convert_ctx);
    // The processed-line result is computed but never gates publication.
    int processed = sws_scale(img_convert_ctx, pic.data, pic.iLineSize, 0,
                              pic.iHeight, &data, &datasize);

    {
        std::lock_guard<std::mutex> lk(m_mtxPicture);
        BitmapPicture &picbuf =
            m_picture[(m_curPicture + m_usedPicture) & (MAX_BUFFER_COUNT - 1)];
        picbuf.Clear();
        picbuf.width = width;
        picbuf.height = height;
        picbuf.data[0] = data;
        picbuf.pts = pic.pts / DVD_TIME_BASE;
        ++m_usedPicture;
    }

    return MAX_BUFFER_COUNT - m_usedPicture;
}

void MoviePlayerLayer::BuildGraph(tTJSNI_VideoOverlay *callbackwin,
                                  IStream *stream, const tjs_char *streamname,
                                  const tjs_char *type, uint64_t size) {
    m_pCallbackWin = callbackwin;
    // Keep the native std::bind topology: the erased target stores a virtual
    // member-function representation and a borrowed raw this pointer.
    m_pPlayer->SetCallback(std::bind(&MoviePlayerLayer::OnPlayEvent, this,
                                     std::placeholders::_1,
                                     std::placeholders::_2));
    m_pPlayer->OpenFromStream(stream, streamname, type, size);
}

void MoviePlayerLayer::OnPlayEvent(KRMovieEvent msg, void *p) {
    if(msg == KRMovieEvent::Update) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_UPDATE;
        int frame;
        GetFrame(&frame);
        ev.LParam = frame;
        m_pCallbackWin->WndProc(ev); // in the same thread
    } else if(msg == KRMovieEvent::Ended) {
        NativeEvent ev(WM_GRAPHNOTIFY);
        ev.WParam = EC_COMPLETE;
        ev.LParam = 0;
        m_pCallbackWin->PostEvent(ev);
    }
}

void MoviePlayerLayer::Play() {
    inherit::Play();
    // Unconditional append is native behaviour: repeated Play calls create
    // duplicate callbacks, Stop does not remove them, and destruction removes
    // all matches.  Re-entrant Play can append work visible in the same pass.
    TVPAddContinuousEventHook(this);
}

NS_KRMOVIE_END
