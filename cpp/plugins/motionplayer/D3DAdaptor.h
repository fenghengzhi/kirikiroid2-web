// Motion.D3DAdaptor reconstructed from the four 1.3.9 reference binaries.
// Platform-specific addresses and STL layout details live in analysis/.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include "tjs.h"
#include "tjsUtils.h"
#include "LayerIntf.h"

class iTVPTexture2D;

namespace motion {

    class Player;
    namespace detail {
        struct PreparedRenderItem;
    }

    class D3DAdaptor {
    public:
        D3DAdaptor() = default;
        D3DAdaptor(iTJSDispatch2 *windowObject,
                   int width,
                   int height,
                   int centerX,
                   int centerY);
        ~D3DAdaptor();
        D3DAdaptor(const D3DAdaptor &) = delete;
        D3DAdaptor &operator=(const D3DAdaptor &) = delete;

        // Native half of the generated NCB Factory descriptor.  The descriptor
        // owns the one-Void shell special case and adaptor attach/rollback;
        // this callback requires the five-argument prefix and publishes its
        // out pointer only after the native constructor returns.
        static tjs_error factory(D3DAdaptor **result, tjs_int numparams,
                                 tTJSVariant **param, iTJSDispatch2 *);

        void renderFromPlayer_guess(
            Player *player,
            std::vector<detail::PreparedRenderItem *> &mainList);

        // --- Properties ---
        // `visible` and `alphaOpAdd` are script-visible echo state only in the
        // four references. They are not consulted by the adaptor/Player render
        // pipeline; the shared batch renderer passes literal true for its
        // alpha-add method-selection input.
        bool getVisible() const { return _visible; }
        void setVisible(bool v) { _visible = v; }
        bool getAlphaOpAdd() const { return _alphaOpAdd; }
        void setAlphaOpAdd(bool v) { _alphaOpAdd = v; }
        // These two bytes do have native consumers: capture gates the complete
        // Player-to-target render helper, while clear gates explicit FillARGB.
        bool getCanvasCaptureEnabled() const { return _canvasCaptureEnabled; }
        void setCanvasCaptureEnabled(bool v) { _canvasCaptureEnabled = v; }
        bool getClearEnabled() const { return _clearEnabled; }
        void setClearEnabled(bool v) { _clearEnabled = v; }

        // --- Methods ---
        void setPos(int, int) {}
        void setSize(int w, int h);
        // Both values are retained exactly, but neither has a post-set native
        // consumer in the current references. clearTargetTexture consumes its
        // explicit call argument rather than `_clearColor`.
        void setClearColor(int color) { _clearColor = color; }
        void setResizable(bool v) { _resizable = v; }
        void removeAllTextures();

        // setPos and these five exported members are intentional native
        // nullsubs in all four reference binaries.  registerBg/registerCaption
        // still keep their typed NCB signatures: the generated wrappers enforce
        // arity and perform all Variant/float/bool conversions before the no-op.
        void removeAllBg() {}
        void removeAllCaption() {}
        void registerBg(tTJSVariant, float, float, float, bool) {}
        void registerCaption(tTJSVariant, float, float) {}
        void unloadUnusedTextures() {}

        void captureCanvas(tTJSVariant layer);

        int getWidth() const { return _width; }
        int getHeight() const { return _height; }
        iTJSDispatch2 *getWindowObject() const;
        int getCenterX() const { return _centerX; }
        int getCenterY() const { return _centerY; }
        iTVPTexture2D *targetTexture() const { return _targetTexture; }
        bool hasTargetTexture() const { return _targetTexture != nullptr; }
        void clearTargetTexture(tjs_int color);
        bool copyTargetTextureRows_guess(std::uint8_t *dst,
                                         tjs_int dstPitch);
        iTVPTexture2D *getRenderTexture_guess(iTVPTexture2D *source);
        std::size_t getCachedTextureCount_guess() const {
            return _softwareTextureCopies.size();
        }

        void initialize_guess(const tTJSVariant &window,
                              int width,
                              int height,
                              int centerX,
                              int centerY);

    private:
        void initializeFromWindowObject_guess(iTJSDispatch2 *windowObject,
                                               int width,
                                               int height,
                                               int centerX,
                                               int centerY);
        void releaseTargetTexture();

        int _width = 0;
        int _height = 0;
        int _centerX = 0;
        int _centerY = 0;
        // This is a real int32 member, not alignment padding: all four native
        // constructors explicitly store zero. The complete adaptor/Player
        // consumer surface never accesses it again and the destructor ignores
        // it, so only the original field name remains unknown.
        int _dormantState_guess = 0;
        bool _visible = false;
        bool _canvasCaptureEnabled = false;
        bool _clearEnabled = true;
        bool _resizable = false;
        bool _alphaOpAdd = false;
        iTJSDispatch2 *_window = nullptr;
        int _clearColor = 0;
        iTVPTexture2D *_targetTexture = nullptr;
        // Keys are borrowed source identities.  The mapped holder performs
        // unconditional intrusive AddRef/Release, including on insertion.
        std::map<iTVPTexture2D *, TJS::tTJSRefHolder<iTVPTexture2D>>
            _softwareTextureCopies;
    };

} // namespace motion
