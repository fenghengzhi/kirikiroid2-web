# DrawDeviceD3D → EmotePlayer 依赖根与双 native identity 四端恢复（MP-F01/ROOT-03，2026-08-27）

## 1. 结论

`DrawDeviceD3D.dll` 的 pre-registration callback 在四个参考二进制中执行完全相同
的三段式初始化：

1. 注册内部 native identity `D3DLayerBase`，把 `{name, classId, null classObject}`
   首次发布到一份完整 NCBind `ClassInfo`；
2. 加载 `emoteplayer.dll`；
3. 注册第二个内部 identity `D3DLayerObjectNativeInstance`，把 class ID 写入一个
   独立的进程级 word。

这两个 identity 的对象和 owner 语义不同：

- `D3DLayerBase` 的 adaptor 是 `{vptr, Instance, Sticky}`，服务于 D3D 根对象、
  `D3DLayer` factory 和 `D3DImage` factory；
- `D3DLayerObjectNativeInstance` 的 adaptor 只有 `{vptr, borrowed D3DLayerObject*}`，
  服务于 D3D 根的 `add`/`remove`，永远不拥有或清空 borrowed payload。

本轮完整枚举了两个 class-id storage 的所有 xref、两份 adaptor vtable 及其
invalidate/destruct/deleting-destructor 槽，证明本地实现已经覆盖 producer、consumer、
对象生命周期、四槽重复注册和失败泄漏边界。`MP-F01-ROOT-03` 可升级为
`IMPLEMENTED`，而且这不会把整个 UI/movie 子系统自动纳入 motionplayer 闭包。

## 2. 依赖根映射

| 目标 | callback | 完整指令数 | `D3DLayerBase` ID storage | borrowed ID storage |
|---|---|---:|---|---|
| Android arm64 | `DrawDeviceD3D_PreRegist_dependency_root_guess@0x53101C` | 49 | `0x1AAF6F8` | `0x1AAF484` |
| Android armv7 | `DrawDeviceD3D_PreRegist_dependency_root_guess@0x49516C` | 47 | `0x110E250` | `0x110E0EC` |
| iOS arm64 | `DrawDeviceD3D_PreRegist_dependency_root_guess@0x1002335C8` | 31 | `0x101AEE518` | `0x10256A0A4` |
| iOS armv7 | `DrawDeviceD3D_PreRegist_dependency_root_guess@0x2323C0` | 71 | `0x1838ECC` | `0x218E054` |

共同伪代码：

```text
baseId = TJSRegisterNativeClass(L"D3DLayerBase")
if D3DLayerBaseClassInfo has not been set:
    publish {name=L"D3DLayerBase", id=baseId, classObject=null}

module = LoadModule(L"emoteplayer.dll")
destroy temporary module holder in native order

D3DLayerObjectClassID =
    TJSRegisterNativeClass(L"D3DLayerObjectNativeInstance")
```

Android arm64/iOS arm64 函数返回最后一个 class ID，Android armv7 的反编译返回
stack-canary 差值，iOS armv7 是 `void`；callback 的源码级结果未被 registrar 使用，
这些只是 ABI/优化差异。

四库对 `D3DLayerBase`、`D3DLayerObjectNativeInstance` 和
`emoteplayer.dll` 都完成 UTF-16LE（含双零终止）raw-byte 搜索。每库前两个 class
name 都只有一个完整命中；模块名在部分库还由另一模块记录引用。Hex-Rays 显示的
单字符 `"D"` 是宽字符串未类型化残留，不是实际注册名。

## 3. `D3DLayerBase` 完整 ClassInfo 闭包

### 3.1 唯一函数等价类与完整指令数

对 class-id word 的全量 xref 在每端都是 7 个 data xref，去重后恰好落入以下 6 个
函数：静态空 tuple 初始化、ROOT-03 publisher、两个 factory、主根构造和 adaptor
setter。没有第七个 class-id consumer。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| ClassInfo 静态空初始化 | `D3DLayerBase_ClassInfo_static_init_guess@0x42CB78`；11 | `...@0x2FF034`；17 | `...@0x10024CAA0`；11 | `...@0x24E680`；17 |
| `D3DLayer` factory 解包根 | `D3DLayer_factory_unwrapBase_guess@0x52D308`；63 | `...@0x49361C`；65 | `...@0x1002317E8`；48 | `...@0x230594`；91 |
| `D3DImage` factory 解包根 | `D3DImage_factory_unwrapBase_guess@0x52D98C`；109 | `...@0x4939F8`；71 | `...@0x100231BE8`；53 | `...@0x2309DC`；89 |
| 主根构造并置 sticky | `DrawDeviceObjectBase_ctor_registerStickyBase_guess@0x531274`；102 | `...@0x495618`；86 | `...@0x100233C88`；76 | `...@0x23295C`；132 |
| adaptor GET/replace/REGISTER | `RegisterD3DLayerBaseNative_guess@0x5322AC`；73 | `...@0x495F90`；85 | `...@0x100234964`；61 | `...@0x2336CA`；70 |

ROOT-03 publisher 是表中第六个函数，指令数见上一节。所有 disassembly cursor 均
`done=true`。

### 3.2 adaptor 状态机

LP64 adaptor 为 24 B，ILP32 为 12 B：

```text
struct D3DLayerBaseNativeInstance {
    vptr
    DrawDeviceObjectBase *Instance
    bool Sticky
}
```

共同 setter 伪代码：

```text
adaptor = owner.NativeInstanceSupport(GETINSTANCE, baseClassId)
if found:
    if adaptor.Instance != null:
        if !adaptor.Sticky:
            delete adaptor.Instance through its virtual deleting destructor
        adaptor.Instance = null
        adaptor.Sticky = false
    // an already-empty adaptor deliberately retains its old Sticky bit
else:
    adaptor = new {baseAdaptorVptr, null, false}

adaptor.Instance = newRoot
status = owner.NativeInstanceSupport(REGISTER, baseClassId, adaptor)
if status failed and throwOnFailure:
    throw "Adaptor registration failed."
return status succeeded
```

主 D3D 根构造传 `throwOnFailure=false` 并忽略返回值，随后再次按 class ID 执行
`GETINSTANCE`，不提供安全 fallback，直接把返回 adaptor 的 `Sticky` 写为 true。
因此正常根对象由其 concrete NCBind adaptor 拥有；`D3DLayerBase` 只是 sticky view。
若 owner/registration 关系被破坏，原生路径会在置 sticky 时触发空指针边界，不能在
portable 实现中静默修复成可选值。

### 3.3 vtable 与 owner 规则

四端 vtable 分别位于 `0x19FAB78`、`0x10AAFD8`、`0x101AEE7D8`、
`0x183902C`，都有五个 `tTJSNativeInstance` 槽：

1. `Construct` 返回 0；
2. `Invalidate` 执行下述 DeleteInstance；
3. `Destruct` 调用 adaptor 自身 deleting destructor；
4. complete destructor；
5. deleting destructor。

`Invalidate`/destructor 的共同逻辑：

```text
if Instance != null and !Sticky:
    delete Instance
Instance = null
Sticky = false
```

所以 empty adaptor 的 sticky 在 setter 的“复用但旧 payload 已经为 null”路径保留，
而 invalidate/destructor 总会最终清零 sticky。新的 root 写入一个原本 sticky 且非空
的 adaptor 时不删除旧 root，但 setter 会先把 sticky 重置为 false，构造路径随后再
置 true。

### 3.4 两个 consumer factory

`D3DLayer` 和 `D3DImage` factory 共同要求至少一个参数，只消费 arg0 并忽略 surplus。
arg0 必须是对象；随后按独立 `D3DLayerBase` class ID 执行 `GETINSTANCE`，解包
`Instance`（LP64 `+8`、ILP32 `+4`）。GET 失败、adaptor null 或 root null 都返回
`TJS_E_INVALIDTYPE`。成功后：

- `D3DLayer` 分配子 layer，并把 root 作为 parent；
- `D3DImage` 分配 image，把 root 作为非 owning owner，并把 image 指针插入 root 的
  managed-object set。

这两个 factory 不接受具体 `D3D`/`DrawDeviceD3D` class ID 冒充 base ID，也不使用
后述 borrowed-view ID。

## 4. `D3DLayerObjectNativeInstance` borrowed-view 闭包

### 4.1 class-id xref 分母

borrowed class-id word 的完整 xref 在每端去重后都恰好是四个函数等价类：ROOT-03
writer、`add` 解包、`remove` 解包、`D3DLayerObject` 构造/注册路径。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| add 解包 | `D3DRoot_add_unwrapD3DLayerObject_guess@0x52B82C`；33 | `...@0x492CA8`；35 | `...@0x100230D58`；25 | `...@0x22FC5E`；22 |
| remove 解包 | `D3DRoot_remove_unwrapD3DLayerObject_guess@0x52B8B0`；50 | `...@0x492D00`；35 | `...@0x100230DBC`；25 | `...@0x22FC92`；22 |
| 构造/REGISTER 路径 | `D3DLayerObject_registration_ctor_path_guess@0x5333F0`；109 | `...@0x496990`；52 | `...@0x1002355B4`；37 | `...@0x2342B4`；80 |

Android arm64 把 base 注册内联进 complete `D3DLayer` constructor，并继续初始化
derived fields/Mat4；另三端保留可复用的 `D3DLayerObject` base constructor。共同
注册子序列不变。

### 4.2 producer 与忽略失败边界

共同构造子序列：

```text
initialize D3DLayerObject base fields/list
if ScriptOwner != null:
    adaptor = new {borrowedAdaptorVptr, this}
    owner.NativeInstanceSupport(REGISTER, borrowedClassId, &adaptor)
    ignore returned tjs_error
```

没有先按相同 ID 查重，也没有在 REGISTER 失败时 delete adaptor。核心
`tTJSCustomObject` 只有四个 native slots；D3DLayer receiver 通常已有 concrete adaptor，
第一次构造把 borrowed view 放入下一个空槽。constructor re-entry 会继续附加相同
class ID，直至槽满；之后 REGISTER 返回 `TJS_E_FAIL`，fresh borrowed adaptor 泄漏，
但外层 concrete factory 仍可报告成功。

### 4.3 consumer 与 oldest-generation 行为

add/remove 都只在 Variant 为 Object 时进入解包；随后执行：

```text
native = null
status = object.NativeInstanceSupport(GETINSTANCE, borrowedClassId, &native)
borrowed = failed/status-null ? null : native.borrowedPointer
```

LP64 payload 在 adaptor `+8`，ILP32 在 `+4`。GET 从 native slot 0 向上扫描并返回
第一个匹配 ID，所以重复构造后 concrete class dispatch 可以指向最新 generation，
add/remove 却始终看到最早注册的 borrowed generation。

add 无论解包是否成功都会调用根的 `AddChild`：null 只跳过 set 插入/回调，仍执行
`OnItemsChanged`。remove 对非 Object、GET 失败或 null payload 直接返回；成功时分别
从 front/back 索引移除，任一成功才调用 child `OnDetached` 和 root
`OnItemsChanged`。

### 4.4 borrowed adaptor vtable

四端 vtable 分别位于 `0x19FAC80`、`0x10AB05C`、`0x101AEE908`、
`0x18390C0`，同样有五个槽，但生命周期完全不同：

1. `Construct` 返回 0；
2. `Invalidate` 是 no-op；
3. `Destruct` 只转调 adaptor 自身 deleting destructor；
4. complete destructor 只恢复 base vptr；
5. deleting destructor 只释放 adaptor storage。

任何槽都不读取、delete 或清零 borrowed `D3DLayerObject*`。因此具体 layer 析构后，
script shell 上可以继续保留 dangling borrowed view；shell invalidate/destruct 不会解引用
它。这解释了参考实现的 generation 泄漏和 oldest-slot 行为，也证明不能把该 adaptor
改成 `unique_ptr` 或自动 detach view。

## 5. 本地逐行对照

| 参考语义 | 本地实现 |
|---|---|
| 两个独立 class identity：完整 ClassInfo 与单 word | `cpp/plugins/DrawDeviceD3D.cpp:56-75` |
| sticky base adaptor、GET/REGISTER setter 和 delete 规则 | `cpp/plugins/DrawDeviceD3D.cpp:77-141`、`cpp/plugins/DrawDeviceD3D.cpp:717-742` |
| borrowed adaptor、忽略 REGISTER 失败和 GET oldest view | `cpp/plugins/DrawDeviceD3D.cpp:101-113`、`cpp/plugins/DrawDeviceD3D.cpp:143-170` |
| add/remove 对 null、front/back set 和回调的边界 | `cpp/plugins/DrawDeviceD3D.cpp:458-495` |
| D3DLayerObject constructor 每次附加 borrowed view | `cpp/plugins/DrawDeviceD3D.cpp:744-750` |
| D3DLayer factory 使用 base identity | `cpp/plugins/DrawDeviceD3D.cpp:1336-1360` |
| D3DImage factory 使用 base identity | `cpp/plugins/DrawDeviceD3D.cpp:1442-1459` |
| ROOT-03 精确注册/加载顺序 | `cpp/plugins/DrawDeviceD3D.cpp:1833-1844` |

现有用例覆盖：

- `tests/unit-tests/plugins/motionplayer-dll.cpp:8781-8802`：两个 identity 已注册但不
  发布脚本全局 class；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:8846-8941`：root 的独立 sticky base
  identity 与 concrete identity 不混用；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:9660-9720`：D3DLayer/D3DImage factory
  的 arity、类型、Void sentinel、surplus 和 base unwrap；
- `tests/unit-tests/plugins/motionplayer-dll.cpp:9943-10065`：四槽重复注册、oldest
  borrowed view、满槽失败、generation 泄漏和 dangling view 析构边界。

本轮没有修改 C++ 语义；本地代码和测试已与 fresh 四端证据一致。当前环境仍缺少
CMake/Emscripten/Ninja 及项目完整依赖，不能声称正式构建/测试通过。实际验证包括：
四端完整 decompile/disassembly、两组 class-id 全量 xref、两份五槽 vtable 和 owner
审计、三组 UTF-16LE raw-byte 搜索、本地逐行比较、覆盖 TSV 校验和
`git diff --check`。

## 6. 范围收口

ROOT-03 证明 DrawDeviceD3D 的根、layer/image factory、root-base adaptor 和
D3DLayerObject borrowed view 都属于 motionplayer 的依赖闭包；D3DEmoteModule 与
D3DEmotePlayer 的注册表已经由独立 NCB ledger 承接。它不证明同一大二进制内所有
DrawDevice、movie、UI 或平台代码都属于本任务。`MP-F01` 的三个根项现已全部闭合，
后续回到仍为 `EVIDENCED_4_4` 的 root-reachable 函数、对象、容器和 owner 条目。
