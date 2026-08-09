# Cluster K — Motion namespace classes alignment audit (2026-05-30)

> Superseding registrar update (2026-08-04): fresh decompilation closed K-7. The current
> Motion block emits 23 constants, 11 subclasses (Player sixth), then two namespace methods in one
> registration flow. There is no `global.Player`, post alias, deferred function attach, or
> `useD3D` descriptor overwrite.

libkrkr2.so authoritative. Decompiled this session:
- Motion_Player_findSource @0x6948e8 (renamed; the source resolve/cache + texture pipeline)
- Motion_createTextureFromPixels @0x695d04
- Motion_doAlphaMaskOperation @0x6af104
- Motion_getD3DAvailable @0x6b0960
- motionplayer_ncb_register @0x6d9b08 (namespace registrar)
- Motion_{SourceCache,ObjSource,ResourceManager,Point,D3DAdaptor}_ncb_register wrappers @0x6fe124/0x6fe610/0x6feac4/0x6fc6e8/0x6ff2f8
- *_ncb_registerMembers bodies: SourceCache@0x6a85a8, ObjSource@0x69ccb8, ResourceManager@0x6ab8bc, Point@0x690fbc, D3DAdaptor@0x6ace94
- PrivateMotionGLL_CreateClass @0x6dd284, PrivateMotionGLL_Constructor @0x6de24c

================================================================
## 1. Motion_Player_findSource @0x6948e8  (NOTE: this is Player::findSource, NOT SourceCache)

Pseudocode (binary):
```
findSource(out, this(Player), &nameVar, &iconVar):
  rm = this+636 (ResourceManager backptr; AddRef)            # +636, not 652==1 guard
  rmState = rm->PropGet(2, dword_1AB8098 /*classID*/) -> v10  # native ResourceManager instance
  snap = copy(this+1012)  (motion snapshot ttstr)
  if rmState && name=="blank" :
     t = rmState+224 (type)
     if t==2:  # embedded PSB resource path
        hash name -> bucket sub_6EB8F4(rmState+88, hash% rmState+96)  # hashmap probe
        node=*bucket+16 ; if none -> out.flag=0
        get node["source"]; if PropGet "source" fails -> cleanup, ret
        hash source -> bucket sub_6E2060(node+8, hash% vt[2])  # 2nd hashmap (texture cache)
        if cached texture: out+24 = cached->+16
        else:
           read truncated_width/height,width(v52),height(v128),type(v57 c-str),pixel(v60 ptr) from node
           buf = alloc(4*width*height); 
           if type=="RGBA8": TVPReverseRGB(buf, pixel, len>>2)
           elif type=="A8L8": expand 2bpp->BGRA per pixel
           else: warn "MotionPlayer.findSource: Unsupported texture format '%1'"
           tex = Motion_createTextureFromPixels()->vt[24](buf,pitch,w,h,4,1)  # update tex
           store tex into node texture cache slot (sub_6E2150); out+24=tex
        read icon node[narrow(icon)] -> originX/originY/width/height (int) , left/top -> out+96/100, blank=0
        out.flag=1
     elif t==1:  # KAG layer-image path
        out+112 = nameVar (AddRef/Release)
        if this+909: ok = sub_695DE8(out, rmState, snapshot); if ok out.flag=1
     else default LABEL_142
  LABEL_142 (no PSB / name!=blank / type fallthrough):
     out+24=0
     build path ttstr from name(+icon): if iconVar -> sub_A1359C(name,icon) else name only
     ok = rmState->FuncCall("findSource", out+4, [snapshot, path])   # TJS dispatch findSource
     if fail or out+20==0: out.flag=0
     else:
        out.flag=1; o=out+4 dispatch
        out+32=propGet "width"; out+40="height"; out+48="originX"; out+56="originY"
        out+1 = propGet "blank" & 1
        propGet "clip" -> if obj: left/top/right/bottom -> out+64..88 ; else 0,0,1,1
        out+96=0; out+104=(int)width; out+108=(int)height
```
Key binary facts: name=="blank" gate (sub_9B1ED0); hash = FNV-ish `(1025*h)^(>>6)` then `9*` then `32769*(x^(x>>11))`, 0->-1; TWO native hashmaps on the ResourceManager native instance (resource nodes @+88, texture cache @ node+8); TJS-dispatch fallback `findSource(snapshot, path)` returns an object with width/height/originX/originY/blank/clip read by string-key propGet.

Local counterpart: **NO 1:1 function.** `SourceCache::findSource` (SourceCache.cpp:602) = thin `loadSourceByName(name,{})`. Player-side findSource: grep `Player_findSource`/`findSource` in Player*.cpp.

Architecture verdict (2026-07-18 corrected): 🔧 PARTIAL. ResourceManager public
inheritance and each outer-map mapped record's Win/KRKR maps, AddRef/Release ownership,
reverse destruction and unload lifetime are restored. SourceCache's byte-budgeted list is a
different cache chain and must not be used to characterize Player_findSource's current
topology. The remaining gap is that pixel metadata/resources are still reached through
the decoded `MotionSnapshot` side graph instead of raw `PSBRawNode` navigation.

================================================================
## 2. Motion_createTextureFromPixels @0x695d04

Pseudocode:
```
static once-guard: g = sub_84B3A4( ttstr("opengl") ); cache qword_1AB8528; return it
```
It is NOT "create texture from pixels": it is a guarded singleton accessor returning the "opengl" GLES texture-backend/manager object (sub_84B3A4 = lookup render backend by name). The caller (findSource) then calls its vtbl[24] (=update/upload). 
Local counterpart: closest is `TVPGetRenderManager()->CreateTexture2D(...)` (SourceCache.cpp:594). 
Verdict: 🔧 mis-modeled. Binary acquires a named ("opengl") backend singleton once and calls vtbl+24 to upload into an existing cache slot; local constructs a fresh texture via the generic RenderManager each call. The "opengl"-keyed singleton (byte_1AB8530 guard / qword_1AB8528) has no local equivalent.

================================================================
## 3. Motion_doAlphaMaskOperation @0x6af104  (namespace-level static fn)

Pseudocode (binary):
```
doAlphaMask(maskLayerVar, dstX,dstY, srcLayerVar, srcX,srcY, w,h, threshold, op, mode):
  maskObj = AddRef(maskLayerVar)
  clipLeft/Top/Width/Height = maskObj.propGet (only if HasMember, else 0; via vtbl+32 then sub_6635DC)
  intersect dst rect with clip -> adjust srcX/Y, w(v30),h(v31); if w<1||h<1 skip
  L0 = sub_A7A050(srcLayer); Lm = sub_A7A050(maskObj)
  if hasGPUAccel: get GPU bitmap ptrs (vtbl+64 base, vtbl+80 pitch) -> CPU pixel loops
  else: lock CPU buffer (vtbl+16) -> shader path (sub_84B454 backend, vtbl+160 drawShader)
  op switch:
    op==1 (AlphaMask): mode 5/6 -> AddAlphaMask add-shader(32774,1,1,771); mode==1 fillRect borders + AlphaMask mul-shader(770)/CPU dst.a=src.a*dst.a/255; mode==2 AlphaMaskRev(771)/CPU dst.a=(~src.a)*dst.a/255
    op==0 default: CPU threshold ops:
       (mode 5/6)->AddAlphaMask add ; (mode==2 crop: a>=thr ->dst.a=0 / AlphaMaskThresholdCrop step) ; (mode==1: fillRect borders + a<thr->dst.a=0 / AlphaMaskThreshold step shader) ; else (5/6 thr: a>=thr->dst.a=255 / AlphaMaskThresholdFill step)
  then update(v128,row,w,h) via maskObj.FuncCall L"update"
```
String constants (exact, all ASCII GLSL/shader names): "AddAlphaMask","AlphaMask","AlphaMaskRev","AlphaMaskThreshold","AlphaMaskThresholdFill","AlphaMaskThresholdCrop", uniform "threshold", and the fixed fragment shaders. Dispatch keys (UTF-16): L"clipLeft/clipTop/clipWidth/clipHeight","fillRect","update". GPU-blend factors: 32774(GL_FUNC_ADD eq) with src/dst factor args (1,1,771 / 1,0,770 / 1,0,771 / step shaders 32774/32776 with 770/771).

Local counterpart: **MISSING.** No alpha-mask pixel op anywhere. main.cpp:279 binds `NCB_METHOD(doAlphaMaskOperation)` to a Player method (wrong owner — see finding K-7); grep the Player impl — it is a stub at best. No shader cache, no fillRect-border passes, no CPU per-pixel `dst.a = src.a*dst.a/255` loops.

================================================================
## 4. Motion_getD3DAvailable @0x6b0960

Binary: `return (hasGPUAccel_guess() & 1) == 0;`  (D3D "available" == NOT GPU-accelerated GLES path).
Current local counterpart is the namespace-level `motion_getD3DAvailable`, registered directly on
Motion after all subclasses. Owner and registration timing now match; the constant `true` result is
the documented Web platform boundary because the port has no GLES-vs-D3D backend split.

================================================================
## 5. NCB registration — namespace registrar @0x6d9b08 (motionplayer_ncb_register)

Binary registers under the **Motion namespace object** (not on Player):
- 23 constants (0x10000 flag = static const): LayerType{Obj0,Shape1,Layout2,Motion3,Particle4,Camera5}, ShapeType{Point0,Circle1,Rect2,Quad3}, PlayFlag{Force1,Chain2,AsCan4,Join8,Stealth16}, TransformOrder{Flip0,Slant3,Zoom2,Angle1}, CoordinateRecutangular{XY0,XZ1}, **MaskModeStencil0, MaskModeAlpha1**.
- Subclasses in order: Point, Circle, Rect, Quad, LayerGetter, **Player** (key "Player" @MEMORY[0x14C1E9C][5]), SourceCache, ObjSource, ResourceManager, SeparateLayerAdaptor, D3DAdaptor.
- **Namespace-level functions: doAlphaMaskOperation (@0x6da1f0) and getD3DAvailable (@0x6da260)** — registered directly on the Motion namespace dispatch, NOT on any class.

Local (2026-08-04 current `main.cpp` Motion block):
- emits all 23 constants in binary order, including both MaskMode constants;
- emits the exact eleven subclasses in binary order, with Player sixth and no EmotePlayer row;
- emits doAlphaMaskOperation and getD3DAvailable directly on Motion after D3DAdaptor;
- has no `global.Player`, post alias, or deferred function-registration callback.

The complete registration structure now matches `motionplayer_ncb_register@0x6D9B08`.

================================================================
## 6. Per-class member-set diffs

### SourceCache (binary @0x6a85a8) — members: constructor, loadSource(sub_6A7BA8), clearCache(sub_6A8438), property bufLayer RO(getter sub_6A84FC).  [4 members]
Local (main.cpp:20): constructor, loadSource, clearCache, bufLayer RO. ✅ member set MATCHES.
Caveat: binary loadSource = sub_6A7BA8 (a distinct function, not
findSource/0x6948e8). Its layer-list implementation is a separate audit item; it
does not describe the now-restored per-resource Win/KRKR maps.

### ObjSource (binary @0x69ccb8) — members: constructor(sub_6E3BC8 base), prop originX RO(sub_69D014), originY RO(sub_69D0D8), width RO(sub_69D19C), height RO(sub_69D27C), clip RO(sub_69D35C), method drawLayer(sub_69D6D8).  [7 members]
**Superseding correction (2026-07-19):** this historical local verdict is no
longer true. ObjSource now owns the exact raw PSB owner/node pair plus lazy
texture and registers originX/originY/width/height/clip/drawLayer. The former
key/src/blendMode/color facade was deleted.

### ResourceManager (binary @0x6ab8bc) — constructor + 12 exposed members:
loadSource(sub_6A7BA8), clearCache(sub_6A8438), bufLayer RO(sub_6A84FC), load(ResourceManager_loadResource), unload(sub_6A959C), unloadAll(0x6A8CF8), isExistMotion(sub_6A96F8), findMotion(sub_6A9ED4), findSource(sub_6AAB3C), random(sub_6AB56C), requireLayerId(sub_6AB694), releaseLayerId(sub_6AB750).

**2026-07-23 current local verdict:** `NCB_REGISTER_SUBCLASS(ResourceManager)`
registers those same 12 methods/properties in binary order, including inherited
`loadSource`/`clearCache`/`bufLayer` and RM-own `unloadAll`/`isExistMotion`/
`findMotion`/`findSource`/`random`/layer-id methods. `ResourceManager` publicly
inherits `SourceCache`, whose source-level container is `std::list<Entry>`; the
old “unrelated classes/different containers” and “hand-written intrusive LRU”
descriptions are obsolete. `setEmotePSBDecryptSeed/Func` are injected by the
separate emoteplayer registration path rather than belonging to this 12-member
table, so they are not evidence of a motionplayer ResourceManager registrar
divergence. This member-set closure does not prove every method body globally
100%; use the per-function audits for remaining behavioral gaps.

### Point (binary @0x690fbc) — members: constructor, type RO(sub_691248), contains(Player_hitTest!), x RO(sub_691250), y RO(sub_691258).  [5]
Local (main.cpp:29): constructor, type RO, contains, x RO, y RO. ✅ member set MATCHES.
Caveat: binary `contains` = Player_hitTest (shared hit-test fn); local `contains` returns false stub (SourceCache.h:145). ⚠️ impl stub vs real hit-test.

### D3DAdaptor (binary @0x6ace94) — 19 members:
constructor(D3DAdaptor_constructor), setPos(nullsub_81), setSize(sub_6AD7A8), setClearColor(sub_6AD7B0), setResizable(sub_6AD7B8), removeAllTextures(sub_6AD8B8), removeAllBg(nullsub_82), removeAllCaption(nullsub_83), registerBg(nullsub_84), registerCaption(nullsub_85), unloadUnusedTextures(nullsub_86), visible prop(get sub_6AD904/set sub_6AD90C), alphaOpAdd prop(sub_6AD918/sub_6AD920), captureCanvas(D3DAdaptor_captureCanvas), canvasCaptureEnabled prop(sub_6ADAE8/sub_6ADAF0), clearEnabled prop(sub_6ADAFC/sub_6ADB04).
Local (main.cpp:100): factory, setPos, setSize, setClearColor, setResizable, removeAllTextures, removeAllBg, removeAllCaption, registerBg, registerCaption, unloadUnusedTextures, captureCanvas, visible, alphaOpAdd, canvasCaptureEnabled, clearEnabled. ✅ member set MATCHES (note binary has no explicit "setPos" name change; many are nullsub stubs — acceptable platform stubs for Web/D3D).

================================================================
## 7. PrivateMotionGLL

Binary PrivateMotionGLL_CreateClass @0x6dd284: builds ncb class (class object 0xB0 bytes), classID = sub_9F4F18(off_1AA40F0). Members: constructor(PrivateMotionGLL_Constructor), method **setSize**(sub_6DE2E0), prop **visible**(get sub_6DE46C/set sub_6DE4EC), prop **absolute**(get sub_6DE5C8/set sub_6DE64C). Destructor slot +168 = sub_6DD430. [4 members]
Constructor @0x6de24c: thin — propGet(2, classID) on a4 to fetch parent native ctor, then tail-calls it (a base-class delegating constructor).

Local PrivateMotionGLL.h: NO NCB class at all — it is modeled as a set of free helper functions (ensurePrivateMotionGLLLike_0x6D5948, append/clear render-queue helpers) over a raw iTJSDispatch2, plus a std::vector<PackedPoint> render-item struct.
Verdict: 🔧 architectural divergence. Binary PrivateMotionGLL is a real registered TJS native class (setSize/visible/absolute) with a delegating constructor; local has no class, no setSize/visible/absolute members, and uses std::vector for the point list where the binary uses its own render-item layout. (PrivateMotionGLL is an internal class created by SLA @0x6D5948; whether it must be a full NCB class for Web parity depends on whether scripts touch .visible/.absolute/.setSize — flagged for module-driver.)

================================================================
## FINDINGS TABLE

| id | func@addr | local file:line | sev | one-line |
|----|-----------|-----------------|-----|----------|
| K-1 | Motion_Player_findSource @0x6948e8 | ResourceManager/PlayerResource | AUDITED SITES + BOUNDARY | **2026-07-23 再纠正**：raw mapped record/两内表结论保留；旧 CLOSED 漏掉 `0x6F1060→0x695DE8`、item→SourceState alias 与 getter 后 rect 重读。该链及解码分支边界现已补齐；KRKR 整页上传是 Web API 边界，未审计余部不得外推为全局 100% |
| K-2 | Motion_createTextureFromPixels @0x695d04 | SourceCache.cpp:594 | P1 🔧 | Binary = guarded "opengl" backend singleton + vtbl+24 upload into cache slot; local = TVPGetRenderManager()->CreateTexture2D per-call |
| K-3 | Motion_doAlphaMaskOperation @0x6af104 | (none) / main.cpp:279 | P0 ❌ | Alpha-mask op MISSING: no shader cache, fillRect borders, or CPU dst.a=src.a*dst.a/255 loops; also wrong registration owner |
| K-4 | Motion_getD3DAvailable @0x6b0960 | main.cpp:278 + Player impl | P1 ⚠️ | Must be `!hasGPUAccel`; registered on Player not namespace |
| K-5 | ObjSource_ncb_registerMembers @0x69ccb8 | main.cpp:26 | P0 ❌ | Local registers only constructor; binary has originX/originY/width/height/clip RO + drawLayer method (6 missing) |
| K-6 | ResourceManager_ncb_registerMembers @0x6ab8bc | main.cpp ResourceManager registrar | ✅ member set | Constructor + 12 exposed members match, including bufLayer/unloadAll/isExistMotion/findMotion/random; setEmotePSBDecrypt* belongs to the separate emoteplayer injection path |
| K-7 | motionplayer_ncb_register @0x6d9b08 | current Motion registration block | ✅ CLOSED 2026-08-04 | Exact `23 constants -> 11 subclasses -> 2 functions`; Player is sixth in-flow row, EmotePlayer remains independently owned, no post alias/deferred attach |
| K-8 | Point_ncb_registerMembers @0x690fbc | SourceCache.h:145 | P2 ⚠️ | `contains` = Player_hitTest in binary; local returns false stub (member set otherwise matches) |
| K-9 | PrivateMotionGLL_CreateClass @0x6dd284 | PrivateMotionGLL.h | P2 🔧 | Binary is a registered TJS class (setSize/visible/absolute + delegating ctor); local has no class, uses free fns + std::vector |
| K-10 | SourceCache loadSource impl | SourceCache.cpp | ✅ named chain aligned 2026-07-23 | `std::list<Entry>`; full Variant key+src+blend identity, mutable color, node copy/erase, greedy byte trim, exact descriptor bridge. This remains independent of Player_findSource's mapped-record texture maps. |

## MISSING (no local counterpart)
- Motion_doAlphaMaskOperation full body (shader cache + CPU pixel loops + fillRect-border passes).
- ObjSource: originX/originY/width/height/clip RO props + drawLayer.
- Player::findSource 的 raw PSBRawNode 像素导航（容器/生命周期已在 2026-07-18 复原）。
- PrivateMotionGLL as a registered NCB class (setSize/visible/absolute).

## ALIGNED (member set)
- SourceCache (4), Point (5), D3DAdaptor (19): member sets match (impls vary; D3DAdaptor nullsub stubs acceptable for Web).
- All 23 namespace constants, including MaskModeStencil/MaskModeAlpha: match.
- Motion subclass order and both in-flow namespace function registrations: match.

## PLATFORM_BOUNDARY notes
- None explicitly annotated in the audited K files. D3DAdaptor's nullsub-backed methods (setPos/removeAllBg/registerBg/...) are binary-side stubs already, so local stubs are faithful, not platform deviations.

## VERDICT
**2026-07-19 superseded verdict:** K-1 的容器归属与生命周期、K-5 ObjSource
成员、RM/SourceCache 继承均已恢复。Win/KRKR 与非-atlas ObjSource 都已改为 raw
`PSBRawNode` 导航；ObjSource 的 clip/ensureTexture/drawLayer、adaptor 失败泄漏及
texture→owner 析构顺序已闭合。旧 dict-facade、decoded→raw open 与
“list+shared_ptr 是根因”结论不得继续作为当前状态引用。其余 K 项按各自后续审计处理。
