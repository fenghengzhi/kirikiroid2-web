---
name: ncbind-invoker-error-codes
description: ncbind invoker 错误码模式库级裁决 (fstat/csvParser 抽样): membername→-1001 库级成立; 两种成员形态 (FuncCall override vs TJSCreateNativeClassMethod 4参回调) + 各形态检查顺序全表
type: project
---

# ncbind invoker 错误码模式 — 库级审计 (2026-06-12, fstat + csvParser 抽样)

裁决: **"首句 membername!=null → -1001" 是库级统一行为**, 非 textrender 特例。但实现位置分两种形态:

## 形态 A: FuncCall override (ncbind 模板 invoker, textrender/fstat 实例方法)
8 参 FuncCall(this,flag,membername,hint,result,numparams,params,objthis), 检查序:
1. membername!=0 → -1001  2. objthis==0 → -1008  3. result?→Clear(sub_A0F790)  4. numparams<N → -1004  5. 调 this+48 PMF; bool 返回 false→-1(TJS_E_FAIL)
- 实例: Storages_clearStorageCaches_invoker@0x5B6A94 (numparams<0 即0参模板)、getLastModifiedFileTime@0x5B6B1C、getFileAttributes@0x5B85FC
- PropGet@0x5B94BC: -1001 → getter==0→**-1007** → objthis==0→-1008 → Clear → bool→-1 (getter-null 检查在 objthis 之前!)
- PropSet@0x5B955C: -1001 → setter==0→-1007 → objthis==0→-1008 → param==0→-1
- fstat 成员对象 0x38(方法)/0x40(属性): {vtbl@0, tag@16=1/2, label@24=L"Function"/L"Property", item@32=off_19FE1F8, self@40, fn@48, setter@56}; 每成员独立 vtbl, invoker 单引用

## 形态 B: TJSCreateNativeClassMethod 4参回调 (csvParser 全部, fstat TJS_STATICMEMBER=flag 0x10000 成员)
membername/-1001 检查在**引擎共享入口** tTJSNativeClassMethod_FuncCall@0x9F52F8 (=tjsNative.cpp 原文): -1001 → objthis==0→-1008 → Clear → 调 cb(result,numparams,params,objthis)。
回调内部再做: objthis==0||GETINSTANCE(NativeInstanceSupport vtbl+200, 2, classID)失败 → -1008 (无 -1006/"Invalid instance type."); numparams 不足 → -1004
- 实例: CSVParser_init_invoker@0x5AABD0(numparams<1→-1004)、parse@0x5AAEA0(参数可缺省不报错)
- 属性走 TJSCreateNativeClassProperty@0x9F57BC → PropGet@0x9F56EC(-1001/-1008/result==0→-1, 无Clear)/PropSet@0x9F5754(-1001/-1008/param==0→-1)
- 只读属性 setter = 恒返 -1007 桩 (CSVParser_currentLineNumber_setter_deny@0x5AB17C)
- 构造器 tTJSNativeClassConstructor_FuncCall@0x9F541C: -1001 → Clear → 调回调, **无 objthis 检查**

## 注册站点
- csvParser: Regist 回调 CSVParser_ncb_registerMembers@0x5AA5BC (节点@0x1AB5248, static_init@0x42D0E0)
- fstat: 类节点@0x1AB5378 vtbl off_1A0D440={Regist=0x5B6464(原被 IDA 合并进 sub_5B63BC, 已拆), Unregist=0x5B6564}; StoragesFstat attach 到 Storages; 成员走查 StoragesFstat_registerMembers@0x5B2AD8; static_init@0x42D240 建 3 节点(类节点+REGIST/POST 回调节点 sub_5B4E4C/sub_5B50B4)
- 0x9F538C 旧名 ncb_createFuncWrapper 已纠正为 TJSCreateNativeClassMethod (引擎源名)

## 对本地 ncbind.hpp 的含义
本地"转发 BaseT::FuncCall"与二进制不矛盾: BaseT=tTJSNativeClassMethod, 其 FuncCall 正是 -1001/-1008/Clear 序; 二进制形态 A 是该基类逻辑被编译器内联进 override 的产物。
