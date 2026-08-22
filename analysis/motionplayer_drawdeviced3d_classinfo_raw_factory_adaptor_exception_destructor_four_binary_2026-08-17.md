# motionplayer `global.DrawDeviceD3D` ClassInfo、raw Factory、adaptor、异常与析构身份（四参考二进制）

日期：2026-08-17  
阶段：V207

## 结论

`global.DrawDeviceD3D` 是一个完整、独立的 NCBind concrete class，不是 `D3D` 的别名或 native subclass。两类共享 C++ root base、对象大小、33 个 method/property 加 factory 的 34-entry 公开表面，以及正常 raw-factory 控制流，但以下 identity 全部分离：

- `ncbClassInfo<DrawDeviceD3D>` tuple 与一次初始化 guard；
- class ID；
- native-class descriptor 与 empty-adaptor allocator；
- raw factory descriptor vtable；
- concrete instance adaptor vtable；
- primary/secondary final vtable；
- deleting destructor。

Android 链接器只把两类字节完全相同的 complete destructor 折叠到同一地址；这不合并 concrete type。iOS 保留两套 complete/deleting destructor 地址。

四端 normal/boundary ABI 与 `D3D` 相同，但 V207 是独立审计，不是模板外推：

- raw descriptor 的 named-member、exact one-Void、result-preserve、business-first、attach-failure-delete 顺序一致；
- business factory 都在 allocation 前执行 `argc >= 2` gate，只转换 arg0、arg1，忽略 surplus，最后发布 out；
- factory 异常矩阵也一致：A64 phased cleanup，A32/I64 无 cleanup，I32 conversion raw-delete、ctor terminate/trap。

本轮还发现并纠正了 V206 报告的一处 ClassInfo 结构文字错误：tuple 首字段是 `initialized` byte，真实顺序为 `initialized/pad/name/classID/(LP64 pad)/classObject`；V206 recovery IDB 中应用的类型原本就是这个顺序，错误集中在报告结构体和四条 tuple 注释，现已同时改正。

## 参考范围与证据门槛

| 缩写 | 参考二进制 | ABI |
|---|---|---|
| A64 | `reference/binaries/Kirikiroid2_1.3.9_Android_arm64-v8a.so` | AArch64 LP64 / libstdc++ |
| A32 | `reference/binaries/Kirikiroid2_1.3.9_Android_armabi-v7a.so` | ARM EABI ILP32 / libstdc++ |
| I64 | `reference/binaries/Kirikiroid2_1.3.9_iOS_arm64` | AArch64 LP64 / libc++ |
| I32 | `reference/binaries/Kirikiroid2_1.3.9_iOS_armv7` | ARMv7 ILP32 / libc++ / SJLJ |

四库先全部只读闭合，随后按 A64 → A32 → I64 → I32 顺序逐库写 recovery IDB；任一时刻只开一个库，保存后关闭。没有把 V206 的 `D3D` 模板实例命名直接套到 `DrawDeviceD3D`。

检查链：

```text
global.DrawDeviceD3D static auto-register object
  -> Regist / Unregist wrappers
  -> RegistBegin / ClassInfo publication / empty-adaptor allocator
  -> 34-entry member registrar
     -> raw Factory descriptor -> business factory
  -> RegistEnd / global publication

ClassInfo classID xrefs
  -> all member wrappers
  -> raw Factory concrete-adaptor lookup
  -> native-class descriptor

concrete adaptor vtable
  -> CreateEmpty / invalidate / complete dtor / deleting dtor
  -> root primary virtual deleting destructor

business factory full boundary
  -> allocation / two conversions / root ctor / final-vtable write / out publish
  -> landing pads or SJLJ call-site matrix
```

## 一、ClassInfo 的精确 ABI

### 1.1 正确字段顺序

LP64：

```cpp
struct ncbClassInfo_DrawDeviceD3D_lp64_guess {
    uint8_t initialized;          // +0x00
    uint8_t pad0[7];
    const tjs_char *name;         // +0x08, borrowed
    int32_t classID;              // +0x10
    uint32_t pad1;
    iTJSDispatch2 *classObject;   // +0x18, borrowed
}; // 0x20
```

ILP32：

```cpp
struct ncbClassInfo_DrawDeviceD3D_ilp32_guess {
    uint8_t initialized;          // +0x00
    uint8_t pad0[3];
    const tjs_char *name;         // +0x04, borrowed
    int32_t classID;              // +0x08
    iTJSDispatch2 *classObject;   // +0x0C, borrowed
}; // 0x10
```

name 和 classObject 都是 borrowed raw pointer；Clear 不 Release。`initialized` 不是 guard：tuple 外另有一个一次静态初始化 guard。

### 1.2 四端 storage

| 目标 | tuple | name | classID | classObject | guard |
|---|---:|---:|---:|---:|---:|
| A64 | `0x1AAF5D0` | `0x1AAF5D8` | `0x1AAF5E0` | `0x1AAF5E8` | `0x1AAF5F0` |
| A32 | `0x110E1BC` | `0x110E1C0` | `0x110E1C4` | `0x110E1C8` | `0x110E1CC` |
| I64 | `0x101AEE3F0` | `0x101AEE3F8` | `0x101AEE400` | `0x101AEE408` | `0x101AEE410` |
| I32 | `0x1838E38` | `0x1838E3C` | `0x1838E40` | `0x1838E44` | `0x1838E48` |

它们都紧邻相应 D3D tuple，但不重叠：A64 D3D 从 `0x1AAF5F8` 开始，A32 从 `0x110E1D0`，I64 从 `0x101AEE418`，I32 从 `0x1838E4C`。

### 1.3 static init

| 目标 | static init |
|---|---:|
| A64 | `0x42CA28` |
| A32 | `0x2FEEE4` |
| I64 | `0x10024C950` |
| I32 | `0x24E54C` |

共同逻辑：

```cpp
if((guard & 1) == 0) {
    memset(&classInfo, 0, sizeof(classInfo));
    guard = 1;
}
```

LP64 guard 为 8 B，ILP32 guard 为 4 B；实际判断读取低 byte/bit 0。

### 1.4 叶函数

Android 两份保留独立叶函数：

| 操作 | A64 | A32 | 行为 |
|---|---:|---:|---|
| GetName | `0x52A578` | `0x492720` | 返回 borrowed name |
| GetID | `0x52A588` | `0x49272C` | 返回 classID |
| GetClassObject | `0x52A598` | `0x492738` | 返回 borrowed classObject |
| IsSubClass | 优化/内联或折叠 | `0x492744` | 恒 false |
| Set | `0x52A5B0` | `0x492748` | first-publication-wins |
| Clear | `0x52A5E8` | `0x492770` | 清完整 tuple |
| zero ctor | `0x52A604` | `0x492784` | 清传入 tuple object |

A32 原数据库未把 zero ctor 建成完整函数；本轮从精确字节边界 `0x492784..0x492790` 恢复五条 Thumb 指令，并恢复相邻 literal data，未改任何原始字节。I64/I32 将简单 ClassInfo 操作内联进 generated code。

### 1.5 Set / Clear / inheritance

Set 的精确顺序：

```cpp
if(initialized)
    return false;
name = arg0;
classID = arg1;
classObject = arg2;
initialized = true;
return true;
```

Clear 将 initialized、三个 payload slot 和 ABI padding 一起清零。

A32 的 IsSubClass 恒 false；其余端的 generated code 与独立 class-ID lookup 同样没有 parent chain。`DrawDeviceD3D` 和 `D3D` 的共同 C++ base 不形成 NCBind concrete inheritance。

## 二、registration / unregistration 生命周期

### 2.1 四端函数表

| 阶段 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| Regist wrapper | `0x534684` | `0x497E78` | `0x100236664` | `0x235360` |
| Unregist wrapper | `0x5347E8` | `0x497EFC` | `0x1002366CC` | `0x235414` |
| RegistBegin | `0x534940` | `0x497F78` | `0x100236728` | `0x2354C4` |
| begin cleanup | 内联/landing | — | `0x10023681C` | `0x2355E6` |
| CreateEmpty | `0x534A94` | `0x498060` | `0x100236840` | `0x235620` |
| finalize | `0x534AC0` | `0x498080` | `0x10023686C` | `0x235640` |
| adaptor invalidate | `0x534AC8` | `0x498084` | `0x100236874` | `0x235646` |
| adaptor complete dtor | `0x534B08` | `0x4980A0` | `0x1002368B4` | `0x235662` |
| adaptor deleting dtor | `0x534B64` | `0x4980DC` | `0x100236914` | `0x23569C` |
| regist/unreg wrapper | 优化折叠 | generated wrapper 内 | `0x100236960` | `0x2356C8` |
| dispatch branch | 优化折叠 | generated wrapper 内 | `0x100236988` | `0x235758` |
| RegistEnd | `0x534BB0` | `0x49810C` | `0x10023699C` | `0x235768` |
| RegistEnd cleanup | landing 内 | — | `0x100236A74` | `0x235862` |
| UnregistEnd | wrapper 内联 | `0x4981CC` | `0x100236A98` | `0x23589C` |
| RegistNCMIfNeeded | `0x534D18` generic item path | `0x49820C` | `0x100236B08` | `0x2358DC` |
| NotImplemented | `0x534D10` | `0x498248` | `0x100236B6C` | `0x235912` |

I64/I32 begin cleanup 只覆盖 registration descriptor/name 临时对象的构造失败，不是 script Factory 的 native root cleanup。

### 2.2 RegistBegin

共同顺序：

1. 从 class name 构造内部 string；
2. operator new native-class descriptor：LP64 `0xB0`，ILP32 `0x70`；
3. 构造 descriptor 并安装 DrawDeviceD3D-specific empty-adaptor allocator；
4. 取得新的 TJS native class ID；
5. 若 ClassInfo 已初始化，进入重复注册诊断；否则写 name/classID/classObject，最后置 initialized；
6. 把 classID 写回 native-class descriptor；
7. 注册 `finalize` NCM。

ClassInfo publication 发生在 34-entry member registrar 和 global publication 完成之前，因此仍是 prefix-visible、非事务式注册。

### 2.3 RegistEnd / UnregistEnd

RegistEnd 获取 global dispatch 并发布 `global.DrawDeviceD3D`。缺失 global 时只记录 `No Global Dispatch, Regist failed.`，不会 Clear 已发布的 ClassInfo，也不会回滚 member prefix。

UnregistEnd 在 global 可用时删除 public member，然后无条件清 tuple。generated Unregist 函数存在，但当前集成式 loader 仍无恢复到的正常 unload caller；不能把“有函数体”写成“进程运行中一定执行卸载”。

## 三、concrete instance adaptor

### 3.1 布局

| ABI | 大小 | 字段 |
|---|---:|---|
| LP64 | `0x18` | `vptr@0x00, native@0x08, sticky@0x10` |
| ILP32 | `0x0C` | `vptr@0x00, native@0x04, sticky@0x08` |

CreateEmpty 设置 DrawDeviceD3D-specific adaptor vptr、null native、false sticky。

### 3.2 删除规则

```cpp
if(native != nullptr && !sticky)
    native->primary_virtual_deleting_destructor();
native = nullptr;
sticky = false;
```

invalidate 和 complete adaptor destructor 都执行这条规则；deleting adaptor destructor随后释放 adaptor storage。

普通 DrawDeviceD3D root 的 concrete adaptor 保持 non-sticky，是 native 唯一 owner。root ctor 另行注册的 `D3DLayerBase` adaptor 为 sticky borrowed view。

### 3.3 与 D3D 的独立性

Raw wrapper 查询的 class ID：

| 目标 | DrawDeviceD3D classID slot | D3D classID slot |
|---|---:|---:|
| A64 | `0x1AAF5E0` | `0x1AAF608` |
| A32 | `0x110E1C4` | `0x110E1D8` |
| I64 | `0x101AEE400` | `0x101AEE428` |
| I32 | `0x1838E40` | `0x1838E54` |

一个 D3D empty shell 含有共同 sticky-root-view 的注册能力，但没有 DrawDeviceD3D concrete adaptor；反向也成立。两个方向的 executable test 都得到 `TJS_E_NATIVECLASSCRASH`，并触发 fresh root 的 class-specific deleting destructor rollback。

四端没有找到 DrawDeviceD3D existing-native/CreateAdaptor producer；公开创建路径是 native class 先创建 empty adaptor，再由 raw Factory 填 native。

## 四、raw Factory descriptor

### 4.1 映射

| 阶段 | A64 | A32 | I64 | I32 |
|---|---:|---:|---:|---:|
| member registrar | `0x52A618` | `0x492790` | `0x10023070C` | `0x22F622` |
| Factory register helper | 内联 | `0x492BD4` | `0x100230C34` | `0x22FAFE` |
| business factory | `0x52B654` | `0x492BFC` | `0x100230C88` | `0x22FB28` |
| descriptor Create | registrar 内联 | `0x498252` | `0x100236B74` | `0x23591C` |
| descriptor ctor | registrar 内联 | `0x49836C` | `0x100236D18` | `0x235B44` |
| descriptor FuncCall | `0x534F78` | `0x4983CC` | `0x100236D98` | `0x235C38` |

这是 `ncbNativeClassFactory<DrawDeviceD3D>` raw descriptor，不是 typed constructor。

### 4.2 wrapper 精确状态机

```text
membername != null
  -> MEMBERNOTFOUND (-1001), result 保持

argc == 1 && argv[0].Type == Void
  -> success, 不调 business factory, 不检查 receiver, result 保持

native = null
hr = businessFactory(&native, argc, argv, objthis)
hr != 0
  -> 原样返回, result 保持

从 objthis 按 DrawDeviceD3D class ID 查询 existing concrete adaptor
  -> 成功且 adaptor 非空: adaptor.native = native, success, result 保持
  -> 失败: 若 native 非空则 primary virtual delete；返回 -1008；result 保持
```

重入 populated adaptor 只覆写 native，不先删除旧 root，保留参考泄漏边界。

### 4.3 参数与 receiver

- zero args、one non-Void：business factory 在 allocation 前返回 `-1004`；
- exact one Void：descriptor 私有 sentinel，留下 null-native shell；
- two Void：ordinary path，两项按 integer 0 转换；
- three+ args：只读前两项；
- null receiver + ordinary two args：root ctor 的严格 `D3DLayerBase` 注册发生在 post-factory concrete lookup 前，不是安全的普通 `-1008` 路径；
- wrong concrete shell：root ctor 可注册 shared sticky view，随后 class-specific lookup 失败、fresh root 被删除，shell 上留下 dangling borrowed view。

## 五、business factory 正常数据流

| 目标 | factory | object size |
|---|---:|---:|
| A64 | `0x52B654` | `0x200` |
| A32 | `0x492BFC` | `0x13C` |
| I64 | `0x100230C88` | `0x1A0` |
| I32 | `0x22FB28` | `0x10C` |

共同正常顺序：

1. `argc < 2` -> `TJS_E_BADPARAMCOUNT`；
2. allocate complete object；
3. `arg0.AsInteger()`；
4. `arg1.AsInteger()`；
5. common root ctor `(objthis,width,height)`；
6. secondary `tTVPDrawDevice` 构造及 concrete tail；
7. 安装 DrawDeviceD3D primary/secondary final vtable；
8. 写 `*out`，返回 0。

`objthis` 存为 borrowed ScriptOwner，不 AddRef。surplus 未读。只有第 8 步后 raw wrapper 才可能 attach concrete adaptor。

## 六、异常清理矩阵

### 6.1 四端结果

| 目标 | conversion escape | root ctor escape | 产物证据 |
|---|---|---|---|
| A64 | raw-delete `0x200` allocation | primary root base 已完成时先析构 base 再 delete；更早 phase 只 delete | `0x52B7C8` / `0x52B7D8` 两 landing pad |
| A32 | 泄漏 `0x13C` allocation | 泄漏 | 完整 factory 仅 `0x4C`，无 landing pad/EH cleanup |
| I64 | 泄漏 `0x1A0` allocation | 泄漏 | 完整 factory 仅 `0x80`，无 landing pad/EH cleanup |
| I32 | call-site 1/2 raw-delete `0x10C` 后 resume | call-site 3 进入 terminate/trap | SJLJ landing `0x22FBE8`，table `02 02 02 00` |

### 6.2 A64 phase

- `0x52B7C8` 保存异常对象，调用 common root-base destructor，随后汇入 raw delete/resume；
- `0x52B7D8` 直接汇入 raw delete/resume。

out pointer 尚未发布；descriptor 也尚未收到成功返回。

### 6.3 A32 / I64

从 allocation 到两个 AsInteger 和 ctor call 的完整函数范围内没有 landing pad；函数结束紧随 normal epilogue。若这些调用逃逸 C++ exception，raw allocation 不由 factory 回收。

### 6.4 I32 SJLJ

factory 明确在三次风险调用前写：

```text
arg0 AsInteger -> call_site = 1
arg1 AsInteger -> call_site = 2
root ctor      -> call_site = 3
```

landing 对 0..3 做 TBB；原始 table bytes 为 `02 02 02 00`。0/1/2 汇入 `operator delete + SjLj_Resume`；3 回到 table/termination 形态，Hex-Rays 表达为 abort。它不是“ctor 先析构已构造 base 再 resume”。

### 6.5 与 attach failure 的区别

business factory 完整成功后，raw wrapper 的 concrete-adaptor lookup 失败仍会调用 DrawDeviceD3D deleting destructor，完整销毁 root 并释放 storage。这个显式 rollback 不能补救 factory 尚未返回的 conversion/ctor exception。

## 七、complete/deleting destructor identity

| 目标 | class | primary vtable | complete dtor | deleting dtor |
|---|---|---:|---:|---:|
| A64 | DrawDeviceD3D | `0x19FA908` | `0x531410` | `0x531438` |
| A64 | D3D | `0x19FACB8` | `0x531410` | `0x533370` |
| A32 | DrawDeviceD3D | `0x10AAEA0` | `0x495744` | `0x49575C` |
| A32 | D3D | `0x10AB078` | `0x495744` | `0x496DC8` |
| I64 | DrawDeviceD3D | `0x101AEE568` | `0x100233F54` | `0x100233F7C` |
| I64 | D3D | `0x101AEE9A8` | `0x10023590C` | `0x100235934` |
| I32 | DrawDeviceD3D | `0x1838EF4` | `0x232C74` | `0x232C8C` |
| I32 | D3D | `0x1839110` | `0x234714` | `0x23472C` |

Android 的 complete address 相同是 identical-code folding。A64/A32 recovery IDB 继续用 shared complete 名；DrawDeviceD3D deleting destructor 保持 concrete-specific 名。iOS 两套都独立命名。

complete chain 共同顺序：

```text
concrete complete dtor
  -> tTVPDrawDevice secondary base dtor
  -> common primary root-base dtor
```

deleting dtor 再 operator delete。normal adaptor ownership 与 wrapper attach-failure 都通过这个 primary deleting slot。

## 八、源码与测试同步

### 8.1 源码

`cpp/plugins/DrawDeviceD3D.cpp` 没有改变执行逻辑，只补充：

- DrawDeviceD3D 独立 ClassInfo/guard/classID/descriptor/concrete adaptor；
- concrete adaptor 是 non-sticky owner，D3DLayerBase 是 sticky borrower；
- 四端 business-factory exception matrix；
- registration prefix publication/global-miss no rollback；
- Android complete-dtor folding 与 iOS distinct bodies。

源码注释不含参考绝对地址。

### 8.2 回归

`tests/unit-tests/plugins/motionplayer-dll.cpp` 在现有 D3D raw-factory 回归旁新增 DrawDeviceD3D 对称覆盖：

1. `TJS_IGNOREPROP` 取得 `DrawDeviceD3D` raw descriptor；
2. nested member `-1001` + result preserve；
3. exact one-Void + null receiver 成功 + result preserve；
4. zero 与 one non-Void `-1004` + result preserve；
5. 用 D3D empty shell 作为 wrong concrete receiver，得到 `-1008` 并保持 result；
6. 正确 DrawDeviceD3D empty shell 在填充前访问 typed property 得到 `-1008` 并 clear typed result；
7. 三参数 direct raw call 忽略 surplus、成功填 adaptor；
8. 填充后读回 320/240。

两方向 wrong-shell 测试一起证明共享 root view 不等于共享 concrete identity。

## 九、V206 ClassInfo 文字纠错

V206 recovery IDB 中 `ncbClassInfo_D3D_*_guess` 的 applied type 已是：

```text
initialized -> name -> classID -> classObject
```

但 V206 报告最初把示意结构写成 name-first，I32 tuple 注释也把四个词序列写错。本轮处理：

- 修正 V206 报告结构体、偏移和 Set signature；
- 用 `set_comments` 覆盖四份 D3D tuple 注释，明确 LP64 `0/8/10/18` 与 ILP32 `0/4/8/C`；
- 保持原本正确的 D3D local type，不做无意义重建；
- V207 新建的 DrawDeviceD3D local type从一开始使用正确字段顺序。

这体现了本项目的证据规则：即使结论刚写完，只要下一纵切面发现字段语义不够准确，也必须回修，而不是保留自洽但错误的历史叙事。

## 十、recovery IDB 写回

| IDB | 新 layout type | rename | 新 append comment | 覆盖纠错 comment | bookmark |
|---|---:|---:|---:|---:|---:|
| A64 | 2 | 16 | 16 | 1 | 4 |
| A32 | 2 | 18 | 15 | 1 | 4 |
| I64 | 2 | 16 | 13 | 1 | 4 |
| I32 | 2 | 17 | 14 | 1 | 4 |
| 合计 | 8 | 67 | 58 | 4 | 16 |

另有 45 项 tuple/guard/function type application。A32 额外恢复 zero ctor 的 Thumb 函数边界，并为原本未稳定呈现的 IsSubClass/finalize 叶函数建立语义名。

四份 recovery IDB 已原位保存：

- `out/ida-recovery/android-arm64/Kirikiroid2_1.3.9_Android_arm64-v8a.recovery.i64`；
- `out/ida-recovery/android-armv7/Kirikiroid2_1.3.9_Android_armabi-v7a.recovery.i64`；
- `out/ida-recovery/ios-arm64/Kirikiroid2_1.3.9_iOS_arm64.recovery.i64`；
- `out/ida-recovery/ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.recovery.i64`。

保存后 IDA session count 为 0。

## 十一、被否定的假设

| 假设 | 四参考事实 |
|---|---|
| DrawDeviceD3D 与 D3D 共用 ClassInfo | 两套 tuple、guard、classID 全部独立。 |
| ClassInfo 首字段是 name pointer | 首字段是 initialized byte；name 在第二个对齐槽。 |
| 共享 C++ root base 意味着 NCBind subclass | IsSubClass false；concrete lookup 只认各自 class ID。 |
| 同一 complete-dtor 地址证明同一 concrete type | Android ICF；deleting dtor/vtable/ClassInfo/adaptor 独立，iOS complete 也独立。 |
| one-Void 构造零尺寸 root | 只创建 null-native empty concrete shell。 |
| raw Factory 会清 result | 所有路径保持 result。 |
| ordinary Factory 先验证 receiver | 先构造 root，再查 concrete adaptor。 |
| wrong shell 不会接受共同 root view | root ctor 可注册 sticky D3DLayerBase；随后仍因 concrete class ID 不符而回滚。 |
| 两类 factory 的异常边界可由一个模板实例推出 | 结果相同，但 V207 分别验证八个实例/边界后才确认。 |
| 四端 factory exception 都自动清理 | A32/I64 leak；I32 ctor terminate/trap；仅 A64 phased cleanup。 |

## 十二、验证状态

四库逆向、IDB 保存、源码注释、测试、报告和工程验证均已完成：

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套测试 TU syntax-only 均通过；只有既有 `_tss` deprecated warning；
- Web/Wasmtime 两套最终链接通过；并行 dot-source Emscripten 环境时出现一次 SDK 临时 env 文件占用提示，但命令继续成功并产生完整最终产物，不是编译/链接失败；
- 两份 Wasm 均通过 `WebAssembly.validate` 和 `WebAssembly.Module` parse；
- Web 为 539 imports / 69 exports，Wasmtime 为 538 imports / 69 exports；
- 两个 CTest 命令都以 0 退出，并报告当前未登记 CTest test；
- `git diff --check` 以 0 退出；输出只有工作树既有 LF→CRLF 提示。

产物：

| 产物 | 大小 | SHA-256 |
|---|---:|---|
| Web `index.wasm` | 85,661,395 B | `45920E7ACCDFC78C173BB877E443E9D94A4284BCEB0F013515DC6E92D54CB219` |
| Wasmtime `index.wasm` | 85,008,536 B | `39984FEE7AB8FD687F4F53066A3E920BB3F29E0892304B55666D9871AB7D5B43` |

| section | Web | Wasmtime |
|---|---:|---:|
| FUNCTION | `0x1BD2F` | `0x1BA4E` |
| GLOBAL | `0xD5B2` | `0xD5DA` |
| CODE | `0x1A427D0` | `0x19EA77E` |
| DATA | `0x5A4017` | `0x5A1267` |
| name custom | `0x3185E3C` | `0x3141CD2` |

大小、哈希、section、imports/exports 相对 V206/V205 精确零变化，符合执行源码只变更注释、新断言位于未链接测试 TU 的预期。

## 十三、未闭合边界

- 两个 root concrete class 的注册、factory、adaptor、异常和析构身份现已闭合；common root 内仍有 `_guess` dormant slots，需要后续独立纵切面；
- 参考异常矩阵是产物事实，原始源码使用的具体 EH/编译开关仍不能从这几个函数唯一反推；
- generated Unregist 仍无 loader caller；若后续找到 unload 入口，必须重审 global/ClassInfo 的实际终止时序；
- wrong-shell attach failure 会在 receiver 留下指向已删除 root 的 sticky borrowed view；更多脚本可达 UAF 组合留待对象交互纵切面；
- V207 不是总目标完成点。
