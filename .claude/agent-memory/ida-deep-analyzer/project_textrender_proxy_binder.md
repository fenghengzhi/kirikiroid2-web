---
name: textrender-proxy-binder
description: textrender.dll 专用 proxy-object 绑定框架（区别于 ncbind ncb_addMember）全链路地址图 + 三相模块链/LoadModule 消费端 + invoker 错误码
type: project
---

# textrender proxy-binder 框架（libkrkr2.so）

## 模块注册链 = 与 ncbind 共用同一全局（已证实）
- `xmmword_1AB8920` 是 **3 个链表头数组**：[0]=PRE_REGIST（0x1AB8920）、[1]=REGIST（0x1AB8928）、[2]=POST（0x1AB8930）。证据：sub_548ACC(0/1/2) 按 index 读 `*((_QWORD*)&xmmword_1AB8920 + i)`。
- TextRender@0x42D01C、motionplayer_static_init@0x42EE18（BezierPatch/LayerMeshSupport/Layer/Motion 4 类）都链入 [1]；emoteplayer_static_init@0x42EB00 链入 [0]（PRE 回调节点，+24 存 emoteplayer_entry）。**同一条链，集成无需独立框架**。
- 节点布局 {vtbl@0, dllName@8, next@16, className@24}；vtbl[0]=Regist、vtbl[1]=Unregist（off_1A0B970 = {0x5A6650, 0x5A67B4}，旧 analysis 文档 "[+0]=0x5242A8" 已证伪并于 2026-06-12 就地修正）。
- 启动收集：sub_548924@0x548924（caller sub_907238）→ sub_548ACC(0,1,2) 把静态链灌进 map `qword_1AB8968`（小写模块名→3 phase 的 std::list），完后立刻 link "xp3filter.dll"。
- LoadModule 实体 = sub_704A08@0x704A08：查 already-loaded set qword_1AB8938 → 查 qword_1AB8968 → 依次跑 entry+40/+56/+72 三个 phase list，每个 descriptor 调 vtbl[0]，然后记入 loaded set。按名 link 入口 = sub_548A44@0x548A44（含 8EB23C 文件名提取），被 emoteplayer_entry、sub_907238 等调用。

## 关键引擎 API
- ncb_classInit @0x9F5858 = tTJSNativeClass ctor（87 callers，0xB0 对象）
- sub_9F4F18 @0x9F4F18 = TJSRegisterNativeClass(name)→classID（全局 vector qword_1AF7EA8 线性查/追加，ID=size-1）
- ncb_createFuncWrapper @0x9F538C（"finalize" 等用）
- ncb_registerMember @0x9F5B2C 区域：PropSetByVS(vtbl+80, flags|0xA00=MEMBERENSURE|IGNOREPROP) 失败 -1002 → 回退 PropSet(vtbl+48)；末尾 Release proxy
- sub_9F6D2C = tTJSDispatch ctor {vtbl off_1A30FA8, BeforeDestructionCalled@8=0, RefCount@12=1}
- iTJSDispatch2 vtbl: AddRef0/Release1/FuncCall2(+16)/PropGet4(+32)/PropSet6(+48)/PropSetByVS10(+80)/NativeInstanceSupport25(+200)

## TextRenderBase 专有
- classID dword_1AB5158 唯一写点 = buildClassObject@0x5A690C:0x5A69C0（亦写 classObj+152）；guard byte_1AB5148 "Already registerd class."
- CreateNativeInstance 机制：classObj+168(v2[21])=0x5A6A60，vtbl off_19FD6C8 slot +264 = sub_524094 thunk `(*(this+168))()`
- wrapper 24B {off_1A0B990, native@8, sticky@16}; vtbl = {Construct=0x5242A8(ret 0), Invalidate=0x5A6A94(!sticky 才 delete native), Destruct=0x5242B4(调 vtbl+32), D1=0x5A6AD8, D0=0x5A6B38}
- wrapper+8 填充点 = 构造器 FuncCall invoker sub_5A70C4：调 proxy+48 的 ctor Process(0x59D160: new 0x250+sub_5A111C) 后 GETINSTANCE 写入；失败则析构+delete 并 -1008。numparams==1 且 params[0].type==void → 直接 return 0
- proxy 成员对象：method 0x40 / property 0x50；tag@+16(1/2)；label@+24(L"Function"/L"Property")；inner-item@+32（vtbl off_19FE1F8: [0]ret this+8(=proxy 自身指针@+40), [1]调 dispatch vtbl+264=GetFlags, [2]调 +256=GetType, [3]nullsub）；成员函数指针对 getter/Process@+48/+56、setter@+64/+72（Itanium PMF：adj>>1 this 调整, adj&1 虚）
- 安装 helper sub_5A6E64(ctx{className,classObj,hasCtor@16}, name, item=proxy+32)：name 指针==className 时为构造器，hasCtor 已置→throw "Multiple constructors.(<cls>)"（sub_A183A4）；否则 ncb_registerMember(classObj, name, item->vtbl[0](), className, type, flags)
- invoker 错误码（tjsErrorDefs.h 核实）：membername!=0→-1001；objthis==0/GETINSTANCE 失败/native==0→-1008；numparams 不足→-1004；property 缺 getter/setter→-1007；PropSet 无值→-1
- invoker = 同一模板按签名实例化（~30 个 sub_5A7xxx..5A9xxx 全引用 classID；vtable 仅 FuncCall/PropGet/PropSet 槽不同）：void=0x5A76EC、dict=0x5A71E0（参数=tTJSVariant 值拷贝 CopyRef@sub_A0FB64 AddRef，调用后 A0F778 Release，非 NoAddRef 借用）、2-float=0x5A741C→0x5A74C8、RW-bool prop get/set=0x5A8638/0x5A875C、RW-float=0x5A8984/0x5A8AB4
