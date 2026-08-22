#pragma once

#include "../DrawDeviceD3DIntf.h"

namespace motion {

    // All four current references use this same natural layout: the virtual
    // base contributes only the deleting-destructor vptr, followed by six
    // public configuration scalars and the private max-texture pair. Every
    // accessor below is a direct field load/store; the native code performs no
    // clamping, validation, dirty notification, or propagation to Player.
    class D3DEmoteModule final : public D3DModuleBase_guess {
    public:
        D3DEmoteModule() = default;
        ~D3DEmoteModule() override = default;

        void setMaskMode(int value) { _maskMode = value; }
        [[nodiscard]] int getMaskMode() const { return _maskMode; }

        void setMaskRegionClipping(bool value) {
            _maskRegionClipping = value;
        }
        [[nodiscard]] bool getMaskRegionClipping() const {
            return _maskRegionClipping;
        }

        void setMipMapEnabled(bool value) { _mipMapEnabled = value; }
        [[nodiscard]] bool getMipMapEnabled() const {
            return _mipMapEnabled;
        }

        void setAlphaOp(int value) { _alphaOp = value; }
        [[nodiscard]] int getAlphaOp() const { return _alphaOp; }

        void setProtectTranslucentTextureColor(bool value) {
            _protectTranslucentTextureColor = value;
        }
        [[nodiscard]] bool getProtectTranslucentTextureColor() const {
            return _protectTranslucentTextureColor;
        }

        // Separate from Player::pixelateDivision.
        void setPixelateDivision(int value) { _pixelateDivision = value; }
        [[nodiscard]] int getPixelateDivision() const {
            return _pixelateDivision;
        }

        void setMaxTextureSize(int width, int height) {
            _maxTextureWidth = width;
            _maxTextureHeight = height;
        }

    private:
        int _maskMode = 1;
        bool _maskRegionClipping = false;
        bool _mipMapEnabled = true;
        bool _protectTranslucentTextureColor = false;
        int _alphaOp = 0;
        int _pixelateDivision = 100;
        int _maxTextureWidth = 0;
        int _maxTextureHeight = 0;
    };

    static_assert(
        sizeof(D3DEmoteModule) == (sizeof(void *) == 8 ? 0x20u : 0x1Cu),
        "D3DEmoteModule layout must match the four reference ABIs");

} // namespace motion
