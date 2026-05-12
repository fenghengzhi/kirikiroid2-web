# Full Native `__Private_Motion_GLLayer` Identity Alignment

## Summary
- Replace the current `PrivateMotionGLL` class object in [PrivateMotionGLL.cpp](/Users/bytedance/WebstormProjects/krkr2/krkr2/cpp/plugins/motionplayer/PrivateMotionGLL.cpp:1) with a dedicated native-style `__Private_Motion_GLLayer` class dispatch that has its own singleton, class name, and ClassID, matching `PrivateMotionGLL_CreateClass @ 0x6DD284`.
- Use a native-only private class route: one dispatch object, one native instance, registered only under `g_PrivateMotionGLL_ClassID`. Do not add Layer member-copy or dual-`ClassID` compatibility behavior unless fresh native evidence proves it exists.
- This file currently covers the class-identity/member-wrapper slice. It is necessary for full restoration, but not sufficient by itself for complete `__Private_Motion_GLLayer` source-structure/lifecycle/render parity.

## Implementation Changes
- In [PrivateMotionGLL.cpp](/Users/bytedance/WebstormProjects/krkr2/krkr2/cpp/plugins/motionplayer/PrivateMotionGLL.cpp:1), replace `tTJSNC_PrivateMotionGLLLayerLike_0x800438 : tTJSNC_Layer` with a dedicated `tTJSNativeClass`-based private class:
  - Class name literal must be `__Private_Motion_GLLayer`.
  - Singleton creation must follow the current guarded-lifetime shape already added for `0x6D5948`.
  - The class object must register only the native members seen in `PrivateMotionGLL_CreateClass @ 0x6DD284`: constructor, `setSize`, `visible`, `absolute`.
  - Do not keep `tTJSNC_Layer` as the class object base, and do not use a separate `Layer` superclass instance via `SetSuper`/`CreateNew` fallback.
- Use the `tTJSNativeClass` `CreateNew` path with a private `CreateNativeInstance()` implementation that constructs one dispatch object and one `tTJSNI_PrivateMotionGLLLayerLike_0x800438`:
  - Register that native instance only under `g_PrivateMotionGLL_ClassID`.
  - Do not register the same native instance under `tTJSNC_Layer::ClassID`.
  - Do not copy instance members from the global `Layer` class object.
  - Do not call `Layer::CreateNew`, `SetSuper`, or any fallback that creates a second native `Layer` instance.
- Keep the `0x800438` constructor semantics unchanged: owner closure read, target coercion through `Layer::ClassID`, parent/owner checks, `ConstructResolvedTreeOwnerLike_0x800438`, `visible=true`, `opacity=255`.
- Add private-class resolvers where native explicitly uses `g_PrivateMotionGLL_ClassID`:
  - `ensurePrivateMotionGLLLike_0x6D5948` must fetch the private native instance by private ClassID before applying `absolute`, `visible`, and `SetSize`.
  - The private constructor and the private `setSize` / `visible` / `absolute` wrappers must query private ClassID, mirroring `0x6DE24C`, `0x6DE2E0`, `0x6DE46C`, `0x6DE4EC`, `0x6DE5C8`, and `0x6DE64C`.
  - Keep `tryResolveLayerDispatch` Layer-only for SLA `+20` target coercion; do not broaden that helper to “anything layer-like”.
- Audit only the top-level `PrivateMotionGLL` render-target entry points that directly touch the private object before generic Layer helpers:
  - `renderMotionFrameToTarget` / `executeLayerRenderCommands` must not rely on `Layer::ClassID` resolving the private object.
  - The SLA render tail `Update(false)` must also use the explicit private resolver; otherwise the native-only private object is rendered but never invalidated/updated by the non-accurate SLA path.
  - If a path needs Layer-like operations on the private target, add an explicit private-`ClassID` resolver and operate on the private native instance, mirroring the wrappers at `0x6DE24C`, `0x6DE2E0`, `0x6DE46C`, `0x6DE4EC`, `0x6DE5C8`, and `0x6DE64C`.
  - Do not rewrite unrelated ordinary-Layer paths in motionplayer just to accommodate the private object.

## Important Interfaces
- No public scripting API changes.
- Internal `PrivateMotionGLL` class identity changes from “special Layer subclass object” to “dedicated private class with its own private ClassID”.
- No `tTJSCustomObject` lifecycle change is planned for this slice, because the private object must not depend on duplicate native-instance slots.

## Remaining Native-Parity Gaps
- `sub_6DD430` native factory is more than a plain `tTJSNI_Layer` allocation:
  - allocates `0x388` bytes;
  - calls base Layer constructor `sub_7FFEF0`;
  - installs private vtables `off_1A1A808`, `off_1A1A868`, `off_1A1A890`;
  - zeroes `+824..+904`;
  - initializes a private deque-like container at `+824` via `sub_6DDBD8(..., 0)`;
  - sets `*(this+176)=2`.
- The local private native now has a native-shaped 88-byte render item type and deque lifecycle scaffold. `renderMotionFrameToTarget` clears it like the entry of `Player_RenderMotionFrame @ 0x6DE738`, appends eligible prepared render items, stores full source texture bounds instead of target clip rect, stores native packed float-point vectors, and holds source textures with AddRef/Release while queued.
- Private destructor entries `sub_6DD4CC` / `sub_6DD518` first destroy the `+824` container through the cleanup body at `loc_6DDD54`, then call base Layer destructor `sub_800320`; local RAII now gives the same destruction order for the scaffolded queue and releases item+80 source textures / item+0 heap point vectors. This is still a local C++ container representation, not byte-for-byte native deque storage.
- Private vtable differs from base Layer vtable at least at the draw/render override slot:
  - private uses `sub_6DD56C`;
  - base Layer uses `sub_815CF0`.
  Local `tTJSNI_PrivateMotionGLLLayerLike_0x800438` now overrides `Draw_GPU` and consumes the private queue instead of inheriting ordinary Layer self/child drawing. Remaining work is to retire or narrow the still-present external `Player::executeLayerRenderCommands` fallback after runtime/differential evidence proves the private draw path covers the SLA render surface.
- Base constructor `sub_7FFEF0` field initialization mostly matches [LayerIntf.cpp](/Users/bytedance/WebstormProjects/krkr2/krkr2/cpp/core/visual/LayerIntf.cpp:337), but a proper audit still needs a field-offset table before claiming object layout parity.

## Test Plan
- Static checks:
  - `PrivateMotionGLL.cpp` must no longer define the private class object as `public tTJSNC_Layer`.
  - The file must contain the literal `__Private_Motion_GLLayer`.
  - The private wrappers must fetch by private ClassID, not by `tTJSNC_Layer::ClassID`.
- Build checks:
  - Run `cmake --preset "Web Debug Config"` if the Web Debug tree needs toolchain refresh.
  - Run `cmake --build out/web/debug`.
- Focused runtime check:
  - Run `out/macos/debug/tests/unit-tests/plugins/motionplayer-dll "__Private_Motion_GLLayer uses private ClassID only"`.
- Acceptance criteria:
  - Build succeeds with no new errors from `PrivateMotionGLL.cpp` or `tjsObject.cpp`.
  - The implementation uses one private dispatch object and one private native instance, not a private object plus a second superclass `Layer` instance.
  - The private object is not registered under `tTJSNC_Layer::ClassID`, and no global `Layer` member-copy bridge is added.
  - Generic motionplayer `Layer::ClassID` helpers remain unchanged except where the private target is explicitly the subject.

## Assumptions
- Native evidence currently proves private class identity and private-`ClassID` native-instance access, not Layer member copying or dual `Layer::ClassID` registration. Any local call site that needs Layer-like behavior on the private target must be handled through an explicit private native-instance path.
- This slice is validated by focused identity/runtime checks plus Web Debug build; it does not claim full render-path parity.
- Do not mark the full `__Private_Motion_GLLayer` goal complete until the private `Draw_GPU @ 0x6DD56C` override is runtime/differential validated, the remaining byte/layout gaps around the `+824` deque and base `sub_7FFEF0` fields are audited, and evidence confirms no regression against `libkrkr2.so`.
- This slice does not include broader render-path differential work, D3D revalidation, or IDA DB renaming/typing cleanup unless they become necessary to land the class-identity change itself.
