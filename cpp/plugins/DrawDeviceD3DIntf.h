#pragma once

#include <list>

#include "tjs.h"

class DrawDeviceObjectBase;
class iTVPTexture2D;

// The first four virtual slots are the listener interface consumed by
// D3DImage.  libkrkr2.so D3DEmotePlayer's temporary/final vtables at
// off_19FE050/off_19FE020 contain exactly these slots.
class D3DLayerListener {
public:
    virtual ~D3DLayerListener() = default;
    virtual bool IsVisible() = 0;                    // vtable +16
    virtual void Draw(iTVPTexture2D *target) = 0;    // vtable +24
};

class D3DLayerObject : public D3DLayerListener {
    friend class DrawDeviceObjectBase;

    iTJSDispatch2 *ScriptOwner;
    DrawDeviceObjectBase *Parent = nullptr;
    tjs_int FrontIndex = 0;
    tjs_int BackIndex = 0;
    tjs_int DrawPlane = 1;

protected:
    std::list<D3DLayerListener *> Listeners;

    D3DLayerObject(iTJSDispatch2 *owner, DrawDeviceObjectBase *parent)
        : ScriptOwner(owner), Parent(parent) {}

    iTJSDispatch2 *GetScriptOwner() const { return ScriptOwner; }
    DrawDeviceObjectBase *GetParent() const { return Parent; }
    iTVPTexture2D *GetParentDrawTarget() const;

public:
    ~D3DLayerObject() override;

    virtual void OnParentHasParent() {}
    virtual void OnDetached() {}
    virtual void AddListener(D3DLayerListener *listener);
    virtual void RemoveListener(D3DLayerListener *listener);
    virtual bool OnUpdate(const tTJSVariant &state);
    virtual bool TransformPoint(float &, float &) const { return false; }

    tjs_int getFrontIndex() const { return FrontIndex; }
    void setFrontIndex(tjs_int value);
    tjs_int getBackIndex() const { return BackIndex; }
    void setBackIndex(tjs_int value);
    tjs_int getDrawPlane() const { return DrawPlane; }
    void setDrawPlane(tjs_int value) { DrawPlane = value & 3; }
};

// sub_5428D8 returns the raw D3DImage native object from a script instance.
D3DLayerObject *TVPGetD3DImageNative(iTJSDispatch2 *object);

// D3DEmotePlayer_OnUpdate @0x533CBC reads D3DImage's matrix scale directly.
float TVPGetD3DImageScaleX(const D3DLayerObject *object);
