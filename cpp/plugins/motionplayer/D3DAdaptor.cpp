// Motion.D3DAdaptor texture target and software-source cache.
#include "D3DAdaptor.h"

#include <cstring>

#include "MsgIntf.h"
#include "MotionRenderBackend.h"
#include "RenderManager.h"

namespace motion {

    D3DAdaptor::D3DAdaptor(TextureCacheTestTag) {}

    D3DAdaptor *
    D3DAdaptor::createTextureCacheShellForDifferentialTest_guess() {
        return new D3DAdaptor(TextureCacheTestTag{});
    }

    D3DAdaptor::D3DAdaptor(iTJSDispatch2 *windowObject,
                           int width,
                           int height,
                           int centerX,
                           int centerY)
        : _width(width),
          _height(height),
          _centerX(centerX),
          _centerY(centerY),
          _window(windowObject) {
        // `_window` is deliberately a raw retained pointer rather than an RAII
        // holder.  If AddRef or target creation throws, C++ destroys the map
        // member but does not release this Window reference.
        if(_window) {
            _window->AddRef();
        }
        _targetTexture = render_backend_guess::
            getPrivateOpenGLRenderManager_guess()->CreateTexture2D(
            nullptr, 0, static_cast<unsigned int>(_width),
            static_cast<unsigned int>(_height), TVPTextureFormat::RGBA, 0);
    }

    D3DAdaptor::~D3DAdaptor() {
        removeAllTextures();
        releaseTargetTexture();
        if(_window) {
            // The native destructor does not clear this raw slot after the
            // final Release; member destruction follows immediately.
            _window->Release();
        }
    }

    tjs_error D3DAdaptor::factory(D3DAdaptor **result, tjs_int numparams,
                                  tTJSVariant **param, iTJSDispatch2 *) {
        if(numparams < 5) return TJS_E_BADPARAMCOUNT;

        iTJSDispatch2 *windowObject = param[0]->AsObjectNoAddRef();
        if(!windowObject ||
           windowObject->IsInstanceOf(0, nullptr, nullptr, TJS_W("Window"),
                                      windowObject) != TJS_S_TRUE) {
            TVPThrowExceptionMessage(TJS_W("must set Window object"));
        }

        auto *obj = new D3DAdaptor(
            windowObject,
            static_cast<int>(param[1]->AsInteger()),
            static_cast<int>(param[2]->AsInteger()),
            static_cast<int>(param[3]->AsInteger()),
            static_cast<int>(param[4]->AsInteger()));
        *result = obj;
        return TJS_S_OK;
    }

    void D3DAdaptor::setSize(int w, int h) {
        _width = w;
        _height = h;
    }

    void D3DAdaptor::removeAllTextures() {
        _softwareTextureCopies.clear();
    }

    void D3DAdaptor::captureCanvas(tTJSVariant layerVariant) {
        auto *layer = tTJSNI_Layer::FromVariant(layerVariant);

        if(TVPIsSoftwareRenderManager()) {
            const tjs_int width =
                static_cast<tjs_int>(_targetTexture->GetWidth());
            tjs_int height =
                static_cast<tjs_int>(_targetTexture->GetHeight());
            layer->SetImageSize(width, height);
            const auto *src = static_cast<const std::uint8_t *>(
                _targetTexture->GetScanLineForRead(0));
            auto *dst = static_cast<std::uint8_t *>(
                layer->GetMainImagePixelBufferForWrite());
            const tjs_int srcPitch = _targetTexture->GetPitch();
            const auto dstPitch = layer->GetMainImagePixelBufferPitch();
            if(srcPitch == dstPitch) {
                // The native expression multiplies in signed 32-bit and only
                // then converts the result to size_t.
                std::memcpy(dst, src,
                            static_cast<std::size_t>(srcPitch * height));
            } else if(height >= 1) {
                const auto rowBytes =
                    static_cast<std::size_t>(width) *
                    sizeof(std::uint32_t);
                do {
                    std::memcpy(dst, src, rowBytes);
                    dst += dstPitch;
                    src += srcPitch;
                    --height;
                } while(height != 0);
            }
            return;
        }

        // GetMainImage applies pending font state before returning the current
        // bitmap.  The native capture path deliberately does not make that
        // bitmap independent before selecting its texture as a reuse candidate.
        iTVPTexture2D *replacement = nullptr;
        auto *candidate = layer->GetMainImage()->GetTexture();
        if(!candidate->IsStatic() &&
           candidate->GetWidth() == _targetTexture->GetWidth() &&
           candidate->GetHeight() == _targetTexture->GetHeight()) {
            candidate->AddRef();
            replacement = candidate;
        }

        layer->AssignTexture(_targetTexture);
        // This release is intentionally unconditional. The constructor/GPU
        // replacement path is responsible for maintaining a live target.
        _targetTexture->Release();
        _targetTexture = replacement;
        if(!_targetTexture) {
            _targetTexture =
                render_backend_guess::getPrivateOpenGLRenderManager_guess()
                    ->CreateTexture2D(
                        nullptr, 0, static_cast<unsigned int>(_width),
                        static_cast<unsigned int>(_height),
                        TVPTextureFormat::RGBA, 0);
        }
    }

    iTJSDispatch2 *D3DAdaptor::getWindowObject() const {
        return _window;
    }

    void D3DAdaptor::clearTargetTexture(tjs_int color) {
        if(!_clearEnabled) {
            return;
        }

        // The references use two separately guarded function-local statics.
        // The first caches the borrowed/raw FillARGB method pointer; the
        // second caches its `color` parameter ID.  Both values are trivial, so
        // process exit runs no destructor and releases no renderer object.  A
        // disabled clear reaches neither guard.  ABIs which emit an exception
        // landing path abort only the active guard: a failed method lookup is
        // retried from stage one, whereas a failed parameter lookup preserves
        // the already-published method and retries only stage two.  The Android
        // armv7 build emits no explicit guard-abort landing for these calls.
        static auto *method =
            render_backend_guess::getPrivateOpenGLRenderManager_guess()
                ->GetRenderMethod("FillARGB");
        static const int colorId = method->EnumParameterID("color");
        method->SetParameterColor4B(colorId, static_cast<unsigned int>(color));
        const tTVPRect rc(0, 0, _targetTexture->GetWidth(),
                          _targetTexture->GetHeight());
        // This call-site does not retain another local pointer: the first
        // successful clear calls the shared private-manager getter once for
        // method initialization and once here; later clears call it here only.
        // The getter itself owns the guarded process cache. No local null check
        // or lock separates SetParameterColor4B from the in-place
        // target==source OperateRect call.
        auto *mgr =
            render_backend_guess::getPrivateOpenGLRenderManager_guess();
        mgr->OperateRect(method, _targetTexture, _targetTexture, rc,
                         tRenderTexRectArray());
    }

    bool D3DAdaptor::copyTargetTextureRows_guess(
        std::uint8_t *dst, tjs_int dstPitch) {
        const auto *srcBase = static_cast<const std::uint8_t *>(
            _targetTexture->GetScanLineForRead(0));
        const tjs_int srcPitch = _targetTexture->GetPitch();
        tjs_int height = static_cast<tjs_int>(_targetTexture->GetHeight());
        if(dstPitch == srcPitch) {
            std::memcpy(dst, srcBase,
                        static_cast<std::size_t>(srcPitch * height));
            return true;
        }

        if(height >= 1) {
            const tjs_int width =
                static_cast<tjs_int>(_targetTexture->GetWidth());
            const auto rowBytes =
                static_cast<std::size_t>(width) * sizeof(std::uint32_t);
            do {
                std::memcpy(dst, srcBase, rowBytes);
                dst += dstPitch;
                srcBase += srcPitch;
                --height;
            } while(height != 0);
        }
        return true;
    }

    iTVPTexture2D *D3DAdaptor::getRenderTexture_guess(
        iTVPTexture2D *source) {
        if(!TVPIsSoftwareRenderManager()) {
            return source;
        }
        const auto found = _softwareTextureCopies.find(source);
        if(found != _softwareTextureCopies.end()) {
            // A hit is only a borrow from the mapped intrusive holder.  It does
            // not acquire another reference for the return value.
            return found->second.GetObjectNoAddRef();
        }

        // Preserve the native callback order across compilers: acquire the
        // private renderer first, then query scanline, pitch, size, and format.
        auto *manager =
            render_backend_guess::getPrivateOpenGLRenderManager_guess();
        const auto *pixels = source->GetScanLineForRead(0);
        const auto pitch = source->GetPitch();
        const auto width = source->GetWidth();
        const auto height = source->GetHeight();
        const auto format = source->GetFormat();
        auto *copy = manager->CreateTexture2D(
            pixels, pitch, width, height, format,
            RENDER_CREATE_TEXTURE_FLAG_STATIC);
        // The implicit mapped-holder construction is non-null-safe. The
        // factory's creation reference is not released by this caller. If the
        // following node allocation throws, that raw creation reference has no
        // local RAII owner and leaks just as it does in the four references.
        _softwareTextureCopies.emplace(source, copy);
        return copy;
    }

    void D3DAdaptor::seedTextureCacheForDifferentialTest_guess(
        iTVPTexture2D *source, iTVPTexture2D *copy) {
        _softwareTextureCopies.emplace(source, copy);
    }

    void D3DAdaptor::configureShellForDifferentialTest_guess(
        iTJSDispatch2 *windowObject,
        int width, int height, int centerX, int centerY,
        iTVPTexture2D *adoptedTarget) {
        _width = width;
        _height = height;
        _centerX = centerX;
        _centerY = centerY;
        _window = windowObject;
        if(_window) {
            _window->AddRef();
        }
        _targetTexture = adoptedTarget;
    }

    void D3DAdaptor::releaseTargetTexture() {
        if(_targetTexture) {
            _targetTexture->Release();
            _targetTexture = nullptr;
        }
    }

} // namespace motion
