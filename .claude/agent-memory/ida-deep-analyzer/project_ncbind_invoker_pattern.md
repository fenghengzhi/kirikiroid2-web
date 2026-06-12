---
name: ncbind-invoker-pattern
description: ncbind invoker 库级统一形态已证实——"首句 membername→-1001" 是本地 `BaseT::FuncCall` 转发的内联产物，本地 ncbind.hpp 与二进制完全一致，无源码偏离
type: project
---

# ncbind invoker 库级模式 (motionplayer 抽样审计 2026-06-12)

**结论：membername→-1001 在 motionplayer (Player/EmotePlayer) 全部成立，且它不是偏差**。
本地 `if (membername) return BaseT::FuncCall(...)` 中 BaseT = tTJSDispatch，其默认实现
(tjsObject.h:114) = `membername ? TJS_E_MEMBERNOTFOUND(-1001) : TJS_E_NOTIMPL(-1002)`，
二进制中为独立函数 0x534D94(FuncCall)/0x534C8C(PropGet)/0x534C9C(PropSet)，被编译器内联进
每个 invoker 首句。textrender 的 sub_5A71E0 系列同模式 ≠ textrender 特例。

## dispatch 对象布局（注册函数内 operator new）
+0 主 vtable(按签名特化) | +16 type(1=Method/2=Property) | +24 L"Function"/L"Property" |
+32 ncbIMethodObject 子对象{vtable off_19FE1F8, +8 回指 self} ← addMember 注册的是这个 |
+48.. _method PMF / _callback / getter+setter PMF | RawCallback 形态 +56 = _flag (DWORD)。
ncbRegistClass::RegistItem = 0x6F6970(Player)/0x68C664(EmotePlayer)，从子对象 vt 取
GetDispatch(+0)/GetFlags(+8)/GetType(+16)/Release(+24) 再调 ncb_registerMember。
主 vtable: slot2=FuncCall, slot4=PropGet, slot6=PropSet（AddRef/Release 在 slot0/1）。

## 四形态错误码顺序（全部 byte-verified）
- ncbNativeClassMethod (typed): membername→-1001; !objthis→-1008; result->Clear();
  numparams<N→-1004; GETINSTANCE(NIS=2,classID)失败/inst null→-1008; 调 PMF;
  内层 bool→ `(~r<<31)>>31` = TJS_S_OK : TJS_E_FAIL(-1)。例 0x6F91B0(setFlip)/0x6FA0D4(clear+onAction 共享)/0x6F9D98(getLayerNames+getLayerGetter 共享)。
- ncbRawCallbackMethod: membername→-1001; !objthis→-1008; Clear;
  _flag&TJS_STATICMEMBER(byte+58&1)? inst=0 : GETINSTANCE→-1008; callback(result,n,param,inst)
  **无 numparams 检查**。例 Player.setVariable 0x6F9620 / EmotePlayer.setVariable 0x68E668（仅 classID 不同：1AB8848 vs 1AB8070）。
- PropGet: membername→-1001; getter null→**-1007 ACCESSDENYED**; !objthis→-1008; Clear;
  GETINSTANCE→-1008; 调 getter; 写 result; 0。例 0x6F8714 (double 系)。
- PropSet: membername→-1001; setter null→-1007; !objthis→-1008; **!param→-1 (TJS_E_FAIL)**;
  GETINSTANCE→-1008; 变体转换 switch(vt); 调 setter; 0。例 0x6F8844。
- ctor (ncbNativeClassFactory): membername→-1001; numparams==1&&vt==void→return 0;
  Clear; numparams<1→-1004; createInstance。例 Player_ncb_ctorDispatch 0x6F6BD0。

## 其他
- flag 参数(a2) 在所有 invoker 中从不检查——无 TJS_STATICMEMBER 对 membername!=null 的特殊分支。
- -1008 = TJS_E_NATIVECLASSCRASH（objthis null 与 GETINSTANCE 失败同码）；-1004=BADPARAMCOUNT。
- 同签名成员共享 vtable+invoker（clear/onAction; x/y/speed 等 double 属性共用 0x6F8714/0x6F8844）。
- Player 注册表成员→vtable 全表已在本次会话提取（92 成员注册函数 0x6D69C8）。
