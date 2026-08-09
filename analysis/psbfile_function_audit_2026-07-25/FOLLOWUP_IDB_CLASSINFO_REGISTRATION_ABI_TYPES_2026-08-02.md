# PSBFile class-info / AutoRegister / RegistItem ABI 类型纠正

日期：`2026-08-02`。

## 范围与权威证据

本轮只修正 Android `libkrkr2.so` 活动 IDB 与审计材料，不修改生产 `cpp/`。主会话对
`0x597E98..0x597F38` 的 class-info/member-registration 簇和
`0x59A8D8..0x59B14B` 的 AutoRegister/RegistItem 尾链执行 fresh decompile，并复核
`0x59A8D8/0x59A968` 两个 caller、vtable xref、注册构造点与未消费返回寄存器。

二进制自身明确证明：

- class-info 状态依次是 `initialized@0`、`name@+8`、`id@+0x10`、
  `classObject@+0x18`；
- AutoRegister 实例从 `this+0x18` 读取 PSBFile class name；
- 优化后的 registrar field bundle 是
  `{className@0, classObject@+8, hasConstructor@+0x10}`，外层
  `{impl@0, isRegist@+8}`；
- adaptor 是 `{vptr@0, native PSBFile *@+8, sticky@+0x10}`；
- RegistItem 的 item-interface 四槽依次是
  `GetDispatch/GetFlags/GetType/Release`。

这些记录均由目标指令与数据流固定。本地模板只作最终交叉对照，不用于反向决定字段或
返回类型。

## IDB-only ARM64/O3 记录

| 记录 | 大小 | 用途 |
| --- | ---: | --- |
| `PSBFile_ncbClassInfoInfo_arm64` | `0x20` | class-info 四字段状态。 |
| `PSBFile_ncbAutoRegister_arm64` | `0x20` | vptr/module/next/className；当前入口读取 `+0x18`。 |
| `PSBFile_ncbRegistNativeClassState_arm64` | `0x18` | O3 后跨 helper 传递的 className/classObject/hasConstructor field bundle。 |
| `PSBFile_ncbRegistClassState_arm64` | `0x10` | delegate 指针与注册方向。 |
| `PSBFile_ncbInstanceAdaptor_arm64` | `0x18` | adaptor vptr/native/sticky 生命周期状态。 |
| `ncbIMethodObject_arm64` / vtable | `0x8` / `0x20` | RegistItem 的四槽 metadata interface。 |

`RegistNativeClassState` 特意命名为 `State`：它描述目标 O3 跨函数实际传递的字段束，不声称
是未优化 C++ 多态对象的完整 ABI 布局。所有 `padding` 也只用于 IDB 呈现字段偏移，不能
写入 wasm32 生产类硬凑 Android 对象尺寸。

## 一次性应用的 18 个函数类型

batch 结果为 `applied=18, failed=0`：

- class-info：`GetName/GetID/GetClassObject/IsSubClass/Set/InfoCtor` 与
  `registerMembers`（`0x597E98..0x597F38`）；
- AutoRegister：`Regist@0x59A8D8`、`Unregist@0x59A968`；
- registrar：`RegistBegin@0x59AA84`、`RegistEnd@0x59AD84`、
  `RegistItem@0x59AEEC`；
- adaptor：`CreateEmpty@0x59ABD8`、`Invalidate@0x59AC0C`、complete/deleting
  destructor `0x59AC7C/0x59AD08`；
- callbacks：finalize `0x59AC04` 与 dummy-constructor `0x59AEE4` 的完整四参数
  TJS callback ABI。

## 已清除的伪返回

fresh caller/data-flow 复核后，下列旧原型均改为 `void`：

1. `InfoCtor@0x597F24`：四次字段写后裸 `RET`，X0 只是未改写的 incoming self；C++
   构造器没有 source-level pointer return。
2. `registerMembers@0x597F38`：两个 retained caller 均不消费 X0，末次 RegistItem 的
   残值不是返回协议。
3. AutoRegister `Regist/Unregist@0x59A8D8/0x59A968`：正常尾部遗留 End/Release 的
   X0，vtable override 调用链不消费它。
4. `RegistBegin@0x59AA84` 与 `RegistItem@0x59AEEC`：caller 都不消费末次
   `ncb_registerMember/Release` 的 X0。

`RegistEnd@0x59AD84` 先前已经纠正为 `void`，本轮只把 self 从泛化整数收紧为明确状态
记录。

## fresh 复反编译结果

- class-info getter 现分别返回 `const tjs_char *`、32 位 `int`、class-object pointer 与
  `bool`；`Set` 显示 `name/id/classObject` 和 one-shot bool 返回；
- `InfoCtor` 直接显示四字段初始化且没有 return；
- member-registration body 直接显示 `registrar->impl/isRegist`、三只 wrapper 的建立与
  三次 `RegistItem`，没有伪 pointer return；
- AutoRegister `Regist` 直接显示 24 字节 delegate state、16 字节 registrar state 及
  `RegistBegin → registerMembers → RegistEnd`；Unregist 的 class-info Clear 也不再伪返回
  Release 结果；
- `RegistItem` 直接显示 constructor-name identity gate、`hasConstructor` 提交和
  `item->GetDispatch/GetType/GetFlags/Release`；
- adaptor 三个生命周期入口直接显示 `nativeInstance/sticky` 与 PSBFile owner intrusive
  refcount 清理；dummy callback 的返回现为 32 位 `-1002`，finalize callback 为 32 位
  `0`，两者均带完整四参数 ABI。

最后已 `idb_save` 到
`C:\Users\fenghengzhi\libkrkr2\libkrkr2\libkrkr2.so.i64`。本轮没有发现生产实现
差异，故审计统计保持 `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
