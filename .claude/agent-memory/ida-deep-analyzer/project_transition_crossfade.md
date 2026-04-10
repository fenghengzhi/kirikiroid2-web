---
name: Transition crossfade architecture
description: Complete @trans crossfade call chain from KAG tag to pixel blending, including all function addresses, Layer offsets, and transition handler vtable layout
type: project
---

## Transition System Architecture in libkrkr2.so

### Built-in Transition Handler Providers (registered at 0x86E750)
- **crossfade** - vtable 0x1A29780, simple alpha blend over time
- **universal** - vtable 0x1A298E8, rule-image based transition
- **scroll** - vtable 0x1A29930, scroll-based transition
- Stored in hash map at qword_1AE0640

### CrossFade Handler Provider Vtable (0x1A29780)
| Offset | Address | Name |
|--------|---------|------|
| 0 | 0x86FF10 | AddRef |
| 8 | 0x86E9A4 | Delete |
| 16 | 0x86E9A8 | ? |
| 24 | 0x86E9BC | Release |
| 32 | 0x86E9F4 | GetName -> "crossfade" |
| 40 | 0x86EA14 | StartTransition |
| 48 | 0x86EE74 | CreateHandler |

### CrossFade Handler Instance Vtable (0x1A297C8)
| Offset | Address | Name |
|--------|---------|------|
| 0 | 0x86FBB0 | AddRef |
| 8 | 0x86FBC4 | ? |
| 16 | 0x86FBFC | Release |
| 24 | 0x86EFC0 | Process (compute alpha from time) |
| 32 | 0x86F008 | GetStatus (1=running, 2=done) |
| 40 | 0x86F01C | GetLayerCount/CompositeDispatch |
| 48 | 0x86F464 | SetLayerDest |
| 56 | 0x86FD88 | ? |
| 64 | 0x86FC4C | Destructor |
| 72 | 0x86F074 | Blend (ConstAlphaBlend) |

### CrossFade Handler Object Layout (0x40 bytes)
| Offset | Type | Field |
|--------|------|-------|
| 0 | ptr | vtable |
| 8 | int | refCount |
| 16 | ptr | transOptionDict (TJS dictionary) |
| 24 | int | layerType |
| 32 | int64 | startTick |
| 40 | int64 | totalTime (from "time" param) |
| 48 | byte | firstCall flag |
| 52 | int | maxOpacity (255) |
| 56 | int | currentOpacity (computed) |

### Alpha Computation (CrossFadeHandler_Process at 0x86EFC0)
```
if (firstCall) { firstCall = false; startTick = currentTick; }
progress = (currentTick - startTick) * maxOpacity / totalTime;
if (progress > maxOpacity) progress = maxOpacity;
currentOpacity = progress;
```

### Layer Transition Fields
| Offset | Type | Field |
|--------|------|-------|
| 616 | ptr | transHandler (type 0/1) |
| 624 | ptr | transHandler2 (type 2) |
| 632 | ptr | transPartner (back-pointer from partner layer) |
| 640 | ptr | transOwner (saved owner dispatch) |
| 648 | ptr | transSource (partner layer) |
| 656 | ptr | transSourceOwner (saved source owner dispatch) |
| 664 | byte | transActive (1 when transition running) |
| 665 | byte | transUseChildren (from beginTransition arg3) |
| 666 | byte | transSelfUpdate (read from "selfupdate" dict key) |
| 672 | int64 | transTickStart |
| 680 | byte | transHasCallback |
| 684 | ptr | transCallback1 |
| 692 | ptr | transCallback2 |
| 700 | int | transType (from handler) |
| 704 | int | transStatus (from handler) |
| 712 | ptr | transBitmapWrapper1 |
| 720 | ptr | transBitmapWrapper2 |
| 728 | byte | transPendingComplete |
| 736 | struct | transCompositingTarget (inline, 40 bytes) |
| 776 | ptr | transOrigDrawTarget |
| 784 | ptr | transRenderedSelf |
| 792 | ptr | transRenderedSource |
| 800 | struct | transContinuousEvent (inline callback) |
| 808 | ptr | transContinuousEvent.layer (back-pointer) |

### Call Chain: @trans rule=crossfade time=1200
1. TJS: `layer.beginTransition(dict, useChildren, source, options)`
2. `Layer_beginTransition_NCBWrapper` at 0x82623C - extracts TJS args
3. `Layer_beginTransition` at 0x817EC0 - main implementation
   - Calls `TVPGetTransHandlerProvider` (0x86E750) to find "crossfade" handler
   - Reads "selfupdate" from options dict
   - Reads "callback" from options dict
   - Calls provider->StartTransition (vtable[5]=0x86EA14) -> `CrossFadeProvider_CreateHandler` (0x86EE74)
     - Reads "time" from dict, clamps to min 2
     - Creates handler with maxOpacity=255, totalTime=time
   - Stores handler at layer+616
   - Stores partner layer at layer+648
   - Sets layer+664 = 1 (active)
   - If !selfupdate: adds layer+800 to continuous event list via TVPContinuousEventList_Add (0x8E0A0C)
4. Each frame: `TVPContinuousEventDispatch` (0x8DF8AC) fires continuous events
5. Layer draw path: `Layer_InternalCompleteStruct_guess` (0x814870)
   - Calls `Layer_GetTransitionTick` (0x814B64) -> gets tick from callback or system clock
   - Calls handler->Process (vtable[3]=0x86EFC0) with current tick
   - Process computes: `currentOpacity = (elapsed * 255) / totalTime`
   - Calls handler->GetStatus (vtable[4]=0x86F008): returns 1 if running, 2 if done
   - If done: calls `Layer_TransitionCompleted` (0x8187F0)
6. Layer rendering: `Layer_DrawSelf` (0x81685C) / `Layer_DrawWithTransition` (0x815614)
   - When transition active with image: calls `Layer_TransitionComposite` (0x8159CC)
   - TransitionComposite calls handler->GetLayerCount (vtable[5]=0x86F01C)
   - GetLayerCount dispatches: if opacity=0 use src, if opacity=max use dst, else call Blend
   - `CrossFadeHandler_Blend` (0x86F074):
     - Selects ConstAlphaBlend function based on layerType
     - Calls ConstAlphaBlend_SD (for type 2/ltAlpha) or ConstAlphaBlend_SD_d/a variants
     - Blends source bitmap + dest bitmap -> output with currentOpacity as blend factor

**Why:** Understanding transition system for Web port alignment
**How to apply:** Use these addresses and offsets when implementing/fixing transition support in the Web build
