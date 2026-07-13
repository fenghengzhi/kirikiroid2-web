---
name: open6-d3demoteplayer-consts-emoteobject-ctor
description: Open#6 三子项裁决 — D3DEmotePlayer 4常量类归属(已实装)/EmoteObject +0槽(premise证伪,无scriptObject)/Player ctor签名(架构禁盲改交回)
metadata:
  type: project
---

Open#6 三子项,各自先 fresh decompile 取证再裁决。

子项A (已实装): D3DEmotePlayer_ncb_registerMembers @0x52E504。4 常量 ncb_addConstant(a1,...) @0x52e5a0-0x52e5e8:
MaskModeStencil=0 / MaskModeAlpha=1 / TimelinePlayFlagParallel=1 / TimelinePlayFlagDifference=2,全 flag 0x10000。
`a1` = D3DEmotePlayer NCB register ctx(sub_52E504 IS D3DEmotePlayer_ncb_registerMembers,被 0x541d98+sub_541EFC 共享调用)。
本地原放 D3DEmoteModule(main.cpp 旧 837-843)→移到 NCB_REGISTER_CLASS(D3DEmotePlayer)(main.cpp 现 ~876-887)。
additive-safe(标量 int,Variant() API 在 REGISTER_CLASS/SUBCLASS 块均可用)。旧 "M11 D-02 D3DEmoteModule sub_52E504" 注释误归属,已就地纠正。
web+wasmtime guest clean,m2logo 93f + yuzulogo 243f PASS bit-identical。oracle-inert=非回归守护。

子项B (PREMISE 证伪 — 无 scriptObject 槽,交回): brief 说 "binary EmoteObject +0 是 scriptObject,本地 +0 内联 RM,补 scriptObject 槽"。
反编译 EmoteObject_init @0x67DBAC + dtor EmoteObject_destroy @0x67F420 证明 EmoteObject 40B 三槽:
 +0 = RM (operator new(0xE8)=232B, ctor sub_6A88CC — 含 "new Math.RandomGenerator()" + RB-tree@+176 + SourceCache list;
       它就是传给 ResourceManager_loadResource(_, *a1, _) @0x67dce4 的 RM this;dtor sub_6A8B94+delete)
 +8 = EmoteEngine (new(0x5D8)=1496B, EmoteEngine_ctor;dtor sub_67F4B8+delete)
 +16 = vector<ttstr> (ttstrVector_assign_67F0CC;字符串 handle 逐元素 Release + delete buffer)
NO scriptObject slot。+0 就是 RM。**2026-07-13 纠正**：旧 variant-pointer 类型被 `0x52FDD4` producer + `0x67DBAC` consumer 证伪；本地现为 `_modulePaths`，并恢复多参数加载链。
"scriptObject" 是 brief 对 +0 的误读。无改动,交回。EmotePlayer/D3DEmotePlayer 均直接持 EmoteObject* _primaryObj(peer facade,无 D3DEmotePlayer 中间层)。

子项C (架构级 禁盲改,reframe P3 交回): brief 说本地 ctor 多 parentPlayer + RM native 非 dispatch,是字段顺序漂移根因。
反编译 Player_ctor(this, resourceManager_dispatch) @0x6CED30(Player=new 0x568=1384B, factory Player_factory@0x6f6dc0):
 (1) 签名单参 Player::Player(iTJSDispatch2* resourceManager) — 无 parentPlayer 形参。
 (2) RM 是 DISPATCH 指针(iTJSDispatch2*),非 native。sub_A0F5E0(this+636/+656/+992, resourceManager_dispatch)=把 dispatch 拷进 3+ 个 tTJSVariant 槽。RM 以 TJS dispatch ref 进入 Player。
本地 Player::Player(ResourceManager rm, Player *parentPlayer)(PlayerCore.cpp:90):多 parentPlayer 参 + _resourceManagerNative(native value)再 CreateAdaptor 反向包 dispatch。
属架构级:删 parentPlayer 破 _parentPlayer 链(PlayerVariable.cpp:268 walk player->_parentPlayer);native→dispatch-in 反转整个 RM ownership/lifetime,级联所有 _resourceManagerNative 消费者。非 additive。reframe P3,禁盲改。
EmotePlayer native instance(EmotePlayerNativeInstance_create @0x68629C)=24B{vtbl, EmoteEngine*懒置0@+8, sticky byte@+16},无 ctor 参,engine 懒建。
