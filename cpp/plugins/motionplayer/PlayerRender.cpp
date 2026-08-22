// PlayerRender.cpp — render state and canvas/source/cache helpers
// Split from Player.cpp for maintainability.
//
#include "PlayerInternal.h"
#include "SourceCache.h"

using namespace motion::internal;

namespace {
    struct DispatchReleaseGuard_guess {
        iTJSDispatch2 *dispatch = nullptr;

        ~DispatchReleaseGuard_guess() {
            if(dispatch) {
                dispatch->Release();
            }
        }
    };

    // The native MotionNode fallback has one process-lifetime cache word for
    // its ResourceManager.findSource dispatch. Its consumers stay in this TU.
    tjs_uint32 findSourceMemberHint_guess = 0;
}

namespace motion {
    // --- Drawing/rendering ---
    tjs_error Player::dispatchFindSource_guess(
        iTJSDispatch2 *resourceManager,
        tTJSVariant &contextArgument,
        ttstr name, tTJSVariant &result) {
        // The generic fallback is script-visible. The outer resolver already
        // retained the original receiver and snapshotted the context before
        // any native atlas work; preserve those exact owners through the call.
        // The raw status is intentionally not normalized: the caller treats
        // every nonzero value as failure and the result aliases SourceState.
        tTJSVariant pathArgument(std::move(name));
        tTJSVariant *arguments[] = { &contextArgument, &pathArgument };
        return resourceManager->FuncCall(
            0, TJS_W("findSource"), &findSourceMemberHint_guess,
            &result, 2, arguments, resourceManager);
    }

    tTJSVariant Player::findSource(ttstr name) {
        DispatchReleaseGuard_guess resourceManager{
            _findSourceResourceManager.AsObject()};
        tTJSVariant contextArgument(_findMotionContextVariant);
        tTJSVariant result;
        (void)dispatchFindSource_guess(
            resourceManager.dispatch, contextArgument,
            std::move(name), result);
        return result;
    }

} // namespace motion
