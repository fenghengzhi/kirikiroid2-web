/**
 * DrawDeviceD3D — Full iTVPDrawDevice implementation for web build
 *
 * In libkrkr2.so, DrawDeviceD3D.dll provides D3D/OpenGL-based rendering.
 * Game scripts (MainWindow.tjs initD3D) create a DrawDeviceD3D instance
 * and set it as window.drawDevice, which sets isD3D=true.
 *
 * This implementation wraps the existing PassThroughDrawDevice, delegating
 * all calls to it. The key difference: it returns ITSELF as the `interface`
 * property, so SetDrawDeviceObject accepts it as a "new" device (different
 * pointer from the existing PassThroughDrawDevice).
 *
 * Reference: krkrz/krkr2 DrawDeviceD3D plugin + DrawDeviceForSteam wrapper
 */
#define NCB_MODULE_NAME TJS_W("DrawDeviceD3D.dll")

#include <algorithm>
#include <vector>
#include <spdlog/spdlog.h>
#include "ncbind.hpp"
#include "tjs.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "DrawDevice.h"
#include "visual/WindowIntf.h"
#include "StorageIntf.h"

// ---------------------------------------------------------------------------
// DrawDeviceD3D: thin wrapper around the existing draw device
// ---------------------------------------------------------------------------
class DrawDeviceD3D : public iTVPDrawDevice {
    iTVPDrawDevice *Real = nullptr;
    iTVPWindow *Window = nullptr;
    std::vector<iTVPLayerManager*> myManagers;

public:
    DrawDeviceD3D() {
        if (TVPMainWindow)
            Real = TVPMainWindow->GetDrawDevice();
        spdlog::get("core")->warn("DrawDeviceD3D created: this={} Real={}",
                                  (void*)static_cast<iTVPDrawDevice*>(this), (void*)Real);
        TVPAddLog(TJS_W("(info) DrawDeviceD3D created (wrapper around PassThroughDrawDevice)"));
    }

    ~DrawDeviceD3D() = default;

    static tjs_error factory(DrawDeviceD3D **result, tjs_int numparams,
                             tTJSVariant **params, iTJSDispatch2 *) {
        *result = new DrawDeviceD3D();
        TVPAddLog(ttstr(TJS_W("(info) DrawDeviceD3D factory called, numparams=")) +
            ttstr((tjs_int)numparams));
        return TJS_S_OK;
    }

    // The `interface` property must return THIS wrapper's pointer as int64.
    // This is different from the existing DrawDevice pointer, so
    // Window.SetDrawDeviceObject will accept it as a new device.
    tjs_int64 getInterface() {
        return reinterpret_cast<tjs_int64>(static_cast<iTVPDrawDevice *>(this));
    }

    // Properties matching the reference DrawDeviceD3D
    void setStretchType(int) {}
    int getStretchType() { return 0; }
    void setBicubicParam(double) {}
    double getBicubicParam() { return 0.0; }
    void setLowSpec(int) {}
    int getLowSpec() { return 0; }
    void setDefaultVisible(bool v) { _defaultVisible = v; }
    bool getDefaultVisible() { return _defaultVisible; }

    // primaryLayers property — in libkrkr2.so this returns an array-like
    // object of registered primary layers. The game script accesses it
    // as an object (e.g., drawDevice.primaryLayers.count).
    // Return an empty TJS array to satisfy the object type check.
    tTJSVariant getPrimaryLayers() {
        iTJSDispatch2 *arr = TJSCreateArrayObject();
        tTJSVariant result(arr, arr);
        arr->Release();
        return result;
    }

    tjs_int64 getDevice() {
        return reinterpret_cast<tjs_int64>(static_cast<iTVPDrawDevice *>(this));
    }

    // D3DLayer visibility management (referenced by game scripts)
    void setVisible(int id, bool v) {}
    bool getVisible(int id) { return true; }

    void setSize(int w, int h) {}

    // Methods called by MainWindow.tjs during D3D initialization and usage
    bool checkEnable() { return true; }
    void recreate() {}
    void setPrimarySize(int w, int h) {}
    void setScreenRect(tTJSVariant) {}
    tTJSVariant getScreenRect() { return tTJSVariant(); }
    void fillRect(tTJSVariant) {}
    void copyRect(tTJSVariant) {}
    void setClearColor(int c) {}
    int getClearColor() { return 0; }
    void setForceRenderTexture(bool v) {}
    bool getForceRenderTexture() { return false; }

private:
    bool _defaultVisible = true;

    // ---------------------------------------------------------------
    // iTVPDrawDevice interface — delegate everything to Real
    // ---------------------------------------------------------------
    void Destruct() override { /* don't destroy Real; it's shared */ }

    void SetWindowInterface(iTVPWindow *window) override {
        Window = window;
        // Real may have been invalidated during drawDevice swap.
        // Re-acquire the current draw device from the window.
        if (window && TVPMainWindow) {
            Real = TVPMainWindow->GetDrawDevice();
            if (Real == static_cast<iTVPDrawDevice*>(this))
                Real = nullptr; // don't delegate to ourselves
        }
    }

    void AddLayerManager(iTVPLayerManager *m) override {
        myManagers.push_back(m);
        if (Real) Real->AddLayerManager(m);
    }
    void RemoveLayerManager(iTVPLayerManager *m) override {
        myManagers.erase(std::remove(myManagers.begin(), myManagers.end(), m), myManagers.end());
        if (Real) Real->RemoveLayerManager(m);
    }

    void SetDestRectangle(const tTVPRect &r) override {
        if (Real) Real->SetDestRectangle(r);
    }
    void SetWindowSize(tjs_int w, tjs_int h) override {
        if (Real) Real->SetWindowSize(w, h);
    }
    void SetClipRectangle(const tTVPRect &r) override {
        if (Real) Real->SetClipRectangle(r);
    }
    void GetSrcSize(tjs_int &w, tjs_int &h) override {
        if (Real) Real->GetSrcSize(w, h);
    }
    void NotifyLayerResize(iTVPLayerManager *m) override {
        if (Real) Real->NotifyLayerResize(m);
    }
    void NotifyLayerImageChange(iTVPLayerManager *m) override {
        if (Real) Real->NotifyLayerImageChange(m);
    }

    // Input events
    void OnClick(tjs_int x, tjs_int y) override {
        if (Real) Real->OnClick(x, y);
    }
    void OnDoubleClick(tjs_int x, tjs_int y) override {
        if (Real) Real->OnDoubleClick(x, y);
    }
    void OnMouseDown(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 f) override {
        if (Real) Real->OnMouseDown(x, y, mb, f);
    }
    void OnMouseUp(tjs_int x, tjs_int y, tTVPMouseButton mb, tjs_uint32 f) override {
        if (Real) Real->OnMouseUp(x, y, mb, f);
    }
    void OnMouseMove(tjs_int x, tjs_int y, tjs_uint32 f) override {
        if (Real) Real->OnMouseMove(x, y, f);
    }
    void OnReleaseCapture() override {
        if (Real) Real->OnReleaseCapture();
    }
    void OnMouseOutOfWindow() override {
        if (Real) Real->OnMouseOutOfWindow();
    }
    void OnKeyDown(tjs_uint k, tjs_uint32 s) override {
        if (Real) Real->OnKeyDown(k, s);
    }
    void OnKeyUp(tjs_uint k, tjs_uint32 s) override {
        if (Real) Real->OnKeyUp(k, s);
    }
    void OnKeyPress(tjs_char k) override {
        if (Real) Real->OnKeyPress(k);
    }
    void OnMouseWheel(tjs_uint32 s, tjs_int d, tjs_int x, tjs_int y) override {
        if (Real) Real->OnMouseWheel(s, d, x, y);
    }
    void OnTouchDown(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id) override {
        if (Real) Real->OnTouchDown(x, y, cx, cy, id);
    }
    void OnTouchUp(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id) override {
        if (Real) Real->OnTouchUp(x, y, cx, cy, id);
    }
    void OnTouchMove(tjs_real x, tjs_real y, tjs_real cx, tjs_real cy, tjs_uint32 id) override {
        if (Real) Real->OnTouchMove(x, y, cx, cy, id);
    }
    void OnTouchScaling(tjs_real sd, tjs_real cd, tjs_real cx, tjs_real cy, tjs_int f) override {
        if (Real) Real->OnTouchScaling(sd, cd, cx, cy, f);
    }
    void OnTouchRotate(tjs_real sa, tjs_real ca, tjs_real d, tjs_real cx, tjs_real cy, tjs_int f) override {
        if (Real) Real->OnTouchRotate(sa, ca, d, cx, cy, f);
    }
    void OnMultiTouch() override {
        if (Real) Real->OnMultiTouch();
    }
    void OnDisplayRotate(tjs_int o, tjs_int r, tjs_int b, tjs_int w, tjs_int h) override {
        if (Real) Real->OnDisplayRotate(o, r, b, w, h);
    }
    void RecheckInputState() override {
        if (Real) Real->RecheckInputState();
    }
    void SetDefaultMouseCursor(iTVPLayerManager *m) override {
        if (Real) Real->SetDefaultMouseCursor(m);
    }
    void SetMouseCursor(iTVPLayerManager *m, tjs_int c) override {
        if (Real) Real->SetMouseCursor(m, c);
    }
    void GetCursorPos(iTVPLayerManager *m, tjs_int &x, tjs_int &y) override {
        if (Real) Real->GetCursorPos(m, x, y);
    }
    void SetCursorPos(iTVPLayerManager *m, tjs_int x, tjs_int y) override {
        if (Real) Real->SetCursorPos(m, x, y);
    }
    void WindowReleaseCapture(iTVPLayerManager *m) override {
        if (Real) Real->WindowReleaseCapture(m);
    }
    void SetHintText(iTVPLayerManager *m, iTJSDispatch2 *s, const ttstr &t) override {
        if (Real) Real->SetHintText(m, s, t);
    }
    void SetAttentionPoint(iTVPLayerManager *m, tTJSNI_BaseLayer *l, tjs_int x, tjs_int y) override {
        if (Real) Real->SetAttentionPoint(m, l, x, y);
    }
    void DisableAttentionPoint(iTVPLayerManager *m) override {
        if (Real) Real->DisableAttentionPoint(m);
    }
    void SetImeMode(iTVPLayerManager *m, tTVPImeMode mode) override {
        if (Real) Real->SetImeMode(m, mode);
    }
    void ResetImeMode(iTVPLayerManager *m) override {
        if (Real) Real->ResetImeMode(m);
    }
    tTJSNI_BaseLayer *GetPrimaryLayer() override {
        return Real ? Real->GetPrimaryLayer() : nullptr;
    }
    tTJSNI_BaseLayer *GetFocusedLayer() override {
        return Real ? Real->GetFocusedLayer() : nullptr;
    }
    void SetFocusedLayer(tTJSNI_BaseLayer *l) override {
        if (Real) Real->SetFocusedLayer(l);
    }
    void RequestInvalidation(const tTVPRect &r) override {
        if (Real) Real->RequestInvalidation(r);
    }
    void Update() override {
        if (Real) Real->Update();
    }
    void Show() override {
        static int showCnt = 0;
        if(showCnt < 5) {
            spdlog::get("core")->warn("DrawDeviceD3D::Show Real={} Window={} myManagers={}",
                                      (void*)Real, (void*)Window, myManagers.size());
            showCnt++;
        }
        if (Real) Real->Show();
        if(Window) {
            iWindowLayer *form = Window->GetForm();
            if(form && !myManagers.empty()) {
                iTVPBaseBitmap *buf = myManagers.back()->GetDrawBuffer();
                if(buf) form->UpdateDrawBuffer(buf->GetTexture());
            }
        }
    }
    void StartBitmapCompletion(iTVPLayerManager *m) override {
        if (Real) Real->StartBitmapCompletion(m);
    }
    void NotifyBitmapCompleted(iTVPLayerManager *m, tjs_int x, tjs_int y,
                               tTVPBaseTexture *bmp, const tTVPRect &cr,
                               tTVPLayerType t, tjs_int o) override {
        if (Real) Real->NotifyBitmapCompleted(m, x, y, bmp, cr, t, o);
    }
    void EndBitmapCompletion(iTVPLayerManager *m) override {
        if (Real) Real->EndBitmapCompletion(m);
    }
    void DumpLayerStructure() override {
        if (Real) Real->DumpLayerStructure();
    }
    void SetShowUpdateRect(bool b) override {
        if (Real) Real->SetShowUpdateRect(b);
    }
    void Clear() override {
        if (Real) Real->Clear();
    }
    bool SwitchToFullScreen(int w, tjs_uint a, tjs_uint b, tjs_uint c,
                            tjs_uint d, bool e) override {
        return Real ? Real->SwitchToFullScreen(w, a, b, c, d, e) : false;
    }
    void RevertFromFullScreen(int w, tjs_uint a, tjs_uint b, tjs_uint c,
                              tjs_uint d) override {
        if (Real) Real->RevertFromFullScreen(w, a, b, c, d);
    }
    bool WaitForVBlank(tjs_int *a, tjs_int *b) override {
        return Real ? Real->WaitForVBlank(a, b) : false;
    }
};

// ---------------------------------------------------------------------------
// Stub subclasses referenced by libkrkr2.so's DrawDeviceD3D.dll
// Game scripts access these as global classes (e.g., new D3DLayer())
// ---------------------------------------------------------------------------
class D3DLayer {
public:
    D3DLayer() = default;

    // Properties
    void setVisible(bool v) { _visible = v; }
    bool getVisible() { return _visible; }
    void setFrontIndex(int v) { _frontIndex = v; }
    int getFrontIndex() { return _frontIndex; }
    void setBackIndex(int v) { _backIndex = v; }
    int getBackIndex() { return _backIndex; }
    void setDrawPlane(int v) { _drawPlane = v; }
    int getDrawPlane() { return _drawPlane; }

    // Methods (stubs)
    void setMatrix(tTJSVariant) {}
    void setMatrixGL(tTJSVariant) {}
    void setClip(tTJSVariant) {}

private:
    bool _visible = true;
    int _frontIndex = 0;
    int _backIndex = 0;
    int _drawPlane = 3; // DrawPlaneBoth
};

class D3DImage {
public:
    D3DImage() = default;
};

class D3DPicture {
public:
    D3DPicture() = default;
};

// ---------------------------------------------------------------------------
// NCB Registration
// ---------------------------------------------------------------------------
NCB_REGISTER_CLASS(DrawDeviceD3D) {
    Factory(&DrawDeviceD3D::factory);
    Property(TJS_W("interface"), &DrawDeviceD3D::getInterface, int());
    NCB_PROPERTY(stretchType, getStretchType, setStretchType);
    NCB_PROPERTY(bicubicParam, getBicubicParam, setBicubicParam);
    NCB_PROPERTY(lowSpec, getLowSpec, setLowSpec);
    NCB_PROPERTY(defaultVisible, getDefaultVisible, setDefaultVisible);
    NCB_PROPERTY_RO(primaryLayers, getPrimaryLayers);
    NCB_METHOD(getDevice);
    NCB_METHOD(setVisible);
    NCB_METHOD(getVisible);
    NCB_METHOD(setSize);
    NCB_METHOD(checkEnable);
    NCB_METHOD(recreate);
    NCB_METHOD(setPrimarySize);
    NCB_METHOD(setScreenRect);
    NCB_METHOD(getScreenRect);
    NCB_METHOD(fillRect);
    NCB_METHOD(copyRect);
    NCB_PROPERTY(clearColor, getClearColor, setClearColor);
    NCB_PROPERTY(forceRenderTexture, getForceRenderTexture, setForceRenderTexture);
}

// Register as top-level classes (game scripts access them globally)
NCB_REGISTER_CLASS(D3DLayer) {
    NCB_CONSTRUCTOR(());
    // Constants
    Variant(TJS_W("DrawPlaneFront"), (tjs_int)1);
    Variant(TJS_W("DrawPlaneBack"), (tjs_int)2);
    Variant(TJS_W("DrawPlaneBoth"), (tjs_int)3);
    // Properties
    NCB_PROPERTY(visible, getVisible, setVisible);
    NCB_PROPERTY(frontIndex, getFrontIndex, setFrontIndex);
    NCB_PROPERTY(backIndex, getBackIndex, setBackIndex);
    NCB_PROPERTY(drawPlane, getDrawPlane, setDrawPlane);
    // Methods
    NCB_METHOD(setMatrix);
    NCB_METHOD(setMatrixGL);
    NCB_METHOD(setClip);
}
NCB_REGISTER_CLASS(D3DImage) { NCB_CONSTRUCTOR(()); }
NCB_REGISTER_CLASS(D3DPicture) { NCB_CONSTRUCTOR(()); }

// Post-registration: define ShortCutInitialPadKeyMap on global scope.
// In D3D mode, keybinder.tjs accesses .ShortCutInitialPadKeyMap (= global member)
// before it's defined by default.tjs. Pre-define as empty dictionaries.

// Also register as DrawDeviceD3DZ.dll (libkrkr2.so registers both)
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("DrawDeviceD3DZ.dll")
static void DrawDeviceD3DZ_PreRegist() {}
NCB_PRE_REGIST_CALLBACK(DrawDeviceD3DZ_PreRegist);
