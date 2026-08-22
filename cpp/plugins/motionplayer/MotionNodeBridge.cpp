//
// TJS↔Native bridge helpers for MotionNode.
// Implements the helper methods declared in MotionNode.h.
// Separated to avoid circular dependency: MotionNode.h cannot include
// Player.h or ncbind.hpp, but these helpers need both.
//
// Current four-reference reconstruction:
//   - nodeType=3 owns a Variant wrapping a child Player dispatch;
//   - nodeType=4 owns a Variant wrapping a TJS Array of Player Variants;
//   - particle callers retain one Array dispatch across their whole pass;
//   - malformed Array/element values throw instead of becoming quiet no-ops.
//

#include "MotionNode.h"
#include "MotionDispatch.h"
#include "Player.h"
#include "ncbind.hpp"
#include "tjsArray.h"

namespace motion::detail {

    using PlayerAdaptor = ncbInstanceAdaptor<Player>;

    // --- nodeType=3: Motion child Player ---

    Player* MotionNode::getChildPlayer() const {
        // AsObjectNoAddRef throws on a non-object Variant. A null dispatch or
        // failed native-instance query returns null; native callers decide
        // whether that malformed boundary is guarded or dereferenced.
        auto *dispatch = childPlayerVar.AsObjectNoAddRef();
        if (!dispatch) return nullptr;
        return PlayerAdaptor::GetNativeInstance(dispatch);
    }

    // --- nodeType=4: Particle children TJS Array ---

    ScopedVariantObjectDispatch_guess::ScopedVariantObjectDispatch_guess(
        const tTJSVariant &arrayVariant) :
        dispatch_(arrayVariant.AsObject()) {}

    ScopedVariantObjectDispatch_guess::~ScopedVariantObjectDispatch_guess() {
        if(dispatch_) {
            dispatch_->Release();
        }
    }

    tjs_int particleArrayCount_guess(iTJSDispatch2 *array) {
        tTJSVariant count;
        (void)array->PropGet(
            0, TJS_W("count"), nullptr, &count, array);
        return static_cast<tjs_int>(count.AsInteger());
    }

    Player *particleArrayGetNativePlayerAt_guess(
        iTJSDispatch2 *array, tjs_int index) {
        tTJSVariant element;
        (void)array->PropGetByNum(0, index, &element, array);
        return PlayerAdaptor::GetNativeInstance(
            element.AsObjectNoAddRef(), true);
    }

    void particleArrayAdd_guess(iTJSDispatch2 *array,
                                const tTJSVariant &playerVariant) {
        tTJSVariant argument(playerVariant);
        tTJSVariant *arguments[] = {&argument};
        (void)array->FuncCall(
            0, TJS_W("add"), &particleArrayAddMemberHint_guess,
            nullptr, 1, arguments, array);
    }

    void particleArrayErase_guess(iTJSDispatch2 *array, tjs_int index,
                                  tTJSVariant *result) {
        tTJSVariant argument(index);
        tTJSVariant *arguments[] = {&argument};
        (void)array->FuncCall(
            0, TJS_W("erase"), &particleArrayEraseMemberHint_guess,
            result, 1, arguments, array);
    }

    int MotionNode::getParticleCount() const {
        ScopedParticleArrayDispatch_guess array(particleArrayVar);
        return static_cast<int>(particleArrayCount_guess(array.get()));
    }

    Player* MotionNode::getParticleChild(int index) const {
        ScopedParticleArrayDispatch_guess array(particleArrayVar);
        return particleArrayGetNativePlayerAt_guess(array.get(), index);
    }

} // namespace motion::detail
