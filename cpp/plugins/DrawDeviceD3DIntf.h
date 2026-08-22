#pragma once

#include <list>

#include "math/Mat4.h"
#include "tjs.h"

class DrawDeviceObjectBase;
class iTVPTexture2D;
class D3DLayerObject;
class D3DLayer;

// The stripped references pass this eight-byte value by const reference to
// D3DLayerObject::Draw.  Every producer copies two root float fields into it
// without integer conversion.  The original type spelling is unavailable.
struct D3DPoint_guess {
    float x;
    float y;
};

// Exact source spelling is not present in the four stripped references.  The
// DrawDevice root owns pointers to objects with a virtual deleting destructor.
class D3DModuleBase_guess {
public:
    virtual ~D3DModuleBase_guess() = default;
};

// Listener base consumed by D3DLayer.  The owner link and the two scalar
// defaults are part of this base in all four references; construction and
// destruction register/unregister the listener through the owner.  The
// scalar pair is exposed by D3DPicture as stretchType/bicubicParam, while
// D3DEmotePlayer leaves it at the constructor defaults.
class D3DLayerListener {
    D3DLayer *_d3dLayerOwner;
    tjs_int _stretchType = 8;
    float _bicubicParam = -0.5f;

protected:
    explicit D3DLayerListener(D3DLayer *owner);
    D3DLayer *GetD3DLayerOwner() const { return _d3dLayerOwner; }
    tjs_int getStretchType() const { return _stretchType; }
    void setStretchType(tjs_int value) { _stretchType = value; }
    double getBicubicParam() const { return _bicubicParam; }
    void setBicubicParam(double value) {
        _bicubicParam = static_cast<float>(value);
    }

public:
    virtual ~D3DLayerListener();
    virtual bool IsVisible() = 0;                    // vtable +16
    virtual void Draw(iTVPTexture2D *target) = 0;    // vtable +24
};

class D3DLayerObject {
    friend class DrawDeviceObjectBase;

    iTJSDispatch2 *ScriptOwner;
    DrawDeviceObjectBase *Parent = nullptr;
    tjs_int FrontIndex = 0;
    tjs_int BackIndex = 0;
    tjs_int DrawPlane = 1;

protected:
    std::list<D3DLayerListener *> Listeners;

    explicit D3DLayerObject(iTJSDispatch2 *owner);

    iTJSDispatch2 *GetScriptOwner() const { return ScriptOwner; }
    DrawDeviceObjectBase *GetParent() const { return Parent; }
    iTVPTexture2D *GetParentDrawTarget() const;
    void SetParent_guess(DrawDeviceObjectBase *parent);

public:
    virtual ~D3DLayerObject();

    virtual bool IsVisible() = 0;
    virtual void Draw(const D3DPoint_guess &offset) = 0;

    virtual void OnParentHasParent() {}
    virtual void OnDetached() {}
    virtual void AddListener(D3DLayerListener *listener);
    virtual void RemoveListener(D3DLayerListener *listener);
    virtual bool OnUpdate(tjs_int updateState,
                          const tTJSVariant &state);
    virtual bool TransformPoint(float &, float &) const = 0;

    D3DModuleBase_guess *FindParentModule_guess(tjs_uint32 classId) const;
    void SetParentModule_guess(tjs_uint32 classId,
                               D3DModuleBase_guess *module);

    tjs_int getFrontIndex() const { return FrontIndex; }
    void setFrontIndex(tjs_int value);
    tjs_int getBackIndex() const { return BackIndex; }
    void setBackIndex(tjs_int value);
    tjs_int getDrawPlane() const { return DrawPlane; }
    void setDrawPlane(tjs_int value) { DrawPlane = value & 3; }
};

class D3DLayer final : public D3DLayerObject {
    bool Visible = true;
    float Clip[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    cocos2d::Mat4 Matrix;

    void NotifyMatrixChanged();

public:
    D3DLayer(iTJSDispatch2 *owner, DrawDeviceObjectBase *parent);
    ~D3DLayer() override;

    static tjs_error factory(D3DLayer **result, tjs_int numparams,
                             tTJSVariant **params, iTJSDispatch2 *objthis);

    bool IsVisible() override { return Visible; }
    void setVisible(bool value) { Visible = value; }
    float GetScaleXForListener() const { return Matrix.m[0]; }
    void CopyMatrixForListener(float (&result)[16]) const;

    void Draw(const D3DPoint_guess &offset) override;
    bool TransformPoint(float &x, float &y) const override;

    void setMatrix(float m0, float m1, float m2, float m3,
                   float m4, float m5, float m6, float m7,
                   float m8, float m9, float m10, float m11,
                   float m12, float m13, float m14, float m15);
    void setMatrixGL(float m0, float m1, float m2, float m3,
                     float m4, float m5, float m6, float m7,
                     float m8, float m9, float m10, float m11,
                     float m12, float m13, float m14, float m15);
    void setClip(float left, float top, float right, float bottom);
};

// D3DEmotePlayer's update listener reads D3DLayer's matrix scale directly.
float TVPGetD3DLayerScaleX(const D3DLayer *object);

static_assert(sizeof(D3DLayerListener) ==
                  (sizeof(void *) == 8 ? 0x18u : 0x10u),
              "D3DLayer listener base must match the four-reference layout");
