#include <stdlib.h>
#ifdef _MSC_VER
// #include <concrt.h>
#endif
#include <stdint.h>
#include "tjsCommHead.h"
#include "EventIntf.h"
#include "layerExBase.hpp"
#include "ncbind.hpp"
#include "Application.h"
#include "LayerBitmapIntf.h"
#include <algorithm>
#include "movie/ffmpeg/KRMovieLayer.h"

#define NCB_MODULE_NAME TJS_W("layerExMovie.dll")

/*
 * Movie 描画用レイヤ
 */
struct layerExMovie : public layerExBase_GL, tTVPContinuousEventCallbackIntf {
protected:
    class VideoLayer : public KRMovie::VideoPresentLayer {
        std::function<void(KRMovieEvent, void *)> m_funcCallback;

    public:
        VideoLayer(const std::function<void(KRMovieEvent, void *)> &func) :
            m_funcCallback(func) {}
        void BuildGraph(IStream *stream, const tjs_char *streamname,
                        const tjs_char *type, uint64_t size) {
            // BasePlayer keeps a second std::function copy.  Its worker-thread
            // Ended event therefore goes straight to this callable, whereas
            // VideoPresentLayer's synchronous Update uses OnPlayEvent below.
            m_pPlayer->SetCallback(m_funcCallback);
            // The bool result is deliberately discarded: a non-throwing open
            // failure does not stop layerExMovie from publishing the overlay,
            // querying its size, allocating buffers and resizing the Layer.
            m_pPlayer->OpenFromStream(stream, streamname, type, size);
        }
        virtual void OnPlayEvent(KRMovieEvent msg, void *p) override {
            m_funcCallback(msg, p);
        }
    };
    VideoLayer *VideoOverlay;
    // MessageDelegate *UtilWindow;
    // ObjectT _pType;

    long movieWidth;
    long movieHeight;
    class tTVPBaseTexture *Bitmap[2];

    bool loop;
    bool alpha;

    tTJSBinaryStream *in;
#ifdef FILEBASE
    ttstr tempFile;
#else
// 	CIStreamProxy			*m_Proxy;
// 	CIStreamReader			*m_Reader;
#endif

    void clearMovie();

    // These look like borrowed raw pointers, but assigning a tTJSVariant to
    // DispatchT calls AsObject() and AddRefs the dispatch.  The native
    // destructor deliberately never Releases any of these three references,
    // so each successfully captured callback is leaked and remains callable
    // even after the Layer deletes its ordinary script members.
    DispatchT onStartMovie;
    DispatchT onUpdateMovie;
    DispatchT onStopMovie;

    bool playing;
    std::mutex mtxEvent;
    // One mutex-protected vector serves both the synchronous Update producer
    // and worker-thread Ended producer.  It has no movie generation and is not
    // cleared by clearMovie, openMovie or stopMovie.
    std::vector<KRMovieEvent> PostEvents;

public:
    layerExMovie(DispatchT obj);
    ~layerExMovie();

public:
    // ムービーのロード
    void openMovie(const tjs_char *filename, bool alpha);

    void startMovie(bool loop);
    void stopMovie();

    void start();
    void stop();

    bool isPlayingMovie();

    void onUpdate();
    void onEnded();

    /**
     * Continuous コールバック
     * 吉里吉里が暇なときに常に呼ばれる
     * 塗り直し処理
     */
    virtual void OnContinuousCallback(tjs_uint64 tick);
};

/**
 * コンストラクタ
 */
layerExMovie::layerExMovie(DispatchT obj) :
    /*_pType(obj, TJS_W("type")),*/ layerExBase_GL(obj) {
    VideoOverlay = nullptr;
    loop = false;
    alpha = false;
    movieWidth = 0;
    movieHeight = 0;
    in = nullptr;
    {
        tTJSVariant var;
        if(TJS_SUCCEEDED(obj->PropGet(TJS_IGNOREPROP, TJS_W("onStartMovie"),
                                      nullptr, &var, obj)))
            onStartMovie = var;
        else
            onStartMovie = nullptr;
        if(TJS_SUCCEEDED(obj->PropGet(TJS_IGNOREPROP, TJS_W("onStopMovie"),
                                      nullptr, &var, obj)))
            onStopMovie = var;
        else
            onStopMovie = nullptr;
        if(TJS_SUCCEEDED(obj->PropGet(TJS_IGNOREPROP, TJS_W("onUpdateMovie"),
                                      nullptr, &var, obj)))
            onUpdateMovie = var;
        else
            onUpdateMovie = nullptr;
    }
    playing = false;
    // UtilWindow = new MessageDelegate(EVENT_FUNC2(layerExMovie,
    // WndProc));
    Bitmap[0] = Bitmap[1] = nullptr;
}

/**
 * デストラクタ
 */
layerExMovie::~layerExMovie() {
    // The NCB adaptor keeps its native pointer published until this destructor
    // and scalar delete return.  tTJSCustomObject suppresses a second
    // Invalidate while Finalize is active, but does not suppress other method
    // re-entry from onStopMovie.  A re-entrant openMovie/startMovie can thus
    // repopulate VideoOverlay or the raw hook after stopMovie's one clear; the
    // remaining destructor body intentionally does not clear/remove again.
    stopMovie();
    // if (UtilWindow) delete UtilWindow;
    if(Bitmap[0])
        delete Bitmap[0];
    if(Bitmap[1])
        delete Bitmap[1];
}

void layerExMovie::clearMovie() {
    // This releases only the overlay.  In particular it does not stop/remove
    // this layerExMovie hook, reset playing/loop, delete Bitmap[], release in,
    // or discard PostEvents; openMovie relies on precisely this narrow scope.
    // Release synchronously destroys/joins the old BasePlayer when refcount is
    // one.  If Abort turns an in-flight read into a null packet while playback
    // is non-paused and both packet-byte queues are empty, the old worker can
    // append Ended to this same PostEvents vector before Release returns.  A
    // later open therefore may observe an event produced by the old movie.
    if(VideoOverlay) {
        VideoOverlay->Release(), VideoOverlay = nullptr;
    }
    // 	if (in) {
    // 		delete in;
    // 	}
}

/**
 * ムービーファイルを開いて準備する
 * @param filename ファイル名
 * @param alpha アルファ指定（半分のサイズでα処理する）
 */
void layerExMovie::openMovie(const tjs_char *filename, bool alpha) {
    // No stop() occurs here.  Re-opening while playing preserves the raw hook
    // and playing flag, and queued events from the previous movie remain.
    clearMovie();
    this->alpha = alpha;
    movieWidth = 0;
    movieHeight = 0;

    // The reference path opens storage directly; it does not copy to a
    // temporary file.  Failure leaves the old Bitmap[] and script Layer
    // size/type intact, despite the null overlay and zero native dimensions.
    if((in = TVPCreateStream(filename, TJS_BS_READ)) == nullptr) {
        ttstr error = filename;
        error += TJS_W(":ファイルが開けません");
        TVPAddLog(error);
        return;
    }
    ttstr ext = TVPExtractStorageExt(filename);
    ext.ToLowerCase();
    VideoLayer *pOverlay = new VideoLayer([this](KRMovieEvent msg, void *p) {
        std::lock_guard<std::mutex> lk(mtxEvent);
        // p is intentionally discarded.  Growth happens while holding the
        // same mutex used by the consumer snapshot and exceptions propagate.
        PostEvents.push_back(msg);
    });
    // TVPCreateIStream starts with refcount 1 and InputStream AddRefs it.
    // Neither BuildGraph nor this caller Releases the original reference, so
    // even the normal path leaks the adapter and its owning binary stream;
    // the next open overwrites in.  pOverlay is also a raw local until the
    // call returns, so a throwing BuildGraph leaks that complete object.
    pOverlay->BuildGraph(TVPCreateIStream(in), filename, ext.c_str(),
                         in->GetSize());
    VideoOverlay = pOverlay;
    VideoOverlay->GetVideoSize(&movieWidth, &movieHeight);
    // Deletion deliberately does not null the fields.  Each replacement is
    // published only after its constructor returns; an allocation/constructor
    // exception can therefore leave an old dangling field for later deletion,
    // with VideoOverlay already published.
    if(Bitmap[0])
        delete Bitmap[0];
    if(Bitmap[1])
        delete Bitmap[1];
    long size = movieWidth * movieHeight * 4;
    Bitmap[0] = new tTVPBaseTexture(movieWidth, movieHeight /*, 32*/);
    Bitmap[1] = new tTVPBaseTexture(movieWidth, movieHeight /*, 32*/);
    VideoOverlay->SetVideoBuffer(Bitmap[0], Bitmap[1], size);
    // Commit order is full-size buffers, optional integer width halving,
    // SetSize, then SetType.  There is no rollback around either Layer call.
    if(alpha) {
        movieWidth /= 2;
    }
    // 	_pWidth.SetValue(movieWidth);
    // 	_pHeight.SetValue(movieHeight);
    _this->SetSize(movieWidth, movieHeight);
    //_pType.SetValue(alpha ? ltAlpha : ltOpaque);
    _this->SetType(alpha ? ltAlpha : ltOpaque);
}

/**
 * ムービーの開始
 */
void layerExMovie::startMovie(bool loop) {
    if(VideoOverlay) {
        // 再生開始
        this->loop = loop;
        VideoOverlay->Play();
        start();
        if(onStartMovie != nullptr) {
            onStartMovie->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr,
                                   _obj);
        }
    }
}

/**
 * ムービーの停止
 */
void layerExMovie::stopMovie() {
    bool p = playing;
    if(VideoOverlay)
        VideoOverlay->Stop();
    stop();
    clearMovie();
    if(p) {
        if(onStopMovie != nullptr) {
            // This is the last access to *this in stopMovie.  playing is false
            // and the old overlay is already cleared before script runs.
            onStopMovie->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr,
                                  _obj);
        }
    }
}

bool layerExMovie::isPlayingMovie() { return playing; }

void layerExMovie::start() {
    // Remove-all then append exactly as the references do.  The registry stores
    // this raw callback pointer without retaining the layerExMovie object.
    stop();
    TVPAddContinuousEventHook(this);
    playing = true;
}

/**
 * Irrlicht 呼び出し処理停止
 */
void layerExMovie::stop() {
    TVPRemoveContinuousEventHook(this);
    playing = false;
}

void layerExMovie::onUpdate() {
    // 更新完了
    // サーフェースからレイヤに画面コピー
    reset();
    tTVPBaseTexture *frontbmp = VideoOverlay->GetFrontBuffer();
    if(frontbmp) {
        iTVPTexture2D *src = frontbmp->GetTexture();
        iTVPTexture2D *dst =
            _this->GetMainImage()->GetTextureForRender(false, nullptr);
        tTVPRect rcdst(_clipLeft, _clipTop, _clipLeft + _clipWidth,
                       _clipTop + _clipHeight);
        iTVPRenderMethod *method;
        if(alpha) {
            static iTVPRenderMethod *_method =
                TVPGetRenderManager()->GetRenderMethod("CopyColor");
            method = _method;
        } else {
            static iTVPRenderMethod *_method =
                TVPGetRenderManager()->GetRenderMethod("Copy");
            method = _method;
        }
        tRenderTexRectArray::Element src_tex[] = { tRenderTexRectArray::Element(
            src,
            tTVPRect(0, 0, std::min(_width, (tjs_int)movieWidth),
                     std::min(_height, (tjs_int)movieHeight))) };
        TVPGetRenderManager()->OperateRect(method, dst, nullptr, rcdst,
                                           src_tex);
    }
    // redraw();
    if(onUpdateMovie != nullptr) {
        // This is onUpdate's last member access.  Script may invalidate the
        // Layer and synchronously delete this native object before FuncCall
        // returns; the outer detached-event loop deliberately does not pin or
        // revalidate the owner afterward.
        onUpdateMovie->FuncCall(0, nullptr, nullptr, nullptr, 0, nullptr, _obj);
    }
}

void layerExMovie::onEnded() {
    // 更新終了
    if(loop) {
        VideoOverlay->Rewind();
        VideoOverlay->Play();
    } else {
        // std::bind stores only this raw pointer.  PostUserMessage copies the
        // callable, not a Layer/native ownership token, and its default
        // host/message fields are not consulted when the queue later invokes
        // it.  Destruction before the next message pass leaves this binding
        // dangling exactly as in all four references.
        Application->PostUserMessage(std::bind(&layerExMovie::stopMovie, this));
    }
}

void layerExMovie::OnContinuousCallback(tjs_uint64 tick) {
    if(VideoOverlay) {
        // 更新
        VideoOverlay->OnContinuousCallback(tick);
        std::vector<KRMovieEvent> vecEvent;
        {
            std::lock_guard<std::mutex> lk(mtxEvent);
            // Detach the complete producer range before invoking script-facing
            // handlers.  Events posted during those calls remain for a later
            // callback; the callbacks themselves may stop/restart and append a
            // new raw hook visible later in the current registry pass.
            vecEvent.swap(PostEvents);
        }
        for(KRMovieEvent msg : vecEvent) {
            // Iterator/end are stack snapshots, but every recognized event
            // reuses the original raw this.  If onUpdateMovie invalidates and
            // deletes the owner, the next Update/Ended item calls through the
            // freed pointer; there is intentionally no lifetime pin, break or
            // validity check.
            switch(msg) {
                case KRMovieEvent::Update:
                    onUpdate();
                    break;
                case KRMovieEvent::Ended:
                    onEnded();
                    break;
                default:
                    break;
            }
        }
    } else {
        stop();
    }
}

// ----------------------------------- クラスの登録

NCB_GET_INSTANCE_HOOK(layerExMovie){
    // インスタンスゲッタ
    NCB_INSTANCE_GETTER(objthis){
        // objthis を iTJSDispatch2* 型の引数とする
        ClassT *obj =
            GetNativeInstance(objthis); // ネイティブインスタンスポインタ取得
if(!obj) {
    obj = new ClassT(objthis); // ない場合は生成する
    SetNativeInstance(
        objthis,
        obj); // objthis に obj をネイティブインスタンスとして登録する
}
return obj;
}

// デストラクタ（実際のメソッドが呼ばれた後に呼ばれる）
~NCB_GET_INSTANCE_HOOK_CLASS() {}
}
;

// フックつきアタッチ
NCB_ATTACH_CLASS_WITH_HOOK(layerExMovie, Layer) {
    NCB_METHOD(openMovie);
    NCB_METHOD(startMovie);
    NCB_METHOD(stopMovie);
    NCB_METHOD(isPlayingMovie);
}
