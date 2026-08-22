/**
 * DrawDeviceD3D.dll software-renderer compositor.
 *
 * The Android arm64/armv7 and iOS arm64/armv7 reference binaries agree on the
 * source-level topology restored here: seven auto-registered script classes,
 * two common-root classes with tTVPDrawDevice as a secondary base, ordered
 * layer multisets, a raw D3DImage set, an owning module map, and listener-driven
 * drawing.
 * Platform-specific addresses and object offsets remain in analysis documents
 * only; this compiled source records the cross-reference semantics.
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
#include "DrawDeviceD3DIntf.h"
#include "LayerBitmapIntf.h"
#include "LayerManager.h"
#include "RenderManager.h"
#include "ncbind.hpp"
#include "motionplayer/D3DEmoteModule.h"
#include "motionplayer/EmotePlayer.h"
#include "tjs.h"
#include "tjsArray.h"
#include "tjsUtils.h"
#include "visual/WindowIntf.h"
#include "visual/impl/LayerImpl.h"

using motion::D3DEmoteModule;
using motion::D3DEmotePlayer;

class DrawDeviceObjectBase;
class DrawDeviceManagerItem;
class D3DImage;
class D3DPicture;

namespace {

iTVPRenderManager *GetD3DRenderManager() {
    // All four references cache the render manager named "opengl".
    // The process-wide renderer may still be "software": that setting selects
    // the CPU layer path, while DrawDeviceD3D uses this private manager for
    // target textures and composition.
    static iTVPRenderManager *manager =
        TVPGetRenderManager(TJS_W("opengl"));
    return manager;
}

using D3DLayerBaseClassInfo = ncbClassInfo<DrawDeviceObjectBase>;

tjs_int32 GetD3DLayerBaseClassID() {
    // The references keep a full NCBind ClassInfo tuple for this internal
    // identity even though it has no script class object.  PreRegist publishes
    // the ID before any root can be built; lookups do not lazily find/register
    // a friendlier replacement ID.
    return D3DLayerBaseClassInfo::GetID();
}

tjs_int32 D3DLayerObjectClassID = 0;

static_assert(TJS_MAX_NATIVE_CLASS == 4,
              "the four-reference D3DLayerObject re-entry boundary uses four native slots");

tjs_int32 GetD3DLayerObjectClassID() {
    // Unlike D3DLayerBase, this borrowed-view identity is only one process
    // global class-ID word.  It likewise becomes valid in PreRegist.
    return D3DLayerObjectClassID;
}

class D3DLayerBaseNativeInstance final : public tTJSNativeInstance {
    // NCBind-shaped adaptor: the primary-base constructor registers the root
    // here and then marks this independent view sticky.  The concrete class
    // adaptor remains non-sticky and is the sole owner of the root object.
    DrawDeviceObjectBase *Instance = nullptr;
    bool Sticky = false;

    void DeleteInstance();

public:
    explicit D3DLayerBaseNativeInstance(DrawDeviceObjectBase *instance)
        : Instance(instance) {}
    ~D3DLayerBaseNativeInstance() override;

    void Invalidate() override;
    DrawDeviceObjectBase *Get() const { return Instance; }
    void Set(DrawDeviceObjectBase *instance) {
        if(Instance)
            DeleteInstance();
        Instance = instance;
    }
    void SetSticky() { Sticky = true; }
};

class D3DLayerObjectNativeInstance final : public tTJSNativeInstance {
    // This smaller adaptor is only a borrowed lookup view.  It inherits the
    // empty Invalidate and self-deleting Destruct implementations; neither
    // path deletes or clears the pointed-to D3DLayerObject.  The two fields
    // are exactly {vptr, borrowed D3DLayerObject *} on all four ABIs.
    D3DLayerObject *Instance;

public:
    explicit D3DLayerObjectNativeInstance(D3DLayerObject *instance)
        : Instance(instance) {}

    D3DLayerObject *Get() const { return Instance; }
};

D3DLayerBaseNativeInstance *GetD3DLayerBaseAdaptor(iTJSDispatch2 *owner) {
    iTJSNativeInstance *native = nullptr;
    if(!owner || TJS_FAILED(owner->NativeInstanceSupport(
                      TJS_NIS_GETINSTANCE, GetD3DLayerBaseClassID(), &native)))
        return nullptr;
    return static_cast<D3DLayerBaseNativeInstance *>(native);
}

bool RegisterD3DLayerBaseNative(iTJSDispatch2 *owner,
                                DrawDeviceObjectBase *instance) {
    // This is the generated SetAdaptorWithNativeInstance state machine.  A
    // populated adaptor first discards its old slot (deleting only a
    // non-sticky root); an empty adaptor deliberately retains its sticky bit.
    // REGISTER failure leaves the allocated/reused adaptor and new pointer in
    // their partially published state.  The primary-base constructor ignores
    // this bool and performs a strict GET immediately afterward.
    D3DLayerBaseNativeInstance *adaptor =
        GetD3DLayerBaseAdaptor(owner);
    if(adaptor)
        adaptor->Set(instance);
    else {
        adaptor = new D3DLayerBaseNativeInstance(instance);
    }
    iTJSNativeInstance *native = adaptor;
    return TJS_SUCCEEDED(owner->NativeInstanceSupport(
        TJS_NIS_REGISTER, GetD3DLayerBaseClassID(), &native));
}

void RegisterD3DLayerObjectNative(iTJSDispatch2 *owner,
                                  D3DLayerObject *instance) {
    auto *adaptor = new D3DLayerObjectNativeInstance(instance);
    iTJSNativeInstance *native = adaptor;
    // Registration status is deliberately ignored by all four references.
    // tTJSCustomObject appends without deduplicating into its first free slot;
    // once all four slots are occupied it returns TJS_E_FAIL.  This raw
    // adaptor was already allocated, and the ignored failure leaks it.
    owner->NativeInstanceSupport(TJS_NIS_REGISTER,
                                 GetD3DLayerObjectClassID(), &native);
}

DrawDeviceObjectBase *GetD3DLayerBase(iTJSDispatch2 *object) {
    D3DLayerBaseNativeInstance *adaptor =
        GetD3DLayerBaseAdaptor(object);
    return adaptor ? adaptor->Get() : nullptr;
}

D3DLayerObject *GetD3DLayerObject(iTJSDispatch2 *object) {
    iTJSNativeInstance *native = nullptr;
    // GET scans slots from zero upward.  Re-entering a D3DLayer constructor
    // appends another equal class ID, but root add/remove keep unwrapping the
    // oldest borrowed view rather than the newest concrete D3DLayer.
    if(!object || TJS_FAILED(object->NativeInstanceSupport(
                      TJS_NIS_GETINSTANCE, GetD3DLayerObjectClassID(), &native)))
        return nullptr;
    return static_cast<D3DLayerObjectNativeInstance *>(native)->Get();
}

bool GetVisibleProperty(iTJSDispatch2 *object) {
    if(!object)
        return false;

    static tjs_uint32 drawVisibleHint = 0;
    tTJSVariant value;
    const tjs_error error = object->PropGet(
        TJS_MEMBERMUSTEXIST, TJS_W("drawvisible"), &drawVisibleHint,
        &value, object);
    if(error == TJS_E_MEMBERNOTFOUND)
        return true;
    // Every other status, including failures whose dispatch wrote a value,
    // follows the returned Variant's ordinary truth conversion.
    return static_cast<bool>(value);
}

} // namespace

class DrawDeviceObjectBasePrimary_guess {
    friend class DrawDeviceObjectBase;
    friend class D3DLayerObject;
    friend class DrawDeviceManagerItem;
    friend class D3DImage;

    // FrontItems/BackItems nodes contain only the object pointer.  Their two
    // stripped comparator types read the live index from the pointed object;
    // neither tree stores a separate integer key.  The insertion paths never
    // reject an equivalent value, identifying both containers as multisets.
    struct FrontItemLess_guess {
        bool operator()(const D3DLayerObject *left,
                        const D3DLayerObject *right) const {
            return left->getFrontIndex() < right->getFrontIndex();
        }
    };

    struct BackItemLess_guess {
        bool operator()(const D3DLayerObject *left,
                        const D3DLayerObject *right) const {
            return left->getBackIndex() < right->getBackIndex();
        }
    };

    using FrontItemSet_guess =
        std::multiset<D3DLayerObject *, FrontItemLess_guess>;
    using BackItemSet_guess =
        std::multiset<D3DLayerObject *, BackItemLess_guess>;

    iTJSDispatch2 *ScriptOwner; // no AddRef, identical to the NCB owner link
    // The public clearColor setter treats this as a pointer to a scalar, not as
    // a DrawDeviceObjectBase object: after storing ClearColor it mirrors the
    // value through this pointer when non-null.  The same non-null state is
    // also the base's "nested" marker in AddChild.  The complete root-function
    // audit finds exactly those two post-constructor reads and no writer: both
    // concrete roots therefore keep it null for their ordinary full lifetime.
    tjs_uint32 *ParentClearColor_guess = nullptr;

    tjs_uint32 ClearColor; // deliberately left uninitialized by all four ctors

    // The 32-bit constructors disambiguate these from tTVPRect: each group is
    // one pointer-sized zero slot followed by width/height int32 values.  Both
    // pointers are constructor-null and dormant in the complete four-reference
    // plugin range: no later read, write or destructor cleanup exists.  The
    // adjacent values have independent lifetimes: the initial pair is likewise
    // constructor-only, while the screen pair remains live public/geometry state.
    void *InitialSizePointer_guess = nullptr;
    tjs_int InitialWidth_guess;
    tjs_int InitialHeight_guess;
    void *ScreenSizePointer_guess = nullptr;
    tjs_int ScreenWidth;
    tjs_int ScreenHeight;

    // One zeroed four-byte state group precedes the STL containers.  The first
    // three bytes have no post-constructor access.  The last is only set to 1
    // by screen-size and forceRenderTexture writers: no reference reads it or
    // clears it back to 0.  All four exact source names remain unproved.
    tjs_uint8 RootStateByte0_guess = 0;
    tjs_uint8 RootStateByte1_guess = 0;
    tjs_uint8 RootStateByte2_guess = 0;
    bool RenderTextureDirty_guess = false;

    // All four references construct these four red-black trees in this order.
    // Android's libstdc++ _Rb_tree objects occupy 0x30/0x18 bytes on LP64/
    // ILP32; iOS's libc++ __tree objects occupy 0x18/0x0C.  The source-level
    // ownership is nevertheless identical: the three sets borrow their stored
    // pointers, while the map owns every non-null D3DModuleBase value.  The
    // destructor deletes those mapped values in key order before ordinary
    // reverse member destruction releases only the four trees' nodes.
    FrontItemSet_guess FrontItems;
    BackItemSet_guess BackItems;
    std::set<D3DImage *> ManagedObjects;
    std::map<tjs_uint32, D3DModuleBase_guess *> Modules;

    bool TransitionActive = false;
    // Method and vague are intentionally uninitialized.  They are not read
    // while TransitionActive is false, and startTransition initializes the
    // fields required by the selected method before publishing active=true.
    tjs_int TransitionMethod;
    float TransitionState = 0.0f;
    tjs_int TransitionVague;

    // The default tTJSVariant constructor writes only its tvtVoid
    // discriminator.  The four reference constructors likewise leave the
    // payload bytes untouched, and the root destructor is its only confirmed
    // consumer in the complete plugin code range.
    tTJSVariant TransitionVariant_guess;
    iTVPTexture2D *TransitionRuleTexture = nullptr;

    // These pointer-sized slots are zeroed by all four constructors and have
    // no later read, write or cleanup sites in the complete plugin code range.
    void *TransitionPointer0_guess = nullptr;
    void *TransitionPointer1_guess = nullptr;

    iTVPTexture2D *FrontTarget = nullptr;
    iTVPTexture2D *BackTarget = nullptr;

    // Deliberately uninitialized by all four reference constructors. capture
    // and Show publish a target before invoking child Draw; their normal tails
    // clear it, while ReleaseTargets and the root destructor never touch it.
    iTVPTexture2D *CurrentTarget;
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
    tjs_int StretchType = 2;
    float BicubicParam = -0.5f;
    bool ForceRenderTexture = false;

protected:
    DrawDeviceObjectBasePrimary_guess(tjs_int width, tjs_int height,
                                      iTJSDispatch2 *owner,
                                      DrawDeviceObjectBase *root);
    virtual ~DrawDeviceObjectBasePrimary_guess();

    void ReleaseTargets() {
        if(FrontTarget) {
            FrontTarget->Release();
            FrontTarget = nullptr;
        }
        if(BackTarget) {
            BackTarget->Release();
            BackTarget = nullptr;
        }
    }

public:
    virtual bool capture(tTJSVariant targetLayer,
                         tjs_int frontIndexLimit) = 0;
    virtual void OnItemsChanged_guess() {}
};

class DrawDeviceObjectBase : public DrawDeviceObjectBasePrimary_guess,
                             public tTVPDrawDevice {
    friend class D3DLayerObject;
    friend class DrawDeviceManagerItem;
    friend class D3DImage;

    tjs_int PrimaryWidth;
    tjs_int PrimaryHeight;
    tjs_int ScreenLeft = 0;
    tjs_int ScreenTop = 0;
    // One real pointer-sized member separates ScreenTop from UpdateState in
    // all four ABIs. Both concrete root constructors null it, but the complete
    // plugin code ranges contain no later read, write or destructor cleanup.
    // Keep the storage and a conservative name; its historical type is lost.
    void *TailPointer_guess = nullptr;
    tjs_int UpdateState = 0;

    bool EraseFront(D3DLayerObject *object) {
        const auto range = FrontItems.equal_range(object);
        for(auto it = range.first; it != range.second; ++it) {
            if(*it == object) {
                FrontItems.erase(it);
                return true;
            }
        }
        return false;
    }

    bool EraseBack(D3DLayerObject *object) {
        const auto range = BackItems.equal_range(object);
        for(auto it = range.first; it != range.second; ++it) {
            if(*it == object) {
                BackItems.erase(it);
                return true;
            }
        }
        return false;
    }

    void EnsureTargets(iTVPRenderManager *manager) {
        if(BackTarget && BackTarget->GetWidth() >= static_cast<tjs_uint>(PrimaryWidth) &&
           BackTarget->GetHeight() >= static_cast<tjs_uint>(PrimaryHeight))
            return;

        ReleaseTargets();
        FrontTarget = manager->CreateTexture2D(
            nullptr, 0, static_cast<tjs_uint>(PrimaryWidth),
            static_cast<tjs_uint>(PrimaryHeight), TVPTextureFormat::RGBA, 0);
        BackTarget = manager->CreateTexture2D(
            nullptr, 0, static_cast<tjs_uint>(PrimaryWidth),
            static_cast<tjs_uint>(PrimaryHeight), TVPTextureFormat::RGBA, 0);
    }

    void RequestWindowUpdate() {
        if(Window)
            Window->RequestUpdate();
    }

    // The 32-bit Android and both iOS references keep this as a helper; the
    // Android arm64 optimizer inlines the same body into capture and Show.
    void UpdateObjects_guess(tjs_int updateState) {
        // One Variant is constructed for the whole tree walk and its address
        // is shared by every visible child.  Both IsVisible and OnUpdate run
        // before the red-black-tree iterator increments from the live current
        // node: future erases/inserts can change the structural successor,
        // while erasing the current node leaves increment with freed storage.
        // Callback exceptions destroy this (possibly callback-mutated) Variant
        // and escape; there is no catch or traversal continuation here.
        tTJSVariant state(updateState);
        for(D3DLayerObject *object : FrontItems) {
            if(object->IsVisible())
                object->OnUpdate(updateState, state);
        }
    }

protected:
    DrawDeviceObjectBase(tjs_int width, tjs_int height, iTJSDispatch2 *owner)
        : DrawDeviceObjectBasePrimary_guess(width, height, owner, this),
          tTVPDrawDevice(), PrimaryWidth(width), PrimaryHeight(height) {}

    iTJSDispatch2 *GetScriptOwner() const { return ScriptOwner; }

public:
    // Direct bases are destroyed in reverse declaration order: the
    // tTVPDrawDevice manager snapshot/Release phase runs while this primary
    // root subobject and its trees still exist, then the primary destructor
    // releases targets, modules and tree nodes. The manager phase never
    // deletes plugin item pointers in those trees or clears manager data.
    ~DrawDeviceObjectBase() override = default;

    tjs_int64 getInterface() {
        return reinterpret_cast<tjs_int64>(
            static_cast<iTVPDrawDevice *>(this));
    }

    tjs_uint32 getClearColor() const { return ClearColor; }
    void setClearColor(tjs_uint32 color) {
        ClearColor = color;
        if(ParentClearColor_guess)
            *ParentClearColor_guess = color;
    }

    tjs_real getTransState() const { return TransitionState; }
    void setTransState(tjs_real state) {
        // The public property keeps the script engine's double precision until
        // after clamping.  This ordering also preserves NaN instead of turning
        // it into either endpoint.
        TransitionState = static_cast<float>(
            std::min(std::max(state, 0.0), 1.0));
    }

    tTJSVariant getChildren() const {
        // The references do not dispatch Array.PropSetByNum here.  They build
        // an ncbArrayAccessor, obtain its native tTJSArrayNI once, and append
        // Object closures directly to Items.  NativeInstanceSupport's status
        // is ignored because this is a fresh core Array; the output slot is
        // deliberately not given a fallback value.
        ncbArrayAccessor array;
        tTJSArrayNI *native;
        (void)array.GetDispatch()->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
            reinterpret_cast<iTJSNativeInstance **>(&native));
        for(D3DLayerObject *object : FrontItems) {
            // IsValid checks the current owner, but emplace_back receives two
            // lvalue references to the live field.  A re-entrant IsValid that
            // changes ScriptOwner therefore makes the new value (which is not
            // revalidated) the Object and ObjThis stored in the Array.
            if(!object->ScriptOwner ||
               object->ScriptOwner->IsValid(
                   0, nullptr, nullptr, object->ScriptOwner) != TJS_S_TRUE)
                continue;
            native->Items.emplace_back(
                object->ScriptOwner, object->ScriptOwner);
        }
        iTJSDispatch2 *dispatch = array.GetDispatch();
        return tTJSVariant(dispatch, dispatch);
    }

    void AddChild(D3DLayerObject *object) {
        if(object) {
            object->OnDetached();
            if(ParentClearColor_guess)
                object->OnParentHasParent();
            FrontItems.insert(object);
            BackItems.insert(object);
        }
        // The call is unconditional, including add(non-object).  The concrete
        // reference root currently inherits the primary base's no-op body.
        OnItemsChanged_guess();
    }

    void add(tTJSVariant child) {
        // This borrowed-ID lookup can intentionally disagree with concrete
        // D3DLayer property dispatch after the factory descriptor is re-entered.
        if(child.Type() == tvtObject)
            AddChild(GetD3DLayerObject(child.AsObjectNoAddRef()));
        else
            AddChild(nullptr);
    }

    void remove(tTJSVariant child) {
        // Duplicate borrowed IDs are not replaced, so repeated removal after
        // constructor re-entry continues targeting the oldest generation.
        if(child.Type() != tvtObject)
            return;
        D3DLayerObject *object =
            GetD3DLayerObject(child.AsObjectNoAddRef());
        if(!object)
            return;
        const bool removedFront = EraseFront(object);
        const bool removedBack = EraseBack(object);
        if(removedFront || removedBack) {
            object->OnDetached();
            OnItemsChanged_guess();
        }
    }

    void startTransition(tTJSVariant options) {
        iTJSDispatch2 *object = options.AsObjectNoAddRef();
        tTJSVariant method;
        const bool hasMethod =
            TJS_SUCCEEDED(object->PropGet(
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
                    TransitionRuleTexture =
                        layer->GetMainImage()->GetTexture();
                    TransitionRuleTexture->AddRef();
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

    float getOffsetX() const { return OffsetX; }
    void setOffsetX(float value) { OffsetX = value; }
    float getOffsetY() const { return OffsetY; }
    void setOffsetY(float value) { OffsetY = value; }
    void setOffset(float x, float y) {
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
        RenderTextureDirty_guess = true;
    }

    void setPrimarySize(tjs_int width, tjs_int height) {
        PrimaryWidth = width;
        PrimaryHeight = height;
        if(Window)
            Window->NotifySrcResize();
    }
    tjs_int getPrimaryWidth() const { return PrimaryWidth; }
    void setPrimaryWidth(tjs_int value) { PrimaryWidth = value; }
    tjs_int getPrimaryHeight() const { return PrimaryHeight; }
    void setPrimaryHeight(tjs_int value) { PrimaryHeight = value; }

    void setScreenRect(tjs_int left, tjs_int top, tjs_int width,
                       tjs_int height) {
        ScreenLeft = left;
        ScreenTop = top;
        if(ScreenWidth != width || ScreenHeight != height) {
            ScreenWidth = width;
            ScreenHeight = height;
            ReleaseTargets();
            RenderTextureDirty_guess = true;
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
            RenderTextureDirty_guess = true;
        }
    }
    tjs_int getScreenHeight() const { return ScreenHeight; }
    void setScreenHeight(tjs_int value) {
        if(ScreenHeight != value) {
            ScreenHeight = value;
            ReleaseTargets();
            RenderTextureDirty_guess = true;
        }
    }

    tTJSVariant getPrimaryLayers() const {
        // Like getChildren, the references append straight to a fresh Array's
        // native Items deque and ignore NativeInstanceSupport's ordinary
        // status.  Managers.begin/end are snapshotted by the range-for.
        ncbArrayAccessor array;
        tTJSArrayNI *native;
        (void)array.GetDispatch()->NativeInstanceSupport(
            TJS_NIS_GETINSTANCE, TJSGetArrayClassID(),
            reinterpret_cast<iTJSNativeInstance **>(&native));
        for(auto *manager : Managers) {
            tTJSNI_BaseLayer *layer = manager->GetPrimaryLayer();
            // The old reference GetOwner() helper loads this raw field and
            // AddRefs it once.  That owned getter reference is never released:
            // Items.emplace_back adds two more closure refs, while Array
            // destruction returns only those two.  Preserve the per-manager,
            // per-call +1 leak.  A null layer still crashes naturally; a layer
            // with no owner contributes a tagged null Object closure.
            iTJSDispatch2 *owner = layer->GetOwnerNoAddRef();
            if(owner)
                owner->AddRef();
            native->Items.emplace_back(owner, owner);
        }
        iTJSDispatch2 *dispatch = array.GetDispatch();
        return tTJSVariant(dispatch, dispatch);
    }

    tjs_int getLayerManagerIndex() const {
        return static_cast<tjs_int>(PrimaryLayerManagerIndex);
    }
    void setLayerManagerIndex(tjs_int index) {
        if(index < 0 || Managers.size() <=
                            static_cast<size_t>(index))
            TVPThrowExceptionMessage(TJS_W("invalid layer manager index."));
        PrimaryLayerManagerIndex = static_cast<size_t>(index);
    }

    tjs_int getDestLeft() const { return DestRect.left; }
    tjs_int getDestTop() const { return DestRect.top; }
    tjs_int getDestWidth() const { return DestRect.get_width(); }
    tjs_int getDestHeight() const { return DestRect.get_height(); }

    bool checkEnable(ttstr) const { return false; }
    tTJSVariant getModule(ttstr) const {
        return tTJSVariant(static_cast<tjs_int>(0));
    }

    bool capture(tTJSVariant targetLayer,
                 tjs_int frontIndexLimit) override {
        // capture updates children with a literal zero but does not consume the
        // root UpdateState field.  A callback that calls update(newState)
        // therefore leaves that state pending for the next Show.
        UpdateObjects_guess(0);

        auto *manager = GetD3DRenderManager();
        CurrentTarget = manager->CreateTexture2D(
            nullptr, 0, static_cast<tjs_uint>(PrimaryWidth),
            static_cast<tjs_uint>(PrimaryHeight), TVPTextureFormat::RGBA, 0);
        const D3DPoint_guess offset{OffsetX, OffsetY};
        for(D3DLayerObject *object : FrontItems) {
            if(object->IsVisible() &&
               (!frontIndexLimit || object->getFrontIndex() < frontIndexLimit) &&
               (object->getDrawPlane() & 1) != 0)
                object->Draw(offset);
        }

        tTJSNI_Layer *layer = tTJSNI_Layer::FromVariant(targetLayer);
        if(TVPIsSoftwareRenderManager()) {
            // Every stage deliberately re-reads the live CurrentTarget field.
            // A child callback or target conversion can replace it after the
            // originally allocated texture was published.
            const void *source = CurrentTarget->GetPixelData();
            const tjs_uint sourcePitch =
                static_cast<tjs_uint>(CurrentTarget->GetPitch());
            iTVPTexture2D *sizeTarget = CurrentTarget;
            const tjs_uint width = sizeTarget->GetWidth();
            const tjs_uint height = sizeTarget->GetHeight();
            layer->SetSize(width, height);
            layer->GetMainImage()->Update(
                source, sourcePitch, 0, 0,
                static_cast<int>(width), static_cast<int>(height));
        } else {
            layer->AssignTexture(CurrentTarget);
        }
        CurrentTarget->Release();
        CurrentTarget = nullptr;
        return true;
    }

    void getPrimaryLayerBitmap(tjs_int index, tTJSVariant targetLayer);
    void update(tjs_int state) {
        UpdateState = state;
        OnItemsChanged_guess();
        RequestWindowUpdate();
    }

    void GetCursorPos(iTVPLayerManager *manager, tjs_int &x,
                      tjs_int &y) override;
    void SetCursorPos(iTVPLayerManager *manager, tjs_int x,
                      tjs_int y) override;
    void AddLayerManager(iTVPLayerManager *manager) override;
    void RemoveLayerManager(iTVPLayerManager *manager) override;
    void Show() override;
    void StartBitmapCompletion(iTVPLayerManager *manager) override;
    void NotifyBitmapCompleted(iTVPLayerManager *, tjs_int, tjs_int,
                               tTVPBaseTexture *, const tTVPRect &,
                               tTVPLayerType, tjs_int) override {}
    void EndBitmapCompletion(iTVPLayerManager *) override {}
};

void D3DLayerBaseNativeInstance::DeleteInstance() {
    if(Instance && !Sticky)
        delete Instance;
    Instance = nullptr;
    Sticky = false;
}

D3DLayerBaseNativeInstance::~D3DLayerBaseNativeInstance() {
    DeleteInstance();
}

void D3DLayerBaseNativeInstance::Invalidate() {
    DeleteInstance();
}

DrawDeviceObjectBasePrimary_guess::DrawDeviceObjectBasePrimary_guess(
    tjs_int width, tjs_int height, iTJSDispatch2 *owner,
    DrawDeviceObjectBase *root)
    : ScriptOwner(owner), InitialWidth_guess(width),
      InitialHeight_guess(height), ScreenWidth(width),
      ScreenHeight(height) {
    // This independent native-instance slot is installed while the primary
    // base is under construction, before the tTVPDrawDevice secondary base.
    RegisterD3DLayerBaseNative(owner, root);
    GetD3DLayerBaseAdaptor(owner)->SetSticky();
}

D3DLayerObject::D3DLayerObject(iTJSDispatch2 *owner)
    : ScriptOwner(owner) {
    // Every construction appends a new borrowed view.  There is no duplicate
    // check and no attempt to detach an older view from the same script shell.
    if(owner)
        RegisterD3DLayerObjectNative(owner, this);
}

D3DLayerObject::~D3DLayerObject() {
    // The shipped destructor EH differs by target.  Android arm64 and iOS
    // armv7 clean the listener-list base before terminating if detach escapes;
    // Android armv7 and iOS arm64 emit no local cleanup landing.  This ordinary
    // C++ destructor preserves the shared source shape rather than encoding a
    // compiler artifact as a platform branch.
    if(Parent) {
        const bool removedFront = Parent->EraseFront(this);
        const bool removedBack = Parent->EraseBack(this);
        if(removedFront || removedBack) {
            OnDetached();
            Parent->OnItemsChanged_guess();
        }
    }
}

iTVPTexture2D *D3DLayerObject::GetParentDrawTarget() const {
    return Parent ? Parent->CurrentTarget : nullptr;
}

void D3DLayerObject::SetParent_guess(DrawDeviceObjectBase *parent) {
    if(Parent) {
        const bool removedFront = Parent->EraseFront(this);
        const bool removedBack = Parent->EraseBack(this);
        if(removedFront || removedBack) {
            OnDetached();
            Parent->OnItemsChanged_guess();
        }
    }
    Parent = parent;
    if(Parent)
        Parent->AddChild(this);
}

D3DLayerListener::D3DLayerListener(D3DLayer *owner)
    : _d3dLayerOwner(owner) {
    // Registration is still part of base construction.  In all four
    // references push_back allocates and initializes its detached node before
    // touching the owner list; an allocation exception therefore leaves no
    // half-registered listener for constructor unwinding to remove.
    if(_d3dLayerOwner)
        _d3dLayerOwner->AddListener(this);
}

D3DLayerListener::~D3DLayerListener() {
    if(_d3dLayerOwner)
        _d3dLayerOwner->RemoveListener(this);
}

D3DModuleBase_guess *
D3DLayerObject::FindParentModule_guess(tjs_uint32 classId) const {
    const auto it = Parent->Modules.find(classId);
    return it == Parent->Modules.end() ? nullptr : it->second;
}

void D3DLayerObject::SetParentModule_guess(tjs_uint32 classId,
                                           D3DModuleBase_guess *module) {
    Parent->Modules[classId] = module;
}

// All four references append a list node and deliberately permit duplicates.
// Node allocation is the only throwing operation and precedes link
// publication (and the libc++ cached-size increment), so allocation failure
// leaves the list byte-for-byte in its pre-call state.
void D3DLayerObject::AddListener(D3DLayerListener *listener) {
    if(listener)
        Listeners.push_back(listener);
}

// Removal erases every matching listener node, matching std::list::remove.
// The iOS libc++ build splices matching runs into a temporary list and frees
// them at scope exit; Android libstdc++ unhooks/deletes nodes while scanning.
void D3DLayerObject::RemoveListener(D3DLayerListener *listener) {
    if(listener)
        Listeners.remove(listener);
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
    Parent->FrontItems.insert(this);
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
    Parent->BackItems.insert(this);
}

bool D3DLayerObject::OnUpdate(tjs_int, const tTJSVariant &state) {
    if(ScriptOwner) {
        // All four references put the incoming Variant's address directly in
        // the one-element FuncCall array.  There is no temporary Variant,
        // AddRef/Release pair or local destructor/EH cleanup.  FuncCall's
        // mutable-pointer ABI requires casting away this method's const view;
        // its tjs_error result is deliberately ignored.
        tTJSVariant *parameters[] = {
            const_cast<tTJSVariant *>(&state)
        };
        ScriptOwner->FuncCall(0, TJS_W("onUpdate"), nullptr, nullptr, 1,
                              parameters, ScriptOwner);
    }

    bool result = false;
    // Cursor advancement reads the live current-node link only after the
    // callback returns.  Removing a future node is therefore observed, and an
    // appended node is visited in this pass; removing the current node leaves
    // the subsequent increment reading freed storage.  A thrown callback
    // escapes immediately with no catch, snapshot or deferred continuation.
    for(D3DLayerListener *listener : Listeners) {
        const bool listenerResult = listener->IsVisible();
        result = result || listenerResult;
    }
    return result;
}

class DrawDeviceManagerItem : public D3DLayerObject {
    friend class DrawDeviceObjectBase;

    iTVPLayerManager *Manager;
    tTJSNI_BaseLayer *PrimaryLayer;
    iTJSDispatch2 *PrimaryOwner;

    // This dormant tail retains the texture-lock state from the historical
    // DrawDeviceD3D LayerManagerInfo lineage.  All four current references
    // still construct it as null/zero/true, but contain no later read, write or
    // cleanup of these fields: bitmap completion now uses render textures.
    void *textureBuffer = nullptr;
    tjs_int texturePitch = 0;
    bool lastOK = true;

protected:
    // This extra virtual slot follows TransformPoint.  The software subclass
    // overrides it with a cached private-OpenGL texture conversion.
    virtual iTVPTexture2D *GetDrawTexture_guess(iTVPBaseBitmap *bitmap) {
        return bitmap->GetTexture();
    }

public:
    DrawDeviceManagerItem(DrawDeviceObjectBase *owner,
                          iTVPLayerManager *manager)
        : D3DLayerObject(nullptr), Manager(manager),
          PrimaryLayer(manager->GetPrimaryLayer()),
          PrimaryOwner(PrimaryLayer->GetOwnerNoAddRef()) {
        if(PrimaryOwner)
            PrimaryOwner->AddRef();
        UpdateSettings();

        // The reference constructors require both the primary layer and its
        // main image, then initialize that image to ARGB 0 without null guards.
        auto *image = PrimaryLayer->GetMainImage();
        image->Fill(tTVPRect(0, 0, static_cast<tjs_int>(image->GetWidth()),
                             static_cast<tjs_int>(image->GetHeight())),
                    0);
        SetParent_guess(owner);
    }

    // Construction AddRefs PrimaryOwner, but the reference destructor is the
    // unmodified D3DLayerObject destructor and deliberately does not Release.
    ~DrawDeviceManagerItem() override = default;

    void UpdateSettings() {
        // ncbPropAccessor intentionally AddRefs without a null guard. Each
        // getIntValue first probes with MEMBERMUSTEXIST, then performs a
        // second ordinary PropGet and converts that second Variant.
        ncbPropAccessor properties(PrimaryOwner);
        setDrawPlane(
            properties.getIntValue(TJS_W("drawPlane"), 0) & 3);
        setFrontIndex(
            properties.getIntValue(TJS_W("frontIndex"), 0));
        setBackIndex(
            properties.getIntValue(TJS_W("backIndex"), 0));
    }

    bool IsVisible() override { return GetVisibleProperty(PrimaryOwner); }
    bool TransformPoint(float &, float &) const override { return false; }

    void Draw(const D3DPoint_guess &) override {
        DrawDeviceObjectBase *owner = GetParent();
        if(!owner)
            return;

        iTVPBaseBitmap *drawBuffer = Manager->GetDrawBuffer();
        if(!drawBuffer)
            return;

        ncbPropAccessor properties(PrimaryOwner);
        const tjs_int type =
            properties.getIntValue(TJS_W("type"), 0);
        const tjs_int opacity =
            properties.getIntValue(TJS_W("drawOpacity"), 255);
        const tjs_int x =
            properties.getIntValue(TJS_W("offsetX"), 0);
        const tjs_int y =
            properties.getIntValue(TJS_W("offsetY"), 0);

        auto *renderManager = GetD3DRenderManager();
        iTVPRenderMethod *method =
            renderManager->GetRenderMethod(opacity, true, type);
        if(!method)
            return;

        // The source rectangle snapshots the draw buffer dimensions before
        // the virtual texture conversion.  The conversion may re-enter or,
        // for the software item, expose a differently sized cached texture.
        // Neither rectangle samples the returned texture's dimensions.
        const tjs_uint sourceWidth = drawBuffer->GetWidth();
        const tjs_uint sourceHeight = drawBuffer->GetHeight();
        iTVPTexture2D *source = GetDrawTexture_guess(drawBuffer);

        const tTVPRect sourceRect(
            0, 0, static_cast<tjs_int>(sourceWidth),
            static_cast<tjs_int>(sourceHeight));

        // The target rectangle takes a second draw-buffer sample after the
        // conversion.  right/bottom receive those raw width/height values;
        // the reference does not add offsetX/offsetY to them.
        const tTVPRect targetRect(
            x, y, static_cast<tjs_int>(drawBuffer->GetWidth()),
            static_cast<tjs_int>(drawBuffer->GetHeight()));
        std::pair<iTVPTexture2D *, tTVPRect> textures[] = {
            {source, sourceRect}};
        renderManager->OperateRect(
            method, owner->CurrentTarget, owner->CurrentTarget, targetRect,
            tRenderTexRectArray(textures));
    }
};

void DrawDeviceObjectBase::getPrimaryLayerBitmap(
    tjs_int index, tTJSVariant targetLayer) {
    if(index < 0 || Managers.size() <= static_cast<size_t>(index))
        TVPThrowExceptionMessage(TJS_W("invalid layer manager index."));

    // GetDrawDeviceData, not manager.GetPrimaryLayer, supplies the sole null
    // gate.  Its non-null result is snapshotted before target conversion; a
    // re-entrant NativeInstanceSupport call may replace the manager's live data
    // slot, but this call still dereferences the previously returned item.
    auto *item = static_cast<DrawDeviceManagerItem *>(
        Managers[static_cast<size_t>(index)]->GetDrawDeviceData());
    if(item) {
        tTJSNI_Layer *target = tTJSNI_Layer::FromVariant(targetLayer);
        tTJSNI_BaseLayer *source = item->PrimaryLayer;

        // source, main image and texture are strict raw-pointer links: there is
        // no null fallback, AddRef or rollback guard before AssignTexture.
        target->AssignTexture(source->GetMainImage()->GetTexture());
    }
}

class SoftwareDrawDeviceManagerItem_guess final
    : public DrawDeviceManagerItem {
    iTVPTexture2D *SoftwareTexture = nullptr;

protected:
    iTVPTexture2D *GetDrawTexture_guess(iTVPBaseBitmap *bitmap) override {
        // Render-manager acquisition is the first operation even when the
        // existing software cache will be updated and returned.
        iTVPRenderManager *renderManager = GetD3DRenderManager();
        iTVPTexture2D *source = bitmap->GetTexture();
        const void *pixels = source->GetPixelData();
        const tjs_int pitch = source->GetPitch();
        const tjs_uint width = source->GetWidth();
        const tjs_uint height = source->GetHeight();

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
        // Deliberately do not clear SoftwareTexture before creation.  A
        // CreateTexture2D exception leaves the just-released pointer in the
        // member, matching the reference's non-transactional cache boundary.
        SoftwareTexture = renderManager->CreateTexture2D(
            pixels, pitch, width, height, TVPTextureFormat::RGBA, 0);
        return SoftwareTexture;
    }

public:
    SoftwareDrawDeviceManagerItem_guess(DrawDeviceObjectBase *owner,
                                        iTVPLayerManager *manager)
        : DrawDeviceManagerItem(owner, manager) {}

    ~SoftwareDrawDeviceManagerItem_guess() override {
        // Native A64/iOS-armv7 cleanup the D3DLayerObject base and terminate if
        // this Release escapes; native A32/iOS-arm64 have no local landing.
        // Normal completion on every target then reaches the base destructor
        // and the deleting destructor's raw operator delete.
        if(SoftwareTexture)
            SoftwareTexture->Release();
    }
};

DrawDeviceObjectBasePrimary_guess::~DrawDeviceObjectBasePrimary_guess() {
    // The reference order is significant: targets, transition-rule texture,
    // then every owned Modules value in key order.  TransitionVariant_guess
    // and the four tree objects are destroyed automatically afterward, in
    // reverse declaration order (Modules, ManagedObjects, BackItems,
    // FrontItems).  Tree destruction never visits/deletes the borrowed image
    // or layer-object pointers stored in the three sets.
    ReleaseTargets();
    if(TransitionRuleTexture)
        TransitionRuleTexture->Release();

    for(const auto &entry : Modules)
        delete entry.second;
}

void DrawDeviceObjectBase::Show() {
    // Snapshot UpdateState into the helper argument.  The clear is a
    // normal-success commit after every child callback and the shared Variant
    // destructor: a re-entrant update(newState) is overwritten by zero on
    // success, but remains pending if IsVisible/OnUpdate/Variant cleanup throws.
    UpdateObjects_guess(UpdateState);
    UpdateState = 0;

    if(!Window)
        return;

    for(auto *manager : Managers) {
        auto *item = static_cast<DrawDeviceManagerItem *>(
            manager->GetDrawDeviceData());
        if(item)
            item->UpdateSettings();
    }

    iTVPRenderManager *renderManager = GetD3DRenderManager();
    EnsureTargets(renderManager);

    // All four references initialize these guarded statics on the first Show
    // that passes the Window gate, even when no transition is active.  The
    // color is then published once per active Show and the FrontTarget-sized
    // rectangle is reused for both fill operations.
    static iTVPRenderMethod *fillMethod =
        renderManager->GetRenderMethod("FillARGB");
    static const int fillColorId = fillMethod->EnumParameterID("color");
    if(TransitionActive) {
        fillMethod->SetParameterColor4B(fillColorId, ClearColor);
        const tTVPRect fillRect(
            0, 0, static_cast<tjs_int>(FrontTarget->GetWidth()),
            static_cast<tjs_int>(FrontTarget->GetHeight()));
        renderManager->OperateRect(fillMethod, FrontTarget, FrontTarget,
                                   fillRect, tRenderTexRectArray());
        renderManager->OperateRect(fillMethod, BackTarget, BackTarget,
                                   fillRect, tRenderTexRectArray());

        CurrentTarget = FrontTarget;
        const D3DPoint_guess frontOffset{OffsetX, OffsetY};
        for(D3DLayerObject *item : FrontItems) {
            if(item->IsVisible() && (item->getDrawPlane() & 1) != 0)
                item->Draw(frontOffset);
        }

        CurrentTarget = BackTarget;
        const D3DPoint_guess backOffset{OffsetX, OffsetY};
        for(D3DLayerObject *item : BackItems) {
            if(item->IsVisible() && (item->getDrawPlane() & 2) != 0)
                item->Draw(backOffset);
        }

        // All four references consume only TransitionActive and
        // TransitionState here. The parsed universal method, vague value and
        // retained rule texture are not consulted by the rendering path.
        if(TransitionActive) {
            static iTVPRenderMethod *method =
                renderManager->GetRenderMethod("AlphaBlend_SD");
            static const int opacityId =
                method->EnumParameterID("opacity");
            method->SetParameterFloat(opacityId, TransitionState);
            const tTVPRect targetRect(
                0, 0, static_cast<tjs_int>(FrontTarget->GetWidth()),
                static_cast<tjs_int>(FrontTarget->GetHeight()));
            std::pair<iTVPTexture2D *, tTVPRect> textures[] = {
                {FrontTarget, targetRect}};
            renderManager->OperateRect(
                method, BackTarget, BackTarget, targetRect,
                tRenderTexRectArray(textures));
        }
    } else {
        CurrentTarget = BackTarget;
        const D3DPoint_guess offset{OffsetX, OffsetY};
        for(D3DLayerObject *item : FrontItems) {
            if(item->IsVisible() && (item->getDrawPlane() & 1) != 0)
                item->Draw(offset);
        }
    }

    CurrentTarget = nullptr;
    if(iWindowLayer *form = Window->GetForm())
        form->UpdateDrawBuffer(BackTarget);
}

void DrawDeviceObjectBase::GetCursorPos(iTVPLayerManager *, tjs_int &x,
                                        tjs_int &y) {
    Window->GetCursorPos(x, y);
    if(!TransformToPrimaryLayerManager(x, y)) {
        x = 0;
        y = 0;
    }
}

void DrawDeviceObjectBase::SetCursorPos(iTVPLayerManager *, tjs_int x,
                                        tjs_int y) {
    if(TransformFromPrimaryLayerManager(x, y))
        Window->SetCursorPos(x, y);
}

void DrawDeviceObjectBase::AddLayerManager(iTVPLayerManager *manager) {
    tTVPDrawDevice::AddLayerManager(manager);
    // The plugin needs the alpha plane from the layer-manager draw buffer.
    // The concrete call also updates an already-created tTVPDestTexture.
    static_cast<tTVPLayerManager *>(manager)->SetHoldAlpha(false);

    // The source is one ordinary new-expression, but the shipped compiler EH
    // boundaries differ.  Android arm64 and iOS armv7 destroy the constructed
    // D3DLayerObject base and raw-delete the allocation when the item
    // constructor escapes.  Android armv7 and iOS arm64 emit neither cleanup.
    // None of the four cleans a fully constructed item if the final manager
    // data-slot virtual call escapes; by then the item can already be linked
    // into the root's front/back trees.
    DrawDeviceManagerItem *item;
    if(TVPIsSoftwareRenderManager())
        item = new SoftwareDrawDeviceManagerItem_guess(this, manager);
    else
        item = new DrawDeviceManagerItem(this, manager);
    manager->SetDrawDeviceData(item);
}

void DrawDeviceObjectBase::RemoveLayerManager(iTVPLayerManager *manager) {
    // The data slot is the sole item authority and is cleared before invoking
    // the item's deleting destructor.  None of the four native Remove frames
    // has a rollback landing: a SetDrawDeviceData/delete failure leaves the
    // earlier clear (or partial virtual-call side effect) committed and skips
    // the base-vector removal.
    auto *item = static_cast<DrawDeviceManagerItem *>(
        manager->GetDrawDeviceData());
    if(item) {
        manager->SetDrawDeviceData(nullptr);
        delete item;
    }
    // The base implementation finds the first pointer-equal element, calls
    // Release while that element and end are still published, then erases via
    // the saved iterator.  Release re-entry can therefore invalidate the
    // iterator; a missing element throws only after the item work above.
    tTVPDrawDevice::RemoveLayerManager(manager);
}

void DrawDeviceObjectBase::StartBitmapCompletion(
    iTVPLayerManager *manager) {
    iTVPBaseBitmap *bitmap = manager->GetDrawBuffer();
    if(!bitmap)
        return;

    iTVPRenderManager *renderManager = bitmap->GetRenderManager();
    static iTVPRenderMethod *method =
        renderManager->GetRenderMethod("FillARGB");
    static const int colorId = method->EnumParameterID("color");
    method->SetParameterColor4B(colorId, 0);

    if(TVPIsSoftwareRenderManager()) {
        auto &updateRegion =
            static_cast<tTVPLayerManager *>(manager)
                ->GetUpdateRegionForCompletion();
        auto iterator = updateRegion.GetIterator();
        iTVPTexture2D *referenceTarget = bitmap->GetTexture();
        iTVPTexture2D *target =
            bitmap->GetTextureForRender(false, nullptr);
        while(iterator.Step()) {
            const tTVPRect rect = *iterator;
            if(target->GetWidth() < static_cast<tjs_uint>(rect.right) ||
               target->GetHeight() < static_cast<tjs_uint>(rect.bottom))
                break;
            renderManager->OperateRect(
                method, target, referenceTarget, rect,
                tRenderTexRectArray());
        }
    } else {
        const tTVPRect rect(
            0, 0, static_cast<tjs_int>(bitmap->GetWidth()),
            static_cast<tjs_int>(bitmap->GetHeight()));
        iTVPTexture2D *target =
            bitmap->GetTextureForRender(false, nullptr);
        iTVPTexture2D *referenceTarget = bitmap->GetTexture();
        renderManager->OperateRect(
            method, target, referenceTarget, rect,
            tRenderTexRectArray());
    }
}

class DrawDeviceD3D final : public DrawDeviceObjectBase {
public:
    DrawDeviceD3D(tjs_int width, tjs_int height, iTJSDispatch2 *owner)
        : DrawDeviceObjectBase(width, height, owner) {}

    static tjs_error factory(DrawDeviceD3D **result, tjs_int numparams,
                             tTJSVariant **params, iTJSDispatch2 *objthis) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        // The raw native-class descriptor handles its exact one-Void empty-
        // adaptor sentinel before entering this callback.  Here the reference
        // accepts surplus arguments, converts only width/height in order, and
        // constructs before the descriptor asks objthis for its adaptor.
        // If that lookup fails, the descriptor deletes this object and returns
        // TJS_E_NATIVECLASSCRASH.  Its ClassInfo tuple, guard, class ID,
        // descriptor vtable and 0x18/0x0C concrete adaptor are all independent
        // from D3D; a successful ordinary adaptor is non-sticky and owns this
        // root, while the separately registered D3DLayerBase view is borrowed.
        //
        // The four shipped exception boundaries match D3D only after direct
        // verification: Android arm64 performs phased raw/base cleanup;
        // Android armv7 and iOS arm64 have no factory cleanup and leak an
        // escaping allocation; iOS armv7 raw-deletes conversion failures but
        // routes a constructor escape to its SJLJ terminate/trap case.
        *result = new DrawDeviceD3D(static_cast<tjs_int>(*params[0]),
                                    static_cast<tjs_int>(*params[1]), objthis);
        return TJS_S_OK;
    }
};

class D3D final : public DrawDeviceObjectBase {
public:
    D3D(tjs_int width, tjs_int height, iTJSDispatch2 *owner)
        : DrawDeviceObjectBase(width, height, owner) {}

    static tjs_error factory(D3D **result, tjs_int numparams,
                             tTJSVariant **params, iTJSDispatch2 *objthis) {
        if(numparams < 2)
            return TJS_E_BADPARAMCOUNT;
        // This uses the same raw-factory state machine as DrawDeviceD3D but a
        // distinct ClassInfo tuple, initialization guard, class ID, descriptor
        // vtable and concrete adaptor.  The 0x18/0x0C concrete adaptor owns the
        // object when non-sticky; only the separately registered D3DLayerBase
        // lookup adaptor is sticky and borrowed.
        //
        // Do not infer uniform exception cleanup from this source expression.
        // In the four shipped D3D artifacts the toolchain output differs after
        // allocation: Android arm64 has phased raw/base cleanup; Android armv7
        // and iOS arm64 have no factory landing pad and leak an allocation if
        // conversion/construction escapes; iOS armv7 raw-deletes for either
        // conversion but sends a constructor escape to its SJLJ terminate/trap
        // case.  All four still publish *result only after complete success.
        *result = new D3D(static_cast<tjs_int>(*params[0]),
                          static_cast<tjs_int>(*params[1]), objthis);
        return TJS_S_OK;
    }
};

void D3DLayer::NotifyMatrixChanged() {
    // The reference walks the live list directly.  Duplicate nodes fan out,
    // and a callback that destroys/removes the current listener can invalidate
    // the iterator; future erases and tail appends are visible in this pass.
    // There is no snapshot, re-entrancy fence, deferred erase, exception catch
    // or matrix rollback after a callback throws.
    for(D3DLayerListener *listener : Listeners)
        (void)listener->IsVisible();
}

D3DLayer::D3DLayer(iTJSDispatch2 *owner, DrawDeviceObjectBase *parent)
    : D3DLayerObject(owner) {
    SetParent_guess(parent);
}

// Deliberately does not detach D3DLayerObject's borrowed native-instance view.
// The concrete non-sticky NCBind adaptor owns this object, while the separate
// two-field view may remain registered with a dangling borrowed pointer.
D3DLayer::~D3DLayer() = default;

tjs_error D3DLayer::factory(D3DLayer **result, tjs_int numparams,
                            tTJSVariant **params, iTJSDispatch2 *objthis) {
    if(numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    if((*params[0]).Type() != tvtObject)
        return TJS_E_INVALIDTYPE;
    DrawDeviceObjectBase *parent =
        GetD3DLayerBase((*params[0]).AsObjectNoAddRef());
    if(!parent)
        return TJS_E_INVALIDTYPE;
    // Arg0 is unboxed through the distinct D3DLayerBase/root class ID; this is
    // not either the concrete D3DLayer ClassInfo ID or D3DLayerObject's borrowed
    // lookup ID.  Surplus arguments are ignored.  The outer descriptor then
    // raw-attaches the result to objthis's concrete non-sticky adaptor; lookup
    // failure deletes this fresh layer and returns TJS_E_NATIVECLASSCRASH.
    // Re-entering the descriptor on an already-populated receiver overwrites
    // the concrete native pointer without deleting the old layer, while this
    // constructor appends another borrowed D3DLayerObject view.  Concrete
    // dispatch therefore sees the newest layer, but D3D.add/remove see the
    // oldest duplicate ID.  A normal shell has slot 0=concrete and slot
    // 1=first borrowed view; two re-entries fill slots 2 and 3, and later
    // re-entries leak the newly allocated borrowed adaptor after REGISTER
    // fails.  Borrowed views also remain registered after concrete layer dtors.
    *result = new D3DLayer(objthis, parent);
    return TJS_S_OK;
}

void D3DLayer::Draw(const D3DPoint_guess &) {
    if(!GetParent() || !Visible)
        return;
    // Parent/Visible and CurrentTarget are sampled once before fan-out.  The
    // callbacks then use the same post-callback live-node advancement as the
    // update/matrix loops, and a thrown Draw aborts the remaining listeners.
    iTVPTexture2D *target = GetParentDrawTarget();
    for(D3DLayerListener *listener : Listeners)
        listener->Draw(target);
}

bool D3DLayer::TransformPoint(float &x, float &y) const {
    x = Matrix.m[12] + static_cast<float>(GetParent()->getScreenWidth() / 2) +
        x * Matrix.m[0];
    y = Matrix.m[13] + static_cast<float>(GetParent()->getScreenHeight() / 2) +
        y * Matrix.m[5];
    return true;
}

void D3DLayer::setMatrix(float m0, float m1, float m2, float m3,
                         float m4, float m5, float m6, float m7,
                         float m8, float m9, float m10, float m11,
                         float m12, float m13, float m14, float m15) {
    Matrix.set(m0, m4, m8,  m12, m1, m5, m9,  m13,
               m2, m6, m10, m14, m3, m7, m11, m15);
    NotifyMatrixChanged();
}

void D3DLayer::setMatrixGL(float m0, float m1, float m2, float m3,
                           float m4, float m5, float m6, float m7,
                           float m8, float m9, float m10, float m11,
                           float m12, float m13, float m14, float m15) {
    Matrix.set(m0, m1, m2, m3, m4,  m5,  m6,  m7,
               m8, m9, m10,m11,m12, m13, m14, m15);
    NotifyMatrixChanged();
}

void D3DLayer::setClip(float left, float top, float right, float bottom) {
    Clip[0] = left;
    Clip[1] = top;
    Clip[2] = right;
    Clip[3] = bottom;
}

void D3DLayer::CopyMatrixForListener(float (&result)[16]) const {
    std::copy(Matrix.m, Matrix.m + 16, result);
}

float TVPGetD3DLayerScaleX(const D3DLayer *object) {
    return object ? object->GetScaleXForListener() : 0.0f;
}

class D3DImage final {
    DrawDeviceObjectBase *Owner;
    TJS::tTJSRefHolder<iTVPTexture2D> *Picture = nullptr;

public:
    explicit D3DImage(DrawDeviceObjectBase *owner) : Owner(owner) {
        // The script factory proves owner is non-null before construction. The
        // set owns only its pointer node; it neither owns this image nor clears
        // Owner during root destruction, so the image must die first.
        Owner->ManagedObjects.insert(this);
    }

    virtual ~D3DImage() {
        // Holder release precedes set erasure.  D3DPicture borrowers are not
        // detached or visited, and Owner is only null-guarded, never retained.
        ClearTextureHolder_guess();
        if(Owner)
            Owner->ManagedObjects.erase(this);
    }

    // This is a real third virtual slot after the two Itanium destructor
    // entries. The references call it from the complete destructor.
    virtual void ClearTextureHolder_guess() {
        delete Picture;
        Picture = nullptr;
    }

    static tjs_error factory(D3DImage **result, tjs_int numparams,
                             tTJSVariant **params, iTJSDispatch2 *) {
        if(numparams < 1)
            return TJS_E_BADPARAMCOUNT;
        if((*params[0]).Type() != tvtObject)
            return TJS_E_INVALIDTYPE;
        DrawDeviceObjectBase *owner =
            GetD3DLayerBase((*params[0]).AsObjectNoAddRef());
        if(!owner)
            return TJS_E_INVALIDTYPE;
        // Only arg0 is consumed; surplus arguments are ignored.  The outer NCB
        // descriptor raw-attaches this image to the concrete non-sticky adaptor.
        // If that lookup fails, deleting the fresh image also removes its just-
        // inserted ManagedObjects node.  Re-entry on a populated receiver only
        // overwrites the adaptor pointer, leaking the old image and leaving its
        // non-owning root-set node in place.
        *result = new D3DImage(owner);
        return TJS_S_OK;
    }

    tjs_uint getWidth() const {
        return Picture ? Picture->GetObjectNoAddRef()->GetWidth() : 0;
    }
    tjs_uint getHeight() const {
        return Picture ? Picture->GetObjectNoAddRef()->GetHeight() : 0;
    }
    iTVPTexture2D *GetTexture() const {
        return Picture ? Picture->GetObjectNoAddRef() : nullptr;
    }

    void load(tTJSVariant sourceLayer) {
        tTJSNI_Layer *layer = tTJSNI_Layer::FromVariant(sourceLayer);
        iTVPTexture2D *loaded = layer->GetMainImage()->GetTexture();
        if(TVPGetRenderManager()->IsSoftware()) {
            const void *pixels = loaded->GetScanLineForRead(0);
            const tjs_int pitch = loaded->GetPitch();
            loaded = GetD3DRenderManager()->CreateTexture2D(
                pixels, pitch, loaded->GetWidth(), loaded->GetHeight(),
                TVPTextureFormat::RGBA, 0);
        }

        // All four references overwrite the holder without destroying the old
        // one. The software copy's initial reference is likewise not released
        // after the holder adds its own reference.
        Picture = new TJS::tTJSRefHolder<iTVPTexture2D>(loaded);
    }
};

class D3DPicture final : public D3DLayerListener {
    // Both fields are raw borrowers in all four references.  The layer is
    // also the D3DLayerListener owner and must survive this destructor; the
    // image need only survive calls to Draw and is not touched at teardown.
    D3DImage *Image;
    D3DLayer *TransformLayer;
    tjs_int Opacity = 255;
    tjs_int BlendMode = 2;
    // This is the reference's three-pointer std::vector of trivial 16-byte
    // {left,top,right,bottom} tuples.  clear keeps capacity, while a full
    // append grows 1,2,4,8,... on both libstdc++ and libc++ builds.
    std::vector<tTVPRect> ImageRanges;
    tjs_int ImageRangeTail_guess = 0;
    float CoordX = 0.0f;
    float CoordY = 0.0f;
    float Scale = 1.0f;
    float ScaleTail_guess = 0.0f;

    static void AppendVertex(const float (&matrix)[16],
                             std::vector<tTVPPointD> &source,
                             std::vector<tTVPPointD> &destination,
                             float x, float y) {
        source.push_back(tTVPPointD{static_cast<double>(x),
                                    static_cast<double>(y)});
        destination.push_back(tTVPPointD{
            static_cast<double>(matrix[0] * x + matrix[4] * y + matrix[12]),
            static_cast<double>(matrix[1] * x + matrix[5] * y + matrix[13])});
    }

public:
    D3DPicture(D3DLayer *layer, D3DImage *image)
        : D3DLayerListener(layer), Image(image), TransformLayer(layer) {}

    // The generated typed descriptor reserves exactly one Void for an empty
    // adaptor shell.  Ordinary calls require at least these two strict native
    // arguments in this order and ignore surplus arguments.  Conversion is
    // complete before the listener constructor publishes this object.
    static D3DPicture *factory(iTJSDispatch2 *, D3DLayer *layer,
                               D3DImage *image) {
        return new D3DPicture(layer, image);
    }

    tjs_int getOpacity() const { return Opacity; }
    void setOpacity(tjs_int value) { Opacity = value; }
    tjs_int getBlendMode() const { return BlendMode; }
    void setBlendMode(tjs_int value) { BlendMode = value; }
    tjs_int getStretchType() const {
        return D3DLayerListener::getStretchType();
    }
    void setStretchType(tjs_int value) {
        D3DLayerListener::setStretchType(value);
    }
    double getBicubicParam() const {
        return D3DLayerListener::getBicubicParam();
    }
    void setBicubicParam(double value) {
        D3DLayerListener::setBicubicParam(value);
    }

    void assignImageRange(tjs_int left, tjs_int top,
                          tjs_int right, tjs_int bottom) {
        ImageRanges.emplace_back(left, top, right, bottom);
    }
    void clearImageRange() { ImageRanges.clear(); }
    void setCoord(float x, float y) {
        CoordX = x;
        CoordY = y;
    }
    void setScale(float value) { Scale = value; }
    float getScale() const { return Scale; }

private:
    bool IsVisible() override { return false; }

    void Draw(iTVPTexture2D *target) override {
        iTVPRenderManager *manager = GetD3DRenderManager();
        iTVPRenderMethod *method =
            manager->GetRenderMethod(Opacity, true, BlendMode);
        if(!method)
            return;

        std::vector<tTVPPointD> sourcePoints;
        std::vector<tTVPPointD> destinationPoints;
        sourcePoints.reserve(ImageRanges.size() * 6u);
        destinationPoints.reserve(ImageRanges.size() * 6u);

        float matrix[16];
        TransformLayer->CopyMatrixForListener(matrix);
        float x = CoordX;
        float y = CoordY;
        TransformLayer->TransformPoint(x, y);
        matrix[12] = x;
        matrix[13] = y;

        for(const tTVPRect &range : ImageRanges) {
            const float left = static_cast<float>(range.left);
            const float top = static_cast<float>(range.top);
            const float right = static_cast<float>(range.right);
            const float bottom = static_cast<float>(range.bottom);
            AppendVertex(matrix, sourcePoints, destinationPoints, left, top);
            AppendVertex(matrix, sourcePoints, destinationPoints, right, top);
            AppendVertex(matrix, sourcePoints, destinationPoints, left, bottom);
            AppendVertex(matrix, sourcePoints, destinationPoints, right, top);
            AppendVertex(matrix, sourcePoints, destinationPoints, left, bottom);
            AppendVertex(matrix, sourcePoints, destinationPoints, right, bottom);
        }

        tRenderTexQuadArray::Element textures[] = {
            tRenderTexQuadArray::Element(Image->GetTexture(),
                                         sourcePoints.data())};
        const tTVPRect clip(
            0, 0, static_cast<tjs_int>(target->GetWidth()),
            static_cast<tjs_int>(target->GetHeight()));
        manager->OperateTriangles(
            method, static_cast<int>(ImageRanges.size() * 2u),
            target, target, clip, destinationPoints.data(),
            tRenderTexQuadArray(textures));
    }
};

static_assert(sizeof(D3DPicture) ==
                  (sizeof(void *) == 8 ? 0x60u : 0x40u),
              "D3DPicture must match the four-reference layout");

#define REGISTER_DRAW_DEVICE_MEMBERS(CLASS)                                  \
    Factory(&CLASS::factory);                                                \
    NCB_PROPERTY_RO(children, getChildren);                                  \
    NCB_PROPERTY(clearColor, getClearColor, setClearColor);                  \
    NCB_PROPERTY(transState, getTransState, setTransState);                  \
    NCB_METHOD(add);                                                         \
    NCB_METHOD(remove);                                                      \
    NCB_METHOD(startTransition);                                             \
    NCB_METHOD(stopTransition);                                              \
    NCB_METHOD(update);                                                       \
    NCB_METHOD(checkEnable);                                                  \
    NCB_METHOD(getModule);                                                    \
    NCB_METHOD(capture);                                                      \
    NCB_PROPERTY(offsetX, getOffsetX, setOffsetX);                            \
    NCB_PROPERTY(offsetY, getOffsetY, setOffsetY);                            \
    NCB_METHOD(setOffset);                                                   \
    NCB_PROPERTY(stretchType, getStretchType, setStretchType);               \
    NCB_PROPERTY(bicubicParam, getBicubicParam, setBicubicParam);            \
    NCB_PROPERTY(forceRenderTexture, getForceRenderTexture,                  \
                 setForceRenderTexture);                                    \
    Property(TJS_W("interface"), &CLASS::getInterface, int());              \
    NCB_METHOD(setPrimarySize);                                              \
    NCB_PROPERTY(primaryWidth, getPrimaryWidth, setPrimaryWidth);            \
    NCB_PROPERTY(primaryHeight, getPrimaryHeight, setPrimaryHeight);         \
    NCB_METHOD(setScreenRect);                                               \
    NCB_PROPERTY(screenLeft, getScreenLeft, setScreenLeft);                  \
    NCB_PROPERTY(screenTop, getScreenTop, setScreenTop);                     \
    NCB_PROPERTY(screenWidth, getScreenWidth, setScreenWidth);               \
    NCB_PROPERTY(screenHeight, getScreenHeight, setScreenHeight);            \
    NCB_PROPERTY_RO(primaryLayers, getPrimaryLayers);                        \
    NCB_PROPERTY(layerManagerIndex, getLayerManagerIndex,                    \
                 setLayerManagerIndex);                                     \
    NCB_METHOD(getPrimaryLayerBitmap);                                       \
    NCB_PROPERTY_RO(destLeft, getDestLeft);                                  \
    NCB_PROPERTY_RO(destTop, getDestTop);                                    \
    NCB_PROPERTY_RO(destWidth, getDestWidth);                                \
    NCB_PROPERTY_RO(destHeight, getDestHeight)

NCB_REGISTER_CLASS(DrawDeviceD3D) {
    // Independent global.DrawDeviceD3D ClassInfo/guard and concrete adaptor.
    // Registration publishes the tuple before the common 34-entry member
    // surface and global member; a missing global is logged without rollback.
    // Generated unregistration removes the global member when possible and
    // clears the full tuple.  Android folds only the byte-identical complete
    // root destructor with D3D; both deleting destructors and all NCB identity
    // objects remain distinct, while iOS retains both complete bodies too.
    REGISTER_DRAW_DEVICE_MEMBERS(DrawDeviceD3D);
}

NCB_REGISTER_CLASS(D3D) {
    // Independent global.D3D ClassInfo and guard: registration first-publishes
    // the class tuple, allocates a 0xB0/0x70 native-class descriptor and its
    // empty-adaptor allocator, then exposes the same 34-entry surface as
    // DrawDeviceD3D.  Unregistration removes global.D3D when possible and
    // clears the complete tuple; a missing global during registration is only
    // logged and does not roll ClassInfo back.  Android linkers fold the two
    // concrete roots' byte-identical complete destructor, but retain distinct
    // deleting destructors/vtables; iOS retains both destructor bodies.
    REGISTER_DRAW_DEVICE_MEMBERS(D3D);
}

NCB_REGISTER_CLASS(D3DLayer) {
    // Independent concrete ClassInfo: this registration is neither the sticky
    // D3DLayerBase root view nor D3DLayerObject's two-field borrowed view.  The
    // descriptor reserves exactly one Void for a valid empty adaptor; ordinary
    // construction requires arg0 to carry the root view and ignores surplus.
    // Successful attachment is non-sticky and owns the concrete 0x90/0x74/
    // 0x98/0x78 layer.  Generated Regist and Unregist vtable slots both exist,
    // but the integrated loader has no unload/registered-set-erase caller for
    // Unregist, so published class/adaptor state is not reclaimed at runtime.
    Factory(&D3DLayer::factory);
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

NCB_REGISTER_CLASS(D3DImage) {
    // Independent concrete ClassInfo, separate from the D3DLayerBase/root ID.
    // Exactly one Void preserves an empty concrete adaptor; ordinary Factory
    // success owns a 0x18/0x0C D3DImage non-sticky.  Generated Unregist exists,
    // but the integrated loader never unloads/erases the registered module.
    Factory(&D3DImage::factory);
    NCB_PROPERTY_RO(width, getWidth);
    NCB_PROPERTY_RO(height, getHeight);
    NCB_METHOD(load);
}

NCB_REGISTER_CLASS(D3DPicture) {
    // Independent concrete ClassInfo and non-sticky 0x18/0x0C adaptor.  The
    // typed Factory clears its result before the argc>=2 gate (except for the
    // exact-one-Void sentinel), attaches by raw-writing the fresh native into
    // the receiver adaptor, deletes that fresh native on attach failure, and
    // overwrites an already populated adaptor without deleting its old native.
    // Generated Unregist exists but the integrated loader never invokes it;
    // there is also no existing-native CreateAdaptor producer for D3DPicture.
    Factory(&D3DPicture::factory);
    NCB_PROPERTY(opacity, getOpacity, setOpacity);
    NCB_PROPERTY(blendMode, getBlendMode, setBlendMode);
    NCB_PROPERTY(stretchType, getStretchType, setStretchType);
    NCB_PROPERTY(bicubicParam, getBicubicParam, setBicubicParam);
    NCB_METHOD(assignImageRange);
    NCB_METHOD(clearImageRange);
    NCB_METHOD(setCoord);
    NCB_METHOD(setScale);
    NCB_METHOD(getScale);
}

// The four reference translation units construct these two class registrars
// immediately after D3DPicture. They therefore belong to DrawDeviceD3D.dll,
// not to the emoteplayer.dll pre-registration callback.
NCB_REGISTER_CLASS(D3DEmoteModule) {
    // Independent borrowed ClassInfo. The generated zero-argument constructor
    // accepts every nonnegative argc and ignores argv; exactly one Void is the
    // empty-adaptor sentinel. Ordinary construction raw-attaches a non-sticky
    // 0x20/0x1C module, so populated-receiver re-entry leaks the old payload.
    // D3DEmotePlayer.module is the sole existing-native CreateAdaptor producer:
    // it boxes the root-map pointer non-sticky, making the map and TJS adaptor
    // concurrent owners. Generated Unregist exists, but no integrated loader
    // unload/registered-set-erase path reaches it in any of the four references.
    NCB_CONSTRUCTOR(());
    NCB_PROPERTY(maskMode, getMaskMode, setMaskMode);
    NCB_PROPERTY(maskRegionClipping, getMaskRegionClipping,
                 setMaskRegionClipping);
    NCB_PROPERTY(mipMapEnabled, getMipMapEnabled, setMipMapEnabled);
    NCB_PROPERTY(alphaOp, getAlphaOp, setAlphaOp);
    NCB_PROPERTY(protectTranslucentTextureColor,
                 getProtectTranslucentTextureColor,
                 setProtectTranslucentTextureColor);
    NCB_PROPERTY(pixelateDivision, getPixelateDivision, setPixelateDivision);
    NCB_METHOD(setMaxTextureSize);
}

NCB_REGISTER_CLASS(D3DEmotePlayer) {
    // This class owns an independent borrowed ClassInfo tuple. The generated
    // typed Factory requires one D3DLayer argument (one Void is the empty-shell
    // sentinel) and installs a fresh non-sticky 0x38/0x24 listener shell.
    // Typed clone is the sole existing-native producer and boxes its new shell
    // through CreateAdaptor(copy,false,false); neither producer is sticky.
    // The generated Unregist virtual body can clear the tuple, but the four
    // integrated loaders have no module-unload/registered-set-erase caller.
    Factory(&D3DEmotePlayer::factory);

    Variant(TJS_W("MaskModeStencil"),
            static_cast<tjs_int>(motion::MaskModeStencil));
    Variant(TJS_W("MaskModeAlpha"),
            static_cast<tjs_int>(motion::MaskModeAlpha));
    Variant(TJS_W("TimelinePlayFlagParallel"),
            static_cast<tjs_int>(motion::TimelinePlayFlagParallel));
    Variant(TJS_W("TimelinePlayFlagDifference"),
            static_cast<tjs_int>(motion::TimelinePlayFlagDifference));

    // The 54-member order deliberately interleaves methods and properties.
    // load is the only raw descriptor; every fixed-signature entry uses the
    // generated typed ncbind adapter.
    NCB_PROPERTY_RO(module, getModule);                                  // 1
    NCB_METHOD(clear);                                                   // 2
    NCB_METHOD_RAW_CALLBACK(load, &D3DEmotePlayer::loadCompat, 0);       // 3
    NCB_METHOD(clone);                                                   // 4
    NCB_METHOD(show);                                                    // 5
    NCB_METHOD(hide);                                                    // 6
    NCB_PROPERTY(visible, getVisible, setVisible);                       // 7
    NCB_PROPERTY(smoothing, getSmoothing, setSmoothing);                 // 8
    NCB_PROPERTY(meshDivisionRatio, getMeshDivisionRatio,
                 setMeshDivisionRatio);                                 // 9
    NCB_PROPERTY(queing, getQueuing, setQueuing);                        // 10
    NCB_PROPERTY(hairScale, getHairScale, setHairScale);                 // 11
    NCB_PROPERTY(partsScale, getPartsScale, setPartsScale);              // 12
    NCB_PROPERTY(bustScale, getBustScale, setBustScale);                 // 13
    NCB_METHOD(assignState);                                             // 14
    NCB_METHOD(setCoord);                                                // 15
    NCB_METHOD(setScale);                                                // 16
    NCB_METHOD(getScale);                                                // 17
    NCB_METHOD(setRot);                                                  // 18
    NCB_METHOD(getRot);                                                  // 19
    NCB_METHOD(setColor);                                                // 20
    NCB_METHOD(getColor);                                                // 21
    NCB_METHOD(countVariables);                                          // 22
    NCB_METHOD(getVariableLabelAt);                                      // 23
    NCB_METHOD(countVariableFrameAt);                                    // 24
    NCB_METHOD(getVariableFrameLabelAt);                                 // 25
    NCB_METHOD(getVariableFrameValueAt);                                 // 26
    NCB_METHOD(setVariable);                                             // 27
    NCB_METHOD(getVariable);                                             // 28
    NCB_METHOD(startWind);                                               // 29
    NCB_METHOD(stopWind);                                                // 30
    NCB_METHOD(countMainTimelines);                                      // 31
    NCB_METHOD(getMainTimelineLabelAt);                                  // 32
    NCB_METHOD(countDiffTimelines);                                      // 33
    NCB_METHOD(getDiffTimelineLabelAt);                                  // 34
    NCB_METHOD(countPlayingTimelines);                                   // 35
    NCB_METHOD(getPlayingTimelineLabelAt);                               // 36
    NCB_METHOD(getPlayingTimelineFlagsAt);                               // 37
    NCB_METHOD(isLoopTimeline);                                          // 38
    NCB_METHOD(getTimelineTotalFrameCount);                              // 39
    NCB_METHOD(playTimeline);                                            // 40
    NCB_METHOD(isTimelinePlaying);                                       // 41
    NCB_METHOD(stopTimeline);                                            // 42
    NCB_METHOD_DETAIL(setTimelineBlendRatio, Class, void, Class::setTimeline,
                      (ttstr, float, float, float, bool));                // 43
    NCB_METHOD(getTimelineBlendRatio);                                   // 44
    NCB_METHOD(fadeInTimeline);                                          // 45
    NCB_METHOD(fadeOutTimeline);                                         // 46
    NCB_PROPERTY_RO(animating, getAnimating);                            // 47
    NCB_METHOD(skip);                                                    // 48
    NCB_METHOD_DETAIL(pass, Class, void, Class::passTimelines_guess, ()); // 49
    NCB_METHOD(progress);                                                // 50
    NCB_PROPERTY_RO(modified, getModified);                              // 51
    NCB_METHOD(setOuterForce);                                           // 52
    NCB_METHOD(getOuterForce);                                           // 53
    NCB_METHOD(contains);                                                // 54
}

static void DrawDeviceD3D_PreRegist() {
    const tjs_char *rootClassName = TJS_W("D3DLayerBase");
    // There is no global.D3DLayerBase native class.  The callback directly
    // obtains a native class ID and first-publishes {name, id, nullptr} into
    // the internal ClassInfo; a repeated Set is simply ignored.
    (void)D3DLayerBaseClassInfo::Set(
        rootClassName, TJSRegisterNativeClass(rootClassName), nullptr);
    (void)ncbAutoRegister::LoadModule(TJS_W("emoteplayer.dll"));
    D3DLayerObjectClassID = TJSRegisterNativeClass(
        TJS_W("D3DLayerObjectNativeInstance"));
}
NCB_PRE_REGIST_CALLBACK(DrawDeviceD3D_PreRegist);

#undef REGISTER_DRAW_DEVICE_MEMBERS

// The companion module is an alias/dependency shim: its sole PreRegist loads
// the main DrawDeviceD3D module and ignores the bool (already loaded is false).
#undef NCB_MODULE_NAME
#define NCB_MODULE_NAME TJS_W("DrawDeviceD3DZ.dll")
static void DrawDeviceD3DZ_PreRegist() {
    (void)ncbAutoRegister::LoadModule(TJS_W("DrawDeviceD3D.dll"));
}
NCB_PRE_REGIST_CALLBACK(DrawDeviceD3DZ_PreRegist);
