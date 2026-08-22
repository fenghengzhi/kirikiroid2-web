# Motion.SourceCache：ClassInfo、注册事务与 adaptor teardown 四参考复原（2026-08-17）

## 1. 范围与结论

本轮以四个 `reference/binaries/` 当前参考为唯一事实源，补齐旧 SourceCache NCB surface
报告未分离的模板层：

- 独立 `ncbClassInfo<SourceCache>::InfoT`、静态 guard 和初始化顺序；
- Motion wrapper、`ncbSubClassItem::Setup`、`RegistBegin/RegistEnd`；
- ClassInfo Set/Clear/Get/IsSubClass leaves；
- zero-argument typed constructor 的 value-init、metadata attach 与失败回收；
- `CreateEmpty`、finalize、Invalidate、完整/删除析构与 sticky owner gate；
- public `clearCache` 与 native/adaptor teardown 的可观察差异。

四端共同证明 SourceCache 是一个独立 delayed subclass：它不复用 ObjSource、Player 或
ResourceManager 的 ClassInfo。InfoT 只保存 borrowed name/classObject；注册时发布，卸载时清空，
没有 AddRef/Release、同步或 rollback。Android arm64 把 Motion wrapper 内联进根 registrar，
`0x6FB504` 是 Setup，不是 wrapper。

旧报告标为 “adaptor create” 的小函数实际是 NCB native-class factory 使用的 `CreateEmpty`：
它只分配空 adaptor shell。当前四参考中没有 SourceCache native producer 调用公开
`ncbInstanceAdaptor<SourceCache>::CreateAdaptor(native)`；脚本 native 来源是 zero-argument
constructor 的 allocate/attach。ResourceManager 的 SourceCache base subobject则由
ResourceManager constructor 直接构造并归 ResourceManager 对象所有。

本轮 portable 只补两处无地址注释；旧报告已经恢复的 zero-argument constructor、对象布局、
list ABI 和 teardown 行为不需修改。

## 2. 对旧报告的命名纠偏

`analysis/motionplayer_source_cache_ncb_surface_constructor_four_binary_2026-08-14.md`
关于下列行为继续有效：

- script surface 为 zero-argument constructor、`loadSource`、`clearCache`、RO `bufLayer`；
- `SourceCache(owner,cacheSize)` 只用于 ResourceManager base construction；
- direct constructor value-initialize Variant/计数/list；
- attach failure 完整销毁 native；
- non-sticky adaptor teardown 直接析构 list/Variant，不调用 public `clearCache`；
- Android 与 iOS 分别使用旧 libstdc++/libc++ `std::list` ABI。

本轮纠正三类层级标签：

1. Android arm64 `0x6FB504` 是 `ncbSubClassItem::Setup`；Motion wrapper 在根 registrar 内联；
2. 四端原 “adaptor create” 是 `CreateEmpty`，不是一个“把已有 native 包成 script object”的
   `CreateAdaptor` producer；
3. Android armv7 `0x5B6BEC` 是多个 ABI 入口被 IDA 合并的 Thumb cluster：入口分别对应
   Invalidate thunk、complete destructor、deleting destructor 和 shared native destroy。

## 3. ClassInfo ABI 与静态初始化

### 3.1 布局

LP64：

```text
+0x00  bool initialized
+0x01  padding[7]
+0x08  const tjs_char *name       // borrowed
+0x10  int32 classID
+0x14  padding[4]
+0x18  iTJSDispatch2 *classObject // borrowed
sizeof InfoT = 0x20
guard        = 0x08
```

ILP32：

```text
+0x00  bool initialized
+0x01  padding[3]
+0x04  const tjs_char *name       // borrowed
+0x08  int32 classID
+0x0c  iTJSDispatch2 *classObject // borrowed
sizeof InfoT = 0x10
guard        = 0x04
```

### 3.2 四端映射

| 目标 | InfoT | guard | static init |
|---|---:|---:|---:|
| Android arm64 | `0x1AB57C0` | `0x1AB57E0` | `0x42F0E4` |
| Android armv7 | `0x1111B0C` | `0x1111B1C` | `0x3015CC` |
| iOS arm64 | `0x101ADF6B0` | `0x101ADF6D0` | `0x10014FB30` |
| iOS armv7 | `0x18317F4` | `0x1831804` | `0x151B68` |

恢复名：

```text
g_ncbClassInfo_SourceCache_guess
g_ncbClassInfo_SourceCache_guard_guess
ncbClassInfo_SourceCache_staticInit_guess
```

共同伪代码：

```cpp
if ((guard & 1) == 0) {
    info.initialized = false;
    info.name = nullptr;
    info.id = 0;
    info.classObject = nullptr;
    guard = 1;
}
```

四端按 initialized/name/id/classObject/guard 读取的 20 个逻辑初值全部为零。guard 是普通
load/test/store，没有 mutex、atomic、TLS 或异常恢复。身份由 direct constructor 的 class-ID
lookup、Setup/RegistBegin 和 static-init xref 共同确认；Android BSS 邻接只作辅助，不用于跨
链接器推导。

### 3.3 Android ClassInfo leaves

| helper | Android arm64 | Android armv7 |
|---|---:|---:|
| GetName | `0x6A58E8` | `0x57B06C` |
| GetID | `0x6A58F8` | `0x57B078` |
| GetClassObject | `0x6A5908` | `0x57B084` |
| IsSubClass | `0x6A5918` | `0x57B090` |
| Set | `0x6A5920` | `0x57B094` |
| Clear | `0x6A5958` | `0x57B0BC` |
| InfoT constructor | `0x6A5974` | `0x57B0D0` |

`IsSubClass` 恒返回 true，表示该 ClassInfo 走 delayed-subclass 路径；它不是一个 SourceCache
C++ base pointer，也不建立 ResourceManager parent relationship。Set 在 initialized 已真时返回
false；成功时按 name/id/classObject/initialized 发布。Clear 把四字段置零但不 Release dispatch。
iOS 将这些 leaves 内联，字段行为相同。

## 4. 注册调用链

### 4.1 地址映射

| 层次 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| Motion wrapper | 根 registrar 内联 | `0x599738` | `0x100126064` | `0x12514C` |
| `ncbSubClassItem::Setup` | `0x6FB504` | `0x5B6A20` | `0x10014DF88` | `0x14FC78` |
| `RegistBegin` | `0x6FB668` | `0x5B6AE0` | `0x10014E020` | `0x14FD6C` |
| `RegistEnd` | Setup 内联 | `0x5B6AB4` | `0x10014E240` | `0x14FF78` |
| dummy-constructor helper | 内联 | `0x5B6C74` | `0x10014E290` | `0x150024` |
| member registrar | `0x6A5988` | `0x57B0DC` | `0x100100F90` | `0xFE12A` |
| subclass-item GetClassObject | `0x6FB9C4` | `0x5B6CBC` | `0x10014E2FC` | `0x150064` |

恢复名按模板层统一为 `Motion_registerSubclass_SourceCache_guess`、
`ncbSubClassItem_SourceCache_Setup_guess`、
`ncbRegistSubClass_SourceCache_RegistBegin/RegistEnd_guess`、
`NCB_registerMembers_SourceCache_guess`。AArch64 不创建虚假的独立 wrapper 符号，只在根
registrar call site 记录内联身份。

### 4.2 wrapper 与 Setup

有独立 wrapper 的三端：

```cpp
if (!SourceCacheSetup(name, isRegist))
    throwSubclassRegistrationFailed();
if (isRegist)
    publishHeapSubclassItem();
```

Setup：

```cpp
if (isRegist && info.classObject != nullptr)
    return false;

RegistrationContext ctx(name, isRegist);
if (isRegist)
    RegistBegin(ctx);
registerSourceCacheMembers(ctx);
RegistEnd(ctx);
return !isRegist || info.classObject != nullptr;
```

重复注册 gate 看 classObject，不看 initialized。SourceCache 是 Motion registrar 的第七个
in-flow row，位于 Player 后、ObjSource 前；它在源码中较早声明并不改变最终 row 顺序。

### 4.3 RegistBegin 与部分提交

共同顺序：

1. 创建 TJS native class：LP64 编译形态 `0xB0` B，ILP32 `0x70` B；
2. 安装 SourceCache `CreateEmpty` factory；
3. 获取 class ID；
4. initialized 已真时抛 already-registered；
5. 发布 borrowed name、class ID、borrowed classObject、initialized=true；
6. 把 class ID 写回 native class；
7. 注册 finalize stub；
8. Setup 注册 zero-argument constructor、loadSource、clearCache、RO bufLayer。

第 5 步先于 SetClassID/finalize/member registration，后续异常没有 Clear/rollback，因此可能留下
已发布但不完整的 class。InfoT 不持有 class dispatch 引用。

### 4.4 RegistEnd/卸载

register 模式仅在 member registrar 没有提供 constructor 时安装 dummy；SourceCache 正常已经
注册真实 zero-argument constructor。unregister 模式清零 InfoT，不 Release，不重置 static
guard。Setup、Create/attach 和卸载之间没有同步，混合代际 classObject/classID 读取属于 data
race。

## 5. direct constructor 与 native owner

### 5.1 地址与 allocation

| 目标 | allocate/attach | native size |
|---|---:|---:|
| Android arm64 | `0x6E8430` | `0x58` |
| Android armv7 | `0x5A6A44` | `0x34` |
| iOS arm64 | `0x100138E90` | `0x60` |
| iOS armv7 | `0x138FC0` | `0x38` |

恢复名为 `SourceCache_ncbConstructor_allocateAttach_guess`。

共同状态机：

1. `new SourceCache()` value-initialize整个对象；
2. 三个 Variant 为 Void，两个计数为零，list 为空；
3. 用 SourceCache ClassInfo.id 查询 objthis 的 adaptor metadata；
4. 成功且 adaptor 非空时写入 native pointer；
5. 失败时按 list → bufLayer → primaryLayer → owner 顺序析构，释放 storage，返回 `-1008`。

这条 direct constructor 是当前四参考 SourceCache script native 的唯一 publication path；没有
另一个 native producer 需要 `CreateAdaptor` owner-transfer/失败矩阵。一个 Void 参数的 empty-
shell sentinel 和 surplus 参数忽略边界继续沿用旧报告。

## 6. adaptor ABI 与 teardown

### 6.1 布局

| field | LP64 | ILP32 |
|---|---:|---:|
| vptr | `+0x00` | `+0x00` |
| SourceCache *native | `+0x08` | `+0x04` |
| bool sticky | `+0x10` | `+0x08` |
| total | `0x18` | `0x0c` |

`CreateEmpty` 分配空 shell，写 `native=null, sticky=false`。finalize method 是返回 0 的空 stub，
不取得或释放 native owner。

### 6.2 四端入口

| 入口 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| CreateEmpty | `0x6FB7BC` | `0x5B6BC8` | `0x10014E138` | `0x14FEC8` |
| finalize stub | `0x6FB7E8` | `0x5B6BE8` | `0x10014E164` | `0x14FEE8` |
| Invalidate/thunk | `0x6FB7F0` | `0x5B6BEC` | `0x10014E16C` | `0x14FEEC` |
| complete destructor | `0x6FB8A0` | `0x5B6BF0` | `0x10014E170` | `0x14FEF0` |
| deleting destructor | `0x6FB900` | `0x5B6C18` | `0x10014E1B4` | `0x14FF1C` |
| shared native destroy | Invalidate 内联 | `0x5B6C3C` | `0x10014E1E8` | `0x14FF40` |

Android armv7 的四个地址处于 IDA 合并的 `0x5B6BEC` Thumb function cluster；本轮不做
destructive split，而在 cluster 名和注释中保留多入口 ABI。其他目标按现有已确认函数边界命名。

### 6.3 owner gate 与析构顺序

共同伪代码：

```cpp
if (adaptor->native != nullptr && !adaptor->sticky) {
    adaptor->native->_entries.~list();
    adaptor->native->_bufLayer.~Variant();
    adaptor->native->_primaryLayer.~Variant();
    adaptor->native->_owner.~Variant();
    operator delete(adaptor->native);
}
adaptor->native = nullptr;
adaptor->sticky = false;
```

public `clearCache()` 会向 cached Layer 发送脚本 `Invalidate` 后删除 entries；native/adaptor
teardown 只运行 list/Variant destructor，不复用该 callback。因此缓存 Layer 在对象析构时不会
收到 public clearCache 的脚本 side effect。sticky=true 时 native 留给外部 owner，但 adaptor
槽仍清零；重复 Invalidate 因此幂等。

## 7. 与 ResourceManager 的边界

ResourceManager C++ 继承 SourceCache，但它有自己的 NCB class、constructor 和 adaptor。
ResourceManager native constructor 直接调用 `SourceCache(owner,cacheSize)` 构造 base subobject；
没有创建 SourceCache script shell，也没有把 base pointer挂进 SourceCache adaptor。析构时先销毁
ResourceManager derived state，再进入相同 SourceCache base teardown。

ClassInfo `IsSubClass=true` 只描述 SourceCache 的 delayed subclass registration，不隐式建立一个
ResourceManager→SourceCache 的 script ClassInfo parent 链。后者应在 ResourceManager 自身纵切面
由其 registration/constructor xref 独立恢复。

## 8. recovery IDB 写回

四库完成：

- 8 个 typed data items：4 组 SourceCache InfoT + guard；
- 67 个 function entry semantic names，并全部 lookup readback；
- 77 个成功 data/function/ABI-cluster comments；
- 4 个 SourceCache ClassInfo/adaptor bookmarks；
- 67 个 targeted force-recompile，全数成功；
- 20 个 logical zero-field reads；
- 四个 recovery IDB 原位保存。

Android armv7 cluster 内两个非 function-head entry 的 decompiler comment API 拒绝单独挂载；
完整/删除析构入口地址已经集中写入 function-head cluster 注释和本报告，不把这两个 API 限制
伪装成二进制未知。

## 9. portable 对齐

仅补注释：

- `cpp/plugins/motionplayer/main.cpp`：SourceCache 有独立 delayed ClassInfo tuple，Setup publish、
  unload Clear，均 non-owning；
- `cpp/plugins/motionplayer/SourceCache.h`：zero-argument native 成功 attach 后由 non-sticky adaptor
  拥有，teardown 不调用 public clearCache。

没有改变 constructor、cache list、Variant ownership 或 clearCache 行为。

## 10. 验证

验证结果：

- ordinary/headless motionplayer syntax-only：通过；
- Web/Headless full build：通过；
- 两个 Wasm 的 Node `WebAssembly.Module` parse：通过；
- 两个 Wasm 的 llvm-objdump section parse：通过；
- Web/Headless CTest：命令成功，当前两个 build tree 均为 `No tests were found`；
- scoped `git diff --check`：通过，仅有既有 LF/CRLF 提示。

相对 V193 精确零变化：

| 指标 | Web | Headless |
|---|---:|---:|
| file size | `85,654,197` B | `85,001,338` B |
| imports | `539` | `538` |
| exports | `69` | `69` |
| FUNCTION | `0x1BD23` | `0x1BA42` |
| GLOBAL | `0xD5B2` | `0xD5DA` |
| CODE | `0x1A4219A` | `0x19EA148` |
| DATA | `0x5A3FB7` | `0x5A1207` |
| name | `0x31848C0` | `0x3140756` |

编译仅报告仓库既有 `_tss`、`nodiscard`、pthread memory-growth、JSPI 和 JS library warning；
没有新增错误或警告类型。

## 11. 置信度

高置信度：

- ClassInfo 地址、ABI、静态初值与 non-owning Set/Clear；
- wrapper/Setup/RegistBegin/RegistEnd 层级；
- direct constructor value-init/attach failure 回收；
- CreateEmpty 而非 public CreateAdaptor producer 的身份；
- adaptor 多入口 ABI、sticky gate、list→Variant teardown 与不调用 public clearCache。

保留 `_guess` 的只有 stripped 模板 helper 原始拼写和 Android armv7 编译器多入口函数的原始
符号划分；不影响字段、控制流、owner 或 side-effect 结论。
