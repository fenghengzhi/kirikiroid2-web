/**
 * DrawDeviceD3D.dll software-renderer compositor.
 *
 * Android libkrkr2.so evidence:
 *   factory/common ctor       sub_52B274 / sub_530E94
 *   embedded draw device      sub_52B66C (interface = root + 376)
 *   manager attach/detach     sub_531390 / sub_531444
 *   manager property refresh  sub_532C50 / sub_5327EC
 *   software texture upload   sub_532B1C
 *   manager composition       sub_5328F4
 *   frame composition/show    sub_5314B0
 *
 * The ARM64 object offsets are analysis evidence only.  This source restores
 * the source-level topology: a script-owned compositor object containing a
 * tTVPDrawDevice adapter, std::vector manager ownership in tTVPDrawDevice,
 * ordered manager maps in the compositor, and one software upload cache per
 * manager.
 */
#define NCB_MODULE_NAME TJS_W("DrawDeviceD3D.dll")

#include <algorithm>
#include <cstring>
#include <list>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "DrawDevice.h"
#include "LayerBitmapIntf.h"
#include "LayerManager.h"
#include "RenderManager.h"
#include "ncbind.hpp"
#include "tjs.h"
#include "tjsArray.h"
#include "visual/WindowIntf.h"
#include "visual/impl/LayerImpl.h"

class DrawDeviceObjectBase;
class D3DLayerObject;
class DrawDeviceManagerItem;
class D3DPicture;

namespace {

iTVPRenderManager *GetD3DRenderManager() {
    // sub_5323C0 @ 0x5323C0 caches the render manager named "opengl".
    // The process-wide renderer may still be "software": that setting selects
    // the CPU layer path, while DrawDeviceD3D uses this private manager for
    // target textures and composition.
    static iTVPRenderManager *manager =
        TVPGetRenderManager(TJS_W("opengl"));
    return manager;
}

tjs_int32 GetD3DLayerBaseClassID() {
    static const tjs_int32 id = [] {
        const tjs_int32 existing = TJSFindNativeClassID(TJS_W("D3DLayerBase"));
        return existing >= 0 ? existing
                             : TJSRegisterNativeClass(TJS_W("D3DLayerBase"));
    }();
    return id;
}

tjs_int32 GetD3DLayerObjectClassID() {
    static const tjs_int32 id = [] {
        const tjs_int32 existing =
            TJSFindNativeClassID(TJS_W("D3DLayerObjectNativeInstance"));
        return existing >= 0
                   ? existing
                   : TJSRegisterNativeClass(
                         TJS_W("D3DLayerObjectNativeInstance"));
    }();
    return id;
}

class D3DLayerBaseNativeInstance final : public tTJSNativeInstance {
    DrawDeviceObjectBase *Instance;
    bool Sticky = true;

public:
    explicit D3DLayerBaseNativeInstance(DrawDeviceObjectBase *instance)
        : Instance(instance) {}

    void Invalidate() override;
    void Destruct() override;
    DrawDeviceObjectBase *Get() const { return Instance; }
    void Detach() { Instance = nullptr; }
};

class D3DLayerObjectNativeInstance final : public tTJSNativeInstance {
    D3DLayerObject *Instance;

public:
    explicit D3DLayerObjectNativeInstance(D3DLayerObject *instance)
        : Instance(instance) {}

    void Invalidate() override {}
    void Destruct() override;
    D3DLayerObject *Get() const { return Instance; }
    void Detach() { Instance = nullptr; }
};

void RegisterD3DLayerBaseNative(iTJSDispatch2 *owner,
                                DrawDeviceObjectBase *instance) {
    auto *adaptor = new D3DLayerBaseNativeInstance(instance);
    iTJSNativeInstance *native = adaptor;
    if(!owner || TJS_FAILED(owner->NativeInstanceSupport(
                      TJS_NIS_REGISTER, GetD3DLayerBaseClassID(), &native))) {
        adaptor->Detach();
        delete adaptor;
        TVPThrowExceptionMessage(TJS_W("Adaptor registration failed."));
    }
}

void RegisterD3DLayerObjectNative(iTJSDispatch2 *owner,
                                  D3DLayerObject *instance) {
    auto *adaptor = new D3DLayerObjectNativeInstance(instance);
    iTJSNativeInstance *native = adaptor;
    if(!owner || TJS_FAILED(owner->NativeInstanceSupport(
                      TJS_NIS_REGISTER, GetD3DLayerObjectClassID(), &native))) {
        adaptor->Detach();
        delete adaptor;
        TVPThrowExceptionMessage(TJS_W("Adaptor registration failed."));
    }
}

DrawDeviceObjectBase *GetD3DLayerBase(iTJSDispatch2 *object) {
    iTJSNativeInstance *native = nullptr;
    if(!object || TJS_FAILED(object->NativeInstanceSupport(
                      TJS_NIS_GETINSTANCE, GetD3DLayerBaseClassID(), &native)))
        return nullptr;
    return static_cast<D3DLayerBaseNativeInstance *>(native)->Get();
}

D3DLayerObject *GetD3DLayerObject(iTJSDispatch2 *object) {
    iTJSNativeInstance *native = nullptr;
    if(!object || TJS_FAILED(object->NativeInstanceSupport(
                      TJS_NIS_GETINSTANCE, GetD3DLayerObjectClassID(), &native)))
        return nullptr;
    return static_cast<D3DLayerObjectNativeInstance *>(native)->Get();
}

void DetachD3DLayerBaseNative(iTJSDispatch2 *owner) {
    iTJSNativeInstance *native = nullptr;
    if(owner && TJS_SUCCEEDED(owner->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, GetD3DLayerBaseClassID(), &native)))
        static_cast<D3DLayerBaseNativeInstance *>(native)->Detach();
}

void DetachD3DLayerObjectNative(iTJSDispatch2 *owner) {
    iTJSNativeInstance *native = nullptr;
    if(owner && TJS_SUCCEEDED(owner->NativeInstanceSupport(
                    TJS_NIS_GETINSTANCE, GetD3DLayerObjectClassID(), &native)))
        static_cast<D3DLayerObjectNativeInstance *>(native)->Detach();
}


tjs_int GetIntProperty(iTJSDispatch2 *object, const tjs_char *name,
                       tjs_int defaultValue) {
    if(!object)
        return defaultValue;

    tTJSVariant value;
    const tjs_error error = object->PropGet(
        TJS_MEMBERMUSTEXIST, name, nullptr, &value, object);
    if(error == TJS_E_MEMBERNOTFOUND)
        return defaultValue;
    if(TJS_FAILED(error))
        return defaultValue;
    return static_cast<tjs_int>(value);
}

bool GetVisibleProperty(iTJSDispatch2 *object) {
    if(!object)
        return false;

    tTJSVariant value;
    const tjs_error error = object->PropGet(
        TJS_MEMBERMUSTEXIST, TJS_W("drawvisible"), nullptr, &value, object);
    if(error == TJS_E_MEMBERNOTFOUND)
        return true;
    if(TJS_FAILED(error))
        return false;
    return static_cast<bool>(value);
}

} // namespace

class DrawDeviceAdapter final : public tTVPDrawDevice {
    DrawDeviceObjectBase *Owner;

public:
    explicit DrawDeviceAdapter(DrawDeviceObjectBase *owner) : Owner(owner) {}
    ~DrawDeviceAdapter() override = default;

    void DetachOwner() { Owner = nullptr; }
    void SetPrimaryManagerIndex(size_t index) { PrimaryLayerManagerIndex = index; }
    size_t GetPrimaryManagerIndex() const { return PrimaryLayerManagerIndex; }
    const tTVPRect &GetDestRect() const { return DestRect; }

    void Destruct() override;
    void AddLayerManager(iTVPLayerManager *manager) override;
    void RemoveLayerManager(iTVPLayerManager *manager) override;
    void Show() override;

    // off_19FD9F8 keeps these completion callbacks as no-ops.  Rendering is
    // pulled from each manager's completed draw buffer by Show().
    void StartBitmapCompletion(iTVPLayerManager *) override {}
    void NotifyBitmapCompleted(iTVPLayerManager *, tjs_int, tjs_int,
                               tTVPBaseTexture *, const tTVPRect &,
                               tTVPLayerType, tjs_int) override {}
    void EndBitmapCompletion(iTVPLayerManager *) override {}
    void Clear() override {}
};

class D3DLayerObject {
    friend class DrawDeviceObjectBase;

    iTJSDispatch2 *ScriptOwner;
    DrawDeviceObjectBase *Parent = nullptr;
    tjs_int FrontIndex = 0;
    tjs_int BackIndex = 0;
    tjs_int DrawPlane = 1;

protected:
    std::list<D3DLayerObject *> Listeners;

    D3DLayerObject(iTJSDispatch2 *owner, DrawDeviceObjectBase *parent)
        : ScriptOwner(owner), Parent(parent) {}

    iTJSDispatch2 *GetScriptOwner() const { return ScriptOwner; }
    DrawDeviceObjectBase *GetParent() const { return Parent; }

public:
    virtual ~D3DLayerObject();

    virtual bool IsVisible() const = 0;
    virtual void Draw() = 0;
    virtual void OnParentHasParent() {}
    virtual void OnDetached() {}

    void AddListener(D3DLayerObject *listener) {
        if(listener)
            Listeners.push_back(listener);
    }
    void RemoveListener(D3DLayerObject *listener) {
        if(listener)
            Listeners.remove(listener);
    }
    bool OnUpdate(const tTJSVariant &state);
    virtual bool TransformPoint(float &, float &) const { return false; }

    tjs_int getFrontIndex() const { return FrontIndex; }
    void setFrontIndex(tjs_int value);
    tjs_int getBackIndex() const { return BackIndex; }
    void setBackIndex(tjs_int value);
    tjs_int getDrawPlane() const { return DrawPlane; }
    void setDrawPlane(tjs_int value) { DrawPlane = value & 3; }
};

class DrawDeviceObjectBase {
    friend class DrawDeviceAdapter;
    friend class D3DLayerObject;
    friend class DrawDeviceManagerItem;
    friend class D3DPicture;

    using ItemMap = std::multimap<tjs_int, D3DLayerObject *>;

    iTJSDispatch2 *ScriptOwner; // no AddRef, identical to the NCB owner link
    DrawDeviceObjectBase *Parent = nullptr;

    // sub_530E94 @ 0x530E94 constructs these four containers in this order.
    ItemMap FrontItems;
    ItemMap BackItems;
    std::set<D3DPicture *> ManagedObjects;
    std::map<tjs_uint32, void *> Modules;

    tjs_uint32 ClearColor; // deliberately not initialized by sub_530E94
    bool TransitionActive = false;
    tjs_int TransitionMethod = 0;
    float TransitionState = 0.0f;
    tjs_int TransitionVague = 0;
    iTVPTexture2D *TransitionRuleTexture = nullptr;
    iTVPTexture2D *FrontTarget = nullptr;
    iTVPTexture2D *BackTarget = nullptr;
    iTVPTexture2D *CurrentTarget = nullptr;
    tjs_int OffsetX = 0;
    tjs_int OffsetY = 0;
    tjs_int StretchType = 2;
    float BicubicParam = -0.5f;
    bool ForceRenderTexture = false;
    bool RenderTextureDirty = false;

    tjs_int PrimaryWidth;
    tjs_int PrimaryHeight;
    tjs_int ScreenLeft = 0;
    tjs_int ScreenTop = 0;
    tjs_int ScreenWidth;
    tjs_int ScreenHeight;
    tjs_int UpdateState = 0;

    DrawDeviceAdapter Device;

    bool EraseFront(D3DLayerObject *object) {
        const auto range = FrontItems.equal_range(object->FrontIndex);
        for(auto it = range.first; it != range.second; ++it) {
            if(it->second == object) {
                FrontItems.erase(it);
                return true;
            }
        }
        return false;
    }

    bool EraseBack(D3DLayerObject *object) {
        const auto range = BackItems.equal_range(object->BackIndex);
        for(auto it = range.first; it != range.second; ++it) {
            if(it->second == object) {
                BackItems.erase(it);
                return true;
            }
        }
        return false;
    }

    void ReleaseTargets() {
        if(FrontTarget) {
            FrontTarget->Release();
            FrontTarget = nullptr;
        }
        if(BackTarget) {
            BackTarget->Release();
            BackTarget = nullptr;
        }
        CurrentTarget = nullptr;
    }

    void EnsureTargets() {
        if(BackTarget && BackTarget->GetWidth() >= static_cast<tjs_uint>(PrimaryWidth) &&
           BackTarget->GetHeight() >= static_cast<tjs_uint>(PrimaryHeight))
            return;

        ReleaseTargets();
        auto *manager = GetD3DRenderManager();
        FrontTarget = manager->CreateTexture2D(
            nullptr, 0, static_cast<tjs_uint>(PrimaryWidth),
            static_cast<tjs_uint>(PrimaryHeight), TVPTextureFormat::RGBA, 0);
        BackTarget = manager->CreateTexture2D(
            nullptr, 0, static_cast<tjs_uint>(PrimaryWidth),
            static_cast<tjs_uint>(PrimaryHeight), TVPTextureFormat::RGBA, 0);
    }

    void RequestWindowUpdate() {
        if(Device.GetWindow())
            Device.GetWindow()->RequestUpdate();
    }

    void FillTarget(iTVPTexture2D *target) {
        auto *manager = GetD3DRenderManager();
        iTVPRenderMethod *method = manager->GetRenderMethod("FillARGB");
        const int colorId = method->EnumParameterID("color");
        method->SetParameterColor4B(colorId, ClearColor);
        const tTVPRect rect(0, 0, static_cast<tjs_int>(target->GetWidth()),
                            static_cast<tjs_int>(target->GetHeight()));
        manager->OperateRect(method, target, target, rect,
                             tRenderTexRectArray());
    }

protected:
    DrawDeviceObjectBase(tjs_int width, tjs_int height, iTJSDispatch2 *owner)
        : ScriptOwner(owner), PrimaryWidth(width), PrimaryHeight(height),
          ScreenWidth(width), ScreenHeight(height), Device(this) {
        // sub_530E94 @ 0x530E94 registers the same native root through the
        // independent "D3DLayerBase" class id.
        RegisterD3DLayerBaseNative(owner, this);
    }

    iTJSDispatch2 *GetScriptOwner() const { return ScriptOwner; }
    virtual void DetachTJSAdaptor() = 0;

public:
    virtual ~DrawDeviceObjectBase();

    void DestroyFromAdapter() {
        DetachTJSAdaptor();
        DetachD3DLayerBaseNative(ScriptOwner);
        delete this;
    }

    tjs_int64 getInterface() {
        // sub_52B66C: the script object is not itself an iTVPDrawDevice.
        return reinterpret_cast<tjs_int64>(
            static_cast<iTVPDrawDevice *>(&Device));
    }

    tjs_uint32 getClearColor() const { return ClearColor; }
    void setClearColor(tjs_uint32 color) { ClearColor = color; }

    float getTransState() const { return TransitionState; }
    void setTransState(float state) {
        TransitionState = std::max(0.0f, std::min(1.0f, state));
    }

    tTJSVariant getChildren() const {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tjs_int index = 0;
        for(const auto &entry : FrontItems) {
            iTJSDispatch2 *owner = entry.second->GetScriptOwner();
            if(!owner || owner->IsValid(0, nullptr, nullptr, owner) != TJS_S_TRUE)
                continue;
            tTJSVariant value(owner, owner);
            array->PropSetByNum(TJS_MEMBERENSURE, index++, &value, array);
        }
        tTJSVariant result(array, array);
        array->Release();
        return result;
    }

    void AddChild(D3DLayerObject *object) {
        if(object) {
            object->OnDetached();
            if(Parent)
                object->OnParentHasParent();
            FrontItems.emplace(object->FrontIndex, object);
            BackItems.emplace(object->BackIndex, object);
        }
        // Root vtable slot +24 is nullsub_249 @ 0x5312B8.
    }

    void add(tTJSVariant child) {
        if(child.Type() == tvtObject)
            AddChild(GetD3DLayerObject(child.AsObjectNoAddRef()));
        else
            AddChild(nullptr);
    }

    void remove(tTJSVariant child) {
        if(child.Type() != tvtObject)
            return;
        D3DLayerObject *object =
            GetD3DLayerObject(child.AsObjectNoAddRef());
        if(!object)
            return;
        const bool removedFront = EraseFront(object);
        const bool removedBack = EraseBack(object);
        if(removedFront || removedBack)
            object->OnDetached();
    }

    void startTransition(tTJSVariant options) {
        iTJSDispatch2 *object = options.AsObjectNoAddRef();
        tTJSVariant method;
        const bool hasMethod =
            object && TJS_SUCCEEDED(object->PropGet(
                          TJS_MEMBERMUSTEXIST, TJS_W("method"), nullptr,
                          &method, object));
        if(hasMethod) {
            TransitionMethod =
                ttstr(method.AsStringNoAddRef()) == TJS_W("universal") ? 1 : 0;
            if(TransitionMethod == 1) {
                TransitionVague = 64;
                tTJSVariant vague;
                if(TJS_SUCCEEDED(object->PropGet(
                       TJS_MEMBERMUSTEXIST, TJS_W("vague"), nullptr,
                       &vague, object)))
                    TransitionVague = static_cast<tjs_int>(vague);

                if(TransitionRuleTexture) {
                    TransitionRuleTexture->Release();
                    TransitionRuleTexture = nullptr;
                }
                tTJSVariant rule;
                if(TJS_SUCCEEDED(object->PropGet(
                       TJS_MEMBERMUSTEXIST, TJS_W("rule"), nullptr,
                       &rule, object))) {
                    tTJSNI_Layer *layer = tTJSNI_Layer::FromVariant(rule);
                    if(layer && layer->GetMainImage()) {
                        TransitionRuleTexture =
                            layer->GetMainImage()->GetTexture();
                        if(TransitionRuleTexture)
                            TransitionRuleTexture->AddRef();
                    }
                }
            }
        } else {
            TransitionMethod = 0;
        }
        TransitionActive = true;
        TransitionState = 1.0f;
    }

    void stopTransition() {
        TransitionActive = false;
        TransitionMethod = -1;
        TransitionState = 0.0f;
        if(TransitionRuleTexture) {
            TransitionRuleTexture->Release();
            TransitionRuleTexture = nullptr;
        }
    }

    tjs_int getOffsetX() const { return OffsetX; }
    void setOffsetX(tjs_int value) { OffsetX = value; }
    tjs_int getOffsetY() const { return OffsetY; }
    void setOffsetY(tjs_int value) { OffsetY = value; }
    void setOffset(tjs_int x, tjs_int y) {
        OffsetX = x;
        OffsetY = y;
    }

    tjs_int getStretchType() const { return StretchType; }
    void setStretchType(tjs_int value) { StretchType = value; }
    float getBicubicParam() const { return BicubicParam; }
    void setBicubicParam(float value) { BicubicParam = value; }
    bool getForceRenderTexture() const { return ForceRenderTexture; }
    void setForceRenderTexture(bool value) {
        ForceRenderTexture = value;
        RenderTextureDirty = true;
    }

    void setPrimarySize(tjs_int width, tjs_int height) {
        PrimaryWidth = width;
        PrimaryHeight = height;
        if(Device.GetWindow())
            Device.GetWindow()->NotifySrcResize();
    }
    tjs_int getPrimaryWidth() const { return PrimaryWidth; }
    tjs_int getPrimaryHeight() const { return PrimaryHeight; }

    void setScreenRect(tjs_int left, tjs_int top, tjs_int width,
                       tjs_int height) {
        ScreenLeft = left;
        ScreenTop = top;
        if(ScreenWidth != width || ScreenHeight != height) {
            ScreenWidth = width;
            ScreenHeight = height;
            ReleaseTargets();
            RenderTextureDirty = true;
        }
    }
    tjs_int getScreenLeft() const { return ScreenLeft; }
    void setScreenLeft(tjs_int value) { ScreenLeft = value; }
    tjs_int getScreenTop() const { return ScreenTop; }
    void setScreenTop(tjs_int value) { ScreenTop = value; }
    tjs_int getScreenWidth() const { return ScreenWidth; }
    void setScreenWidth(tjs_int value) {
        if(ScreenWidth != value) {
            ScreenWidth = value;
            ReleaseTargets();
            RenderTextureDirty = true;
        }
    }
    tjs_int getScreenHeight() const { return ScreenHeight; }
    void setScreenHeight(tjs_int value) {
        if(ScreenHeight != value) {
            ScreenHeight = value;
            ReleaseTargets();
            RenderTextureDirty = true;
        }
    }

    tTJSVariant getPrimaryLayers() const {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        tjs_int index = 0;
        for(auto *manager : Device.GetManagers()) {
            tTJSNI_BaseLayer *layer = manager->GetPrimaryLayer();
            if(!layer || !layer->GetOwnerNoAddRef())
                continue;
            tTJSVariant value(layer->GetOwnerNoAddRef(),
                              layer->GetOwnerNoAddRef());
            array->PropSetByNum(TJS_MEMBERENSURE, index++, &value, array);
        }
        tTJSVariant result(array, array);
        array->Release();
        return result;
    }

    tjs_int getLayerManagerIndex() const {
        return static_cast<tjs_int>(Device.GetPrimaryManagerIndex());
    }
    void setLayerManagerIndex(tjs_int index) {
        if(index < 0 || Device.GetManagers().size() <=
                            static_cast<size_t>(index))
            TVPThrowExceptionMessage(TJS_W("invalid layer manager index."));
        Device.SetPrimaryManagerIndex(static_cast<size_t>(index));
    }

    tjs_int getDestLeft() const { return Device.GetDestRect().left; }
    tjs_int getDestTop() const { return Device.GetDestRect().top; }
    tjs_int getDestWidth() const { return Device.GetDestRect().get_width(); }
    tjs_int getDestHeight() const { return Device.GetDestRect().get_height(); }

    bool checkEnable() const { return false; }
    tTJSVariant getModule() const { return tTJSVariant(); }

    bool capture(tTJSVariant targetLayer, tjs_int frontIndexLimit) {
        tTJSVariant state(static_cast<tjs_int>(0));
        for(const auto &entry : FrontItems) {
            D3DLayerObject *object = entry.second;
            if(object->IsVisible())
                object->OnUpdate(state);
        }

        auto *manager = GetD3DRenderManager();
        iTVPTexture2D *target = manager->CreateTexture2D(
            nullptr, 0, static_cast<tjs_uint>(PrimaryWidth),
            static_cast<tjs_uint>(PrimaryHeight), TVPTextureFormat::RGBA, 0);
        CurrentTarget = target;
        for(const auto &entry : FrontItems) {
            D3DLayerObject *object = entry.second;
            if(object->IsVisible() &&
               (!frontIndexLimit || object->getFrontIndex() < frontIndexLimit) &&
               (object->getDrawPlane() & 1) != 0)
                object->Draw();
        }

        tTJSNI_Layer *layer = tTJSNI_Layer::FromVariant(targetLayer);
        if(layer) {
            const tjs_uint width = target->GetWidth();
            const tjs_uint height = target->GetHeight();
            layer->SetSize(width, height);
            tTVPBaseTexture *bitmap = layer->GetMainImage();
            const auto *source = static_cast<const unsigned char *>(
                target->GetScanLineForRead(0));
            const tjs_int sourcePitch = target->GetPitch();
            if(bitmap && source && sourcePitch > 0) {
                const size_t rowBytes = static_cast<size_t>(width) * 4;
                for(tjs_uint y = 0; y < height; ++y) {
                    auto *destination = static_cast<unsigned char *>(
                        bitmap->GetScanLineForWrite(y));
                    std::memcpy(destination, source + sourcePitch * y,
                                rowBytes);
                }
            }
        }
        target->Release();
        CurrentTarget = nullptr;
        return true;
    }

    void getPrimaryLayerBitmap(tjs_int index, tTJSVariant targetLayer) {
        if(index < 0 || Device.GetManagers().size() <=
                            static_cast<size_t>(index))
            TVPThrowExceptionMessage(TJS_W("invalid layer manager index."));
        tTJSNI_BaseLayer *source =
            Device.GetManagers()[static_cast<size_t>(index)]->GetPrimaryLayer();
        tTJSNI_Layer *target = tTJSNI_Layer::FromVariant(targetLayer);
        if(source && source->GetMainImage() && target)
            target->AssignTexture(source->GetMainImage()->GetTexture());
    }
    void update(tjs_int state) {
        UpdateState = state;
        RequestWindowUpdate();
    }

    void Show();
};

void D3DLayerBaseNativeInstance::Invalidate() {
    if(Instance && !Sticky)
        delete Instance;
    Instance = nullptr;
    Sticky = false;
}

void D3DLayerBaseNativeInstance::Destruct() {
    DrawDeviceObjectBase *instance = Instance;
    Instance = nullptr;
    if(instance)
        delete instance;
    delete this;
}

void D3DLayerObjectNativeInstance::Destruct() {
    D3DLayerObject *instance = Instance;
    Instance = nullptr;
    if(instance)
        delete instance;
    delete this;
}

D3DLayerObject::~D3DLayerObject() {
    if(Parent) {
        const bool removedFront = Parent->EraseFront(this);
        const bool removedBack = Parent->EraseBack(this);
        if(removedFront || removedBack)
            OnDetached();
    }
}

void D3DLayerObject::setFrontIndex(tjs_int value) {
    if(FrontIndex == value)
        return;
    if(!Parent) {
        FrontIndex = value;
        return;
    }
    Parent->EraseFront(this);
    FrontIndex = value;
    Parent->FrontItems.emplace(FrontIndex, this);
}

void D3DLayerObject::setBackIndex(tjs_int value) {
    if(BackIndex == value)
        return;
    if(!Parent) {
        BackIndex = value;
        return;
    }
    Parent->EraseBack(this);
    BackIndex = value;
    Parent->BackItems.emplace(BackIndex, this);
}

bool D3DLayerObject::OnUpdate(const tTJSVariant &state) {
    if(ScriptOwner) {
        tTJSVariant parameter(state);
        tTJSVariant *parameters[] = {&parameter};
        ScriptOwner->FuncCall(0, TJS_W("onUpdate"), nullptr, nullptr, 1,
                              parameters, ScriptOwner);
    }

    bool result = false;
    for(D3DLayerObject *listener : Listeners) {
        const bool listenerResult = listener->IsVisible();
        result = result || listenerResult;
    }
    return result;
}

class DrawDeviceManagerItem final : public D3DLayerObject {
    iTVPLayerManager *Manager;
    tTJSNI_BaseLayer *PrimaryLayer;
    iTJSDispatch2 *PrimaryOwner;
    iTVPTexture2D *SoftwareTexture = nullptr;

    iTVPTexture2D *GetSoftwareTexture(iTVPBaseBitmap *bitmap) {
        if(!bitmap || !bitmap->GetTexture())
            return nullptr;

        iTVPTexture2D *source = bitmap->GetTexture();
        const tjs_uint width = source->GetWidth();
        const tjs_uint height = source->GetHeight();
        const void *pixels = source->GetScanLineForRead(0);
        const tjs_int pitch = source->GetPitch();
        if(!pixels || pitch <= 0)
            return nullptr;

        if(SoftwareTexture && SoftwareTexture->GetWidth() == width &&
           SoftwareTexture->GetHeight() == height) {
            SoftwareTexture->Update(
                pixels, TVPTextureFormat::RGBA, pitch,
                tTVPRect(0, 0, pitch / 4,
                         static_cast<tjs_int>(height)));
            return SoftwareTexture;
        }

        if(SoftwareTexture)
            SoftwareTexture->Release();
        SoftwareTexture = GetD3DRenderManager()->CreateTexture2D(
            pixels, pitch, width, height, TVPTextureFormat::RGBA, 0);
        return SoftwareTexture;
    }

public:
    DrawDeviceManagerItem(DrawDeviceObjectBase *owner,
                          iTVPLayerManager *manager)
        : D3DLayerObject(nullptr, owner), Manager(manager),
          PrimaryLayer(manager->GetPrimaryLayer()),
          PrimaryOwner(PrimaryLayer ? PrimaryLayer->GetOwnerNoAddRef()
                                    : nullptr) {
        if(PrimaryOwner)
            PrimaryOwner->AddRef();
        UpdateSettings();

        // sub_53249C initializes the primary layer image through FillARGB(0).
        if(PrimaryLayer && PrimaryLayer->GetMainImage()) {
            auto *image = PrimaryLayer->GetMainImage();
            image->Fill(tTVPRect(0, 0, static_cast<tjs_int>(image->GetWidth()),
                                 static_cast<tjs_int>(image->GetHeight())),
                        0);
        }
        owner->AddChild(this);
    }

    ~DrawDeviceManagerItem() {
        if(SoftwareTexture)
            SoftwareTexture->Release();
        if(PrimaryOwner)
            PrimaryOwner->Release();
    }

    void UpdateSettings() {
        const tjs_int drawPlane =
            GetIntProperty(PrimaryOwner, TJS_W("drawPlane"), 0) & 3;
        const tjs_int frontIndex =
            GetIntProperty(PrimaryOwner, TJS_W("frontIndex"), 0);
        const tjs_int backIndex =
            GetIntProperty(PrimaryOwner, TJS_W("backIndex"), 0);

        setDrawPlane(drawPlane);
        setFrontIndex(frontIndex);
        setBackIndex(backIndex);
    }

    bool IsVisible() const override { return GetVisibleProperty(PrimaryOwner); }

    void Draw() override {
        DrawDeviceObjectBase *owner = GetParent();
        if(!owner || !owner->CurrentTarget || !Manager)
            return;

        iTVPBaseBitmap *drawBuffer = Manager->GetDrawBuffer();
        if(!drawBuffer)
            return;

        const tjs_int type =
            GetIntProperty(PrimaryOwner, TJS_W("type"), 0);
        const tjs_int opacity =
            GetIntProperty(PrimaryOwner, TJS_W("drawOpacity"), 255);
        const tjs_int x = GetIntProperty(PrimaryOwner, TJS_W("offsetX"), 0);
        const tjs_int y = GetIntProperty(PrimaryOwner, TJS_W("offsetY"), 0);

        auto *renderManager = GetD3DRenderManager();
        iTVPRenderMethod *method =
            renderManager->GetRenderMethod(opacity, true, type);
        if(!method)
            return;

        iTVPTexture2D *source = GetSoftwareTexture(drawBuffer);
        if(!source)
            return;

        const tTVPRect sourceRect(
            0, 0, static_cast<tjs_int>(source->GetWidth()),
            static_cast<tjs_int>(source->GetHeight()));
        const tTVPRect targetRect(
            x, y, static_cast<tjs_int>(source->GetWidth()),
            static_cast<tjs_int>(source->GetHeight()));
        std::pair<iTVPTexture2D *, tTVPRect> textures[] = {
            {source, sourceRect}};
        renderManager->OperateRect(
            method, owner->CurrentTarget, owner->CurrentTarget, targetRect,
            tRenderTexRectArray(textures));
    }
};

DrawDeviceObjectBase::~DrawDeviceObjectBase() {
    Device.DetachOwner();

    const std::vector<iTVPLayerManager *> managers = Device.GetManagers();
    for(auto *manager : managers) {
        auto *item = static_cast<DrawDeviceManagerItem *>(
            manager->GetDrawDeviceData());
        if(item) {
            manager->SetDrawDeviceData(nullptr);
            delete item;
        }
    }

    ReleaseTargets();
    if(TransitionRuleTexture)
        TransitionRuleTexture->Release();
}

void DrawDeviceObjectBase::Show() {
    if(!Device.GetWindow())
        return;

    tTJSVariant state(UpdateState);
    for(const auto &entry : FrontItems) {
        D3DLayerObject *object = entry.second;
        if(object->IsVisible())
            object->OnUpdate(state);
    }
    UpdateState = 0;

    for(auto *manager : Device.GetManagers()) {
        auto *item = static_cast<DrawDeviceManagerItem *>(
            manager->GetDrawDeviceData());
        if(item)
            item->UpdateSettings();
    }

    EnsureTargets();
    if(!BackTarget)
        return;

    if(TransitionActive) {
        FillTarget(FrontTarget);
        FillTarget(BackTarget);

        CurrentTarget = FrontTarget;
        for(const auto &entry : FrontItems) {
            D3DLayerObject *item = entry.second;
            if(item->IsVisible() && (item->getDrawPlane() & 1) != 0)
                item->Draw();
        }

        CurrentTarget = BackTarget;
        for(const auto &entry : BackItems) {
            D3DLayerObject *item = entry.second;
            if(item->IsVisible() && (item->getDrawPlane() & 2) != 0)
                item->Draw();
        }

        // sub_5314B0 obtains AlphaBlend_SD from the private "opengl" manager
        // and passes TransitionState through its float parameter slot.
        if(TransitionActive) {
            auto *manager = GetD3DRenderManager();
            iTVPRenderMethod *method =
                manager->GetRenderMethod("AlphaBlend_SD");
            const int opacityId = method->EnumParameterID("opacity");
            method->SetParameterFloat(opacityId, TransitionState);
            const tTVPRect targetRect(
                0, 0, static_cast<tjs_int>(FrontTarget->GetWidth()),
                static_cast<tjs_int>(FrontTarget->GetHeight()));
            std::pair<iTVPTexture2D *, tTVPRect> textures[] = {
                {FrontTarget, targetRect}};
            manager->OperateRect(method, BackTarget, BackTarget, targetRect,
                                 tRenderTexRectArray(textures));
        }
    } else {
        CurrentTarget = BackTarget;
        for(const auto &entry : FrontItems) {
            D3DLayerObject *item = entry.second;
            if(item->IsVisible() && (item->getDrawPlane() & 1) != 0)
                item->Draw();
        }
    }

    CurrentTarget = nullptr;
    if(iWindowLayer *form = Device.GetWindow()->GetForm())
        form->UpdateDrawBuffer(BackTarget);
}

void DrawDeviceAdapter::Destruct() {
    DrawDeviceObjectBase *owner = Owner;
    Owner = nullptr;
    if(owner)
        owner->DestroyFromAdapter();
}

void DrawDeviceAdapter::AddLayerManager(iTVPLayerManager *manager) {
    tTVPDrawDevice::AddLayerManager(manager);
    manager->SetDesiredLayerType(static_cast<tTVPLayerType>(0));
    auto *item = new DrawDeviceManagerItem(Owner, manager);
    manager->SetDrawDeviceData(item);
}

void DrawDeviceAdapter::RemoveLayerManager(iTVPLayerManager *manager) {
    auto *item = static_cast<DrawDeviceManagerItem *>(
        manager->GetDrawDeviceData());
    if(item) {
        manager->SetDrawDeviceData(nullptr);
        delete item;
    }
    tTVPDrawDevice::RemoveLayerManager(manager);
}

void DrawDeviceAdapter::Show() {
    if(Owner)
        Owner->Show();
}

class DrawDeviceD3D final : public DrawDeviceObjectBase {
public:
    DrawDeviceD3D(tjs_int width, tjs_int height, iTJSDispatch2 *owner)
        : DrawDeviceObjectBase(width, height, owner) {}

    static tjs_error factory(DrawDeviceD3D **result, tjs_int numparams,
                             tTJSVariant **params, iTJSDispatch2 *objthis) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        ncbInstanceAdaptor<DrawDeviceD3D>::GetAdaptor(objthis, true)->setSticky();
        *result = new DrawDeviceD3D(static_cast<tjs_int>(*params[0]),
                                    static_cast<tjs_int>(*params[1]), objthis);
        return TJS_S_OK;
    }

protected:
    void DetachTJSAdaptor() override {
        ncbInstanceAdaptor<DrawDeviceD3D>::SetNativeInstance(GetScriptOwner(),
                                                             nullptr);
    }
};

class D3DLayer final : public DrawDeviceObjectBase {
public:
    D3DLayer(tjs_int width, tjs_int height, iTJSDispatch2 *owner)
        : DrawDeviceObjectBase(width, height, owner) {}

    static tjs_error factory(D3DLayer **result, tjs_int numparams,
                             tTJSVariant **params, iTJSDispatch2 *objthis) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        ncbInstanceAdaptor<D3DLayer>::GetAdaptor(objthis, true)->setSticky();
        *result = new D3DLayer(static_cast<tjs_int>(*params[0]),
                               static_cast<tjs_int>(*params[1]), objthis);
        return TJS_S_OK;
    }

protected:
    void DetachTJSAdaptor() override {
        ncbInstanceAdaptor<D3DLayer>::SetNativeInstance(GetScriptOwner(),
                                                        nullptr);
    }
};

class D3DImage final : public D3DLayerObject {
    bool Visible = true;
    float Clip[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float Matrix[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, 0.0f,
                        0.0f, 0.0f, 1.0f, 0.0f,
                        0.0f, 0.0f, 0.0f, 1.0f};

    void NotifyMatrixChanged() {
        // sub_52D198/sub_52D248 notify each listener through vtable +16.
        for(D3DLayerObject *listener : Listeners)
            (void)listener->IsVisible();
    }

public:
    D3DImage(iTJSDispatch2 *owner, DrawDeviceObjectBase *parent)
        : D3DLayerObject(owner, parent) {
        RegisterD3DLayerObjectNative(owner, this);
        if(parent)
            parent->AddChild(this);
    }

    ~D3DImage() override {
        DetachD3DLayerObjectNative(GetScriptOwner());
    }

    static tjs_error factory(D3DImage **result, tjs_int numparams,
                             tTJSVariant **params, iTJSDispatch2 *objthis) {
        if(numparams < 1 || (*params[0]).Type() != tvtObject)
            return TJS_E_BADPARAMCOUNT;
        DrawDeviceObjectBase *parent =
            GetD3DLayerBase((*params[0]).AsObjectNoAddRef());
        if(!parent)
            return TJS_E_INVALIDTYPE;
        ncbInstanceAdaptor<D3DImage>::GetAdaptor(objthis, true)->setSticky();
        *result = new D3DImage(objthis, parent);
        return TJS_S_OK;
    }

    bool IsVisible() const override { return Visible; }
    void setVisible(bool value) { Visible = value; }

    void Draw() override {
        if(!GetParent() || !Visible)
            return;
        for(D3DLayerObject *listener : Listeners)
            listener->Draw();
    }

    bool TransformPoint(float &x, float &y) const override {
        x = Matrix[12] + static_cast<float>(GetParent()->getScreenWidth() / 2) +
            x * Matrix[0];
        y = Matrix[13] + static_cast<float>(GetParent()->getScreenHeight() / 2) +
            y * Matrix[5];
        return true;
    }

    void setMatrix(float m0, float m1, float m2, float m3,
                   float m4, float m5, float m6, float m7,
                   float m8, float m9, float m10, float m11,
                   float m12, float m13, float m14, float m15) {
        const float values[16] = {m0, m4, m8,  m12, m1, m5, m9,  m13,
                                  m2, m6, m10, m14, m3, m7, m11, m15};
        std::copy(values, values + 16, Matrix);
        NotifyMatrixChanged();
    }

    void setMatrixGL(float m0, float m1, float m2, float m3,
                     float m4, float m5, float m6, float m7,
                     float m8, float m9, float m10, float m11,
                     float m12, float m13, float m14, float m15) {
        const float values[16] = {m0, m1, m2, m3, m4,  m5,  m6,  m7,
                                  m8, m9, m10,m11,m12, m13, m14, m15};
        std::copy(values, values + 16, Matrix);
        NotifyMatrixChanged();
    }

    void setClip(float left, float top, float right, float bottom) {
        Clip[0] = left;
        Clip[1] = top;
        Clip[2] = right;
        Clip[3] = bottom;
    }
};

class D3DPicture final {
    struct TextureReference {
        iTVPTexture2D *Texture;
        explicit TextureReference(iTVPTexture2D *texture) : Texture(texture) {
            if(Texture)
                Texture->AddRef();
        }
        ~TextureReference() {
            if(Texture)
                Texture->Release();
        }
    };

    DrawDeviceObjectBase *Owner;
    TextureReference *Picture = nullptr;

public:
    explicit D3DPicture(DrawDeviceObjectBase *owner) : Owner(owner) {
        if(Owner)
            Owner->ManagedObjects.insert(this);
    }

    ~D3DPicture() {
        delete Picture;
        Picture = nullptr;
        if(Owner)
            Owner->ManagedObjects.erase(this);
    }

    static tjs_error factory(D3DPicture **result, tjs_int numparams,
                             tTJSVariant **params, iTJSDispatch2 *) {
        if(numparams < 1 || (*params[0]).Type() != tvtObject)
            return TJS_E_BADPARAMCOUNT;
        DrawDeviceObjectBase *owner =
            GetD3DLayerBase((*params[0]).AsObjectNoAddRef());
        if(!owner)
            return TJS_E_INVALIDTYPE;
        *result = new D3DPicture(owner);
        return TJS_S_OK;
    }

    tjs_uint getWidth() const {
        return Picture && Picture->Texture ? Picture->Texture->GetWidth() : 0;
    }
    tjs_uint getHeight() const {
        return Picture && Picture->Texture ? Picture->Texture->GetHeight() : 0;
    }

    void load(tTJSVariant sourceLayer) {
        tTJSNI_Layer *layer = tTJSNI_Layer::FromVariant(sourceLayer);
        iTVPTexture2D *source =
            layer && layer->GetMainImage()
                ? layer->GetMainImage()->GetTexture()
                : nullptr;
        if(!source)
            return;

        iTVPTexture2D *loaded = source;
        if(TVPGetRenderManager()->IsSoftware()) {
            const void *pixels = source->GetScanLineForRead(0);
            const tjs_int pitch = source->GetPitch();
            loaded = GetD3DRenderManager()->CreateTexture2D(
                pixels, pitch, source->GetWidth(), source->GetHeight(),
                TVPTextureFormat::RGBA, 0);
        }

        // sub_52D7A0 @ 0x52D7A0 overwrites +16 without destroying a previous
        // holder. Preserve that boundary behavior if load is called repeatedly.
        Picture = new TextureReference(loaded);
        if(loaded != source)
            loaded->Release();
    }
};

#define REGISTER_DRAW_DEVICE_MEMBERS(CLASS)                                  \
    Factory(&CLASS::factory);                                                \
    Property(TJS_W("interface"), &CLASS::getInterface, int());              \
    NCB_PROPERTY_RO(children, getChildren);                                  \
    NCB_PROPERTY(clearColor, getClearColor, setClearColor);                  \
    NCB_PROPERTY(transState, getTransState, setTransState);                  \
    NCB_METHOD(add);                                                         \
    NCB_METHOD(remove);                                                      \
    NCB_METHOD(startTransition);                                             \
    NCB_METHOD(stopTransition);                                              \
    NCB_PROPERTY(offsetX, getOffsetX, setOffsetX);                            \
    NCB_PROPERTY(offsetY, getOffsetY, setOffsetY);                            \
    NCB_METHOD(setOffset);                                                   \
    NCB_PROPERTY(stretchType, getStretchType, setStretchType);               \
    NCB_PROPERTY(bicubicParam, getBicubicParam, setBicubicParam);            \
    NCB_PROPERTY(forceRenderTexture, getForceRenderTexture,                  \
                 setForceRenderTexture);                                    \
    NCB_METHOD(setPrimarySize);                                              \
    NCB_PROPERTY_RO(primaryWidth, getPrimaryWidth);                          \
    NCB_PROPERTY_RO(primaryHeight, getPrimaryHeight);                        \
    NCB_METHOD(setScreenRect);                                               \
    NCB_PROPERTY(screenLeft, getScreenLeft, setScreenLeft);                  \
    NCB_PROPERTY(screenTop, getScreenTop, setScreenTop);                     \
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);               \
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);            \
    NCB_PROPERTY_RO(primaryLayers, getPrimaryLayers);                        \
    NCB_PROPERTY(layerManagerIndex, getLayerManagerIndex,                    \
                 setLayerManagerIndex);                                     \
    NCB_PROPERTY_RO(destLeft, getDestLeft);                                  \
    NCB_PROPERTY_RO(destTop, getDestTop);                                    \
    NCB_PROPERTY_RO(destWidth, getDestWidth);                                \
    NCB_PROPERTY_RO(destHeight, getDestHeight);                              \
    NCB_METHOD(update);                                                      \
    NCB_METHOD(checkEnable);                                                 \
    NCB_METHOD(getModule);                                                   \
    NCB_METHOD(capture);                                                     \
    NCB_METHOD(getPrimaryLayerBitmap)

NCB_REGISTER_CLASS(DrawDeviceD3D) { REGISTER_DRAW_DEVICE_MEMBERS(DrawDeviceD3D); }

NCB_REGISTER_CLASS(D3DLayer) {
    REGISTER_DRAW_DEVICE_MEMBERS(D3DLayer);
    Variant(TJS_W("DrawPlaneFront"), static_cast<tjs_int>(1));
    Variant(TJS_W("DrawPlaneBack"), static_cast<tjs_int>(2));
    Variant(TJS_W("DrawPlaneBoth"), static_cast<tjs_int>(3));
}

NCB_REGISTER_CLASS(D3DImage) {
    Factory(&D3DImage::factory);
    Variant(TJS_W("DrawPlaneFront"), static_cast<tjs_int>(1));
    Variant(TJS_W("DrawPlaneBack"), static_cast<tjs_int>(2));
    Variant(TJS_W("DrawPlaneBoth"), static_cast<tjs_int>(3));
    NCB_PROPERTY(visible, IsVisible, setVisible);
    NCB_PROPERTY(frontIndex, getFrontIndex, setFrontIndex);
    NCB_PROPERTY(backIndex, getBackIndex, setBackIndex);
    NCB_PROPERTY(drawPlane, getDrawPlane, setDrawPlane);
    NCB_METHOD(setMatrix);
    NCB_METHOD(setMatrixGL);
    NCB_METHOD(setClip);
}

NCB_REGISTER_CLASS(D3DPicture) {
    Factory(&D3DPicture::factory);
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
    NCB_METHOD(load);
}

#undef REGISTER_DRAW_DEVICE_MEMBERS

// libkrkr2.so exposes the companion module name as well.
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("DrawDeviceD3DZ.dll")
static void DrawDeviceD3DZ_PreRegist() {}
NCB_PRE_REGIST_CALLBACK(DrawDeviceD3DZ_PreRegist);
