# EmoteObject 嵌套 clone、state Variant 与 D3D listener shell 异常前沿（四参考二进制，2026-08-21）

## 1. 范围与新增结论

本报告是 V266，承接 V265 的外层 owner/final allocation release 链，闭合此前报告只写出正常
伪代码、没有逐端恢复的嵌套 clone 异常边界：

```text
D3DEmotePlayer::clone(target D3DLayer)
  -> new D3DEmotePlayer(target)       // constructor registers listener
  -> source.primary->clone_guess()
       -> new EmoteObject(paths)
       -> source.Engine.serialize()
       -> copy.Engine.unserialize(state)
  -> publish result into copy.primary
  -> return raw D3D shell
```

V132/V146 已经证明 `EmoteObject` constructor 的 RM/Engine raw-owner 前缀泄漏；V201 已经证明
D3D typed clone 的 listener shell、boxing 与 CreateAdaptor 三态。本轮不重复这些结论，而是把
二者之间尚未闭合的 **state Variant owner 与两层 new-expression EH frontier** 精确恢复出来。

四份当前 `reference/binaries/` 共同证明：

1. `EmoteObject::clone_guess()` 的 `copy` 从 constructor 返回后就是无 RAII 的 raw local；
2. EmoteObject constructor 抛出时，new-expression cleanup 只 delete pending outer storage；
3. constructor 成功以后，serialize 抛出不会析构/delete 完整 copy；此时 state Variant 尚未构造；
4. serialize 正常返回以后 state Variant 才成为 live owner；unserialize 抛出只析构 state，仍不
   析构/delete copy；
5. D3D clone 的 shell constructor 抛出时，new-expression cleanup 只 delete pending shell
   storage；
6. shell constructor 返回以后，新壳已经向目标 D3DLayer 注册 listener，但 shell raw local
   没有任何 EH owner；inner clone 抛出会同时泄漏 shell storage 与 listener registration；
7. 四份 recovery IDB 的 clone/ctor prototype 和旧注释仍残留 `D3DImage` 参数名，虽然实际 typed
   unbox 链明确是 D3DLayer。本轮以四端调用链为准纠正，验证了用户关于旧注释可能过时的提醒。

## 2. 四端函数与 cleanup 入口

### 2.1 `EmoteObject::clone_guess`

| 目标 | main | pending-copy delete | state-live cleanup | ABI 形态 |
|---|---:|---:|---:|---|
| Android A64 | `0x67CD58` | `0x67CDF4` | `0x67CDE0` | 两个 landing block 都在 IDA main function 尺寸内 |
| Android A32 | `0x5611FC` | `0x561262` | `0x561258` | 两个 EH fragment 位于 IDA function end 之后 |
| iOS A64 | `0x1001B50A4` | `0x1001B511C` branch | `0x1001B5110` branch | split noreturn cleanup `0x1001B510C` |
| iOS A32 | `0x1B4CFC` | SJLJ case0 `0x1B4DB4` | SJLJ case1 `0x1B4DC0` | dispatcher `0x1B4DA6` |

allocation size 在两份 64-bit 目标都是 `0x28`，两份 32-bit 目标都是 `0x14`，与已经恢复的
三成员 EmoteObject layout 完全吻合。

### 2.2 `D3DEmotePlayer::clone`

| 目标 | main | shell-constructor cleanup | ABI 形态 |
|---|---:|---:|---|
| Android A64 | `0x53039C` | `0x530434` | landing block 在 main function 内 |
| Android A32 | `0x4949D4` | `0x4949FE` | function-end 后 fragment |
| iOS A64 | `0x100232DC8` | `0x100232E14` | 独立 noreturn cleanup function |
| iOS A32 | `0x2319DC` | `0x231A6E` | SJLJ dispatcher/cleanup function |

shell allocation size在 LP64 是 `0x38`，ILP32 是 `0x24`。目标参数通过四端同一
`ncbInstanceAdaptor<D3DLayer>` unboxer 产生，类型是 borrowed `D3DLayer*`，不是旧 IDB 中的
`D3DImage*`。

## 3. inner clone 的共同正常路径

四端正常指令序列可归一为：

```cpp
EmoteObject *EmoteObject::clone_guess() {
    EmoteObject *copy = new EmoteObject(source.modulePaths);
    tTJSVariant state = source.engine->serializeState_guess();
    copy->engine->unserializeState_guess(state);
    return copy;
}
```

具体顺序不允许改成先 serialize 再构造 copy：

- 先分配并完整构造新的 RM/Engine/Player 链；
- 再从 source Engine 建立 state Variant；
- 再把 state 写入新 Engine；
- 正常路径先析构 caller 栈上的 state Variant，最后返回 raw copy。

新旧 RM/Engine/Player 不是共享或字段拷贝；只有运行态经 Engine serialize/unserialize 迁移。

## 4. inner clone 的三个异常阶段

### 4.1 EmoteObject constructor 抛出

new-expression 仍持有刚分配的 `0x28/0x14` storage，因此四端都执行：

```text
operator delete(pending copy storage)
resume original exception
```

不调用 `EmoteObject::~EmoteObject()`，因为 outer object 没有完成构造。constructor 内部已经发布
的 RM/Engine raw member 是否泄漏，继续遵循 V132 的逐前沿矩阵；这里的 outer storage delete
不会回收那些已失去 owner 的子 allocation。

### 4.2 serialize 抛出

EmoteObject constructor 已经返回，所以 `copy` 是完整对象；但它只是 raw local。serialize 使用
hidden return storage 构造 state Variant，在 serialize 正常返回前 state 不算 live。

四端的共同结果是：

```text
no state Variant destructor
no EmoteObject ordinary destructor
no delete copy storage
resume original exception
```

iOS armv7 给出最直接的机器证明：

```text
call_site = 1       before EmoteObject constructor
constructor returns
call_site = -1      before serialize
serialize(...)
```

`-1` 状态不进入 cleanup dispatcher，所以完整 copy 直接泄漏。

### 4.3 unserialize 抛出

serialize 已正常返回，state Variant 已构造；四端 cleanup 因而必须析构 state，然后 resume。
但完整 copy 仍没有 owner：

```text
destroy live state Variant
no EmoteObject ordinary destructor
no delete copy storage
resume original exception
```

iOS armv7 在 serialize 返回后才写 `call_site=2`。dispatcher 把 1-based call-site 转成 case1，
case1 只调用 `tTJSVariant` destructor；case0 的 pending storage delete 与此路径互斥。case2 是
cleanup 自身再次抛出后的 abort path。

## 5. 四端 cleanup lowering 的精确差异

### 5.1 Android arm64

`EmoteObject_clone_guess` 的 main body、两个 landing 和 stack-check block 都包含在 IDA 的
`0xB0` function range 中：

- `0x67CDE0`：保存异常、析构 SP 上的 state Variant、`_Unwind_Resume`；
- `0x67CDF4`：保存异常、对 X19 pending copy 调 scalar delete、resume。

两个 landing 都没有 Engine/ResourceManager/EmoteObject ordinary destructor call。

### 5.2 Android armv7

IDA main function 只覆盖 `0x5611FC..0x561258`；真实 EH 代码紧随其后但没有 function owner：

- `0x561258`：析构 SP state Variant，跳到共同 resume；
- `0x561262`：delete R4 pending copy，跳到共同 resume；
- `0x56126A`：共同 `_Unwind_Resume`。

所以仅反编译 main function 会错误得出“Android A32 没有 clone cleanup”。本轮用限定 listing
扫描重新读出 function-end 后 fragment，并写入逐地址注释。

### 5.3 iOS arm64

main `0x1001B50A4` 紧接独立 `0x1001B510C` noreturn cleanup。cleanup 内有两个没有普通 CFG
前驱的入口：

- `0x1001B5110`：state Variant destructor；
- `0x1001B511C`：pending storage delete；
- `0x1001B5128`：共同 resume。

这是多入口 landing 被 IDA 包成一个 function 的结果，不是一个可由普通 C++ caller 调用、靠
参数选择分支的 helper。

### 5.4 iOS armv7

SJLJ dispatcher `0x1B4DA6` 最清楚地表达完整状态机：

| raw `call_site` | dispatcher case | cleanup |
|---:|---:|---|
| `1` | case0 | delete pending copy storage |
| `-1` | 不进入 dispatcher | 无 cleanup owner，完整 copy 泄漏 |
| `2` | case1 | destroy live state Variant，完整 copy 泄漏 |
| cleanup throw | case2 | abort |

这同时证明 Android/iOS 64-bit landing 的 source-level 归属，不需要凭反编译代码排列猜测。

## 6. outer D3D clone 与 listener shell 放大效应

四端共同 outer body 为：

```cpp
D3DEmotePlayer *D3DEmotePlayer::clone(D3DLayer *target) {
    D3DEmotePlayer *shell = new D3DEmotePlayer(target);
    shell->primary = source->primary->clone_guess();
    return shell;
}
```

shell constructor 会建立 listener base、保存 borrowed D3DLayer target，并在 target 非空时立即
`AddListener(shell)`，之后才完成 derived slots/scalars/flags。source primary 没有 null guard。

### 6.1 shell constructor 抛出

new-expression cleanup 对 pending `0x38/0x24` storage 调 scalar delete 后 resume；不会调用完整
`D3DEmotePlayer` ordinary/deleting destructor。任何 constructor 内已经发生的外部副作用只服从
constructor/base 自己的异常语义，clone cleanup 本身没有额外 `RemoveListener` 调用。

V267 随后把这个边界收紧到具体可达路径：四端该 shell constructor 唯一可抛调用就是
listener-base 的虚 `AddListener`，而实际 D3DLayer implementation 在任何 sentinel/cached-size
写入之前先分配 node。分配失败时 list 完全未变；分配成功后的 node 初始化、link、iOS
`size++` 与剩余 derived 字段写入均不再调用可抛函数。因此“constructor 抛出但遗留已注册
listener”在四参考中不可达，pending storage delete 已足够；真正会遗留 listener 的仍是
constructor 正常返回后的 inner clone 失败路径。

### 6.2 inner clone 抛出

shell constructor 已返回，新 shell 是 raw local，且 listener 已注册。四端都没有 landing 对它
调用 complete/deleting destructor：

```text
inner EmoteObject clone throws
no secondary/primary pair teardown on shell
no RemoveListener(shell)
no scalar delete(shell)
exception propagates
```

iOS armv7 再次给出直接证明：shell constructor 前为 `call_site=1`，返回后在调用
`EmoteObject_clone_guess` 前写 `call_site=-1`。因此 outer cleanup case0 只能服务 constructor
failure，不能处理 primary clone failure。

### 6.3 嵌套失败矩阵

| inner 失败点 | inner 释放 | inner 泄漏 | outer 额外泄漏 |
|---|---|---|---|
| EmoteObject constructor | pending EmoteObject storage；ctor 自身已建立的普通 stack/member cleanup | V132 所定义的已发布 RM/Engine 前缀 | completed D3D shell + listener |
| serialize | 无 state cleanup | complete EmoteObject/RM/Engine/Player chain | completed D3D shell + listener |
| unserialize | live state Variant | complete EmoteObject/RM/Engine/Player chain | completed D3D shell + listener |
| success | normal state Variant dtor | 无 | primary store 后由 returned shell/adaptor 接管 |

typed wrapper 在 native clone 正常返回后的 `result==null`、CreateAdaptor null/empty/populated 三态与
unchecked Release 是 V201 的后续边界；不能与本轮“native body 尚未返回时”的 EH owner 混为一谈。

## 7. 过时 `D3DImage` 注释的复核与纠正

本轮四库打开时，outer clone prototype/comment 都仍显示 `targetD3DImage`，Android A32/iOS 两端
独立 ctor 的第二参数也仍是 `ownerD3DImage`。但当前四参考的直接证据为：

- typed clone wrapper 使用 D3DLayer ClassInfo ID；
- arg0 进入 `ncbInstanceAdaptor<D3DLayer>` unboxer；
- factory 与 clone 复用同一 listener-base constructor；
- owner 虚调用 slots 与 D3DLayer AddListener/RemoveListener/TransformPoint 链一致。

因此这些名字是旧单目标/相邻 class-state 分析留下的 metadata，不是源类型证据。本轮已把 clone
与三个独立 ctor 的函数原型修成 `targetD3DLayerBorrow/ownerD3DLayerBorrow`；Android A64 ctor
真实内联，所以直接在 clone function comment 和参数原型中纠正。fresh decompile 已在四端回读
D3DLayer 名。

旧报告 `analysis/motionplayer_d3d_shell_raw_slot_protocol_four_binary_2026-08-13.md` 中四处仍把
target/owner 写成 D3DImage，也已原位改成 D3DLayer。V201 报告保留旧字段名的地方只是在描述
“从什么旧名迁移”，不是当前语义残留。

## 8. 源码审计

portable executable statement 原本已经保持精确 raw-local source shape：

```cpp
auto *copy = new EmoteObject(_modulePaths);
tTJSVariant state = _engine->serializeState_guess();
copy->_engine->unserializeState_guess(state);
return copy;
```

以及：

```cpp
auto *copy = new D3DEmotePlayer(d3dLayerOwner);
copy->_primaryObj = obj().clone_guess();
return copy;
```

本轮没有加入 unique_ptr、scope guard、catch/release 或 listener rollback，只在
`EmotePlayer.cpp/.h` 补入 ABI-neutral 的两层失败前沿说明。否则任何“异常安全”改造都会修复参考
中可观察的 allocation/listener 泄漏边界。

## 9. Recovery IDB 写回

四库合计写回：

- 49 条 function/line comment；
- 8 个 bookmark（每端 inner clone 与 outer clone 各一个）；
- 7 个 `_guess` function rename；
- 11 个 function type/prototype update；
- 12 个定向 force-recompile/readback（A32 2、i64 5、i32 5；A64由 analyze fresh 回读）。

分端明细：

| 目标 | comments | bookmarks | renames | type updates | 关键 metadata 修复 |
|---|---:|---:|---:|---:|---|
| Android A64 | 11 | 2 | 0 | 2 | in-function双landing、clone D3DLayer参数 |
| Android A32 | 11 | 2 | 0 | 3 | function-end后双fragment、clone/ctor D3DLayer参数 |
| iOS A64 | 13 | 2 | 2 | 3 | 两个split cleanup命名、clone/ctor D3DLayer参数 |
| iOS A32 | 14 | 2 | 5 | 3 | inner/outer/cleanup/ctor命名、SJLJ状态、D3DLayer参数 |

最终 recovery IDB：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android A64 | 366794239 | `3C38B2CA72392451E5B500EE6C7B50FF8332C029AAB8B12BA0F98BE291AC2569` |
| Android A32 | 345941564 | `1BE0420010C0D8F42CB0EED1E365065E011D4DEEE389B48A006146EFB433719A` |
| iOS A64 | 334966683 | `295E9C0300A4EFC3A0A19C814E840BD97F7083BBECC9D2F31A5EAF171278AF63` |
| iOS A32 | 376835929 | `6264553BF0715CE6C510CAEFBB7AE4674C88D04B6FF8010EDB86EA0EE4447CA9` |

iOS armv7 安全保存记录：

| 状态 | bytes | SHA-256 |
|---|---:|---|
| V266 canonical/pre-backup/candidate initial | 376819545 | `3A2F07D25F2F7E3A405CD06AF6BB4C73EBB1DAD503DFC67758716E7D2B5DE24A` |
| V266 final candidate/canonical | 376835929 | `6264553BF0715CE6C510CAEFBB7AE4674C88D04B6FF8010EDB86EA0EE4447CA9` |

candidate 在编辑前后都经 `C:\IDA\idat.exe -A`，两次 exit 0。发布后 candidate/canonical
大小与 hash 一致；再从 canonical 打开回读五个 function name、outer D3DLayer prototype 与
serialize `call_site=-1` 注释，最后 `save=false` 关闭。最终 `idb_list` 为零会话。

## 10. 验证状态

### 10.1 syntax 与构建

- ordinary motionplayer test TU syntax：exit 0；
- `KRKR2_WASMTIME_HEADLESS=1` test TU syntax：exit 0；
- Web debug build：10/10，exit 0；
- Wasmtime debug build：17/17，exit 0；
- `krkr2_wasmtime_guest`：1/1 link + exnref conversion，exit 0；
- 最后顺序复验 Web、Wasmtime、guest 三目标，全部 `ninja: no work to do`。

日志只有项目既有 `_tss` literal-operator、imagepacker `nodiscard` 与 Emscripten
pthread/JSPI/JS-library warning。

### 10.2 最终 wasm

| 产物 | bytes | SHA-256 |
|---|---:|---|
| `out/web/debug/index.wasm` | 85655322 | `6039AA6D8DC48FB7CCC5840CFF7630EEE9838C1AB2809BCEE5B096BCD42EEC6F` |
| `out/wasmtime/debug/index.wasm` | 85002463 | `EB61485C3B53A2F58F211F264E276337455AA267743AAB789C0A7A4E6049004E` |
| `out/wasmtime/debug/krkr2_wasmtime_guest.wasm` | 151479098 | `68C54DB94EE6CDC85570276D6A0BDC19A0538BD03AE18CAA9E2AE8EB35E5BAB2` |

两个主 wasm 与 V265 逐字节相同。guest 的 executable/data/name 段也不变：

| 产物 | CODE | DATA | name |
|---|---:|---:|---:|
| Web | `0x01A4109D` | `0x005A3E40` | `0x03185F7B` |
| Wasmtime | `0x019E904B` | `0x005A1090` | `0x03141E11` |
| guest | `0x013D7DCD` | `0x004D1630` | `0x01421EBA` |

guest 比 V265 增加 3 bytes，变化来自 `.debug_info` 与其后调试 section 的 VMA 位移；CODE、DATA、
name 均相同，符合本轮只修改源码注释的预期。

### 10.3 最终审计

- `git diff --check`：通过；
- 定向 stale-name 扫描：源码与被纠正旧报告中无 `clone(D3DImage*)`、`target D3DImage`、
  `owner D3DImage` 当前语义；
- `idb_list`：`sessions=[] / count=0`；
- 进程审计只有常驻 `idalib-mcp` 服务，无 IDA GUI、headless worker 或 `idat`；
- 工作树中的其余改动属于此前连续复原片，本轮没有覆盖或回退不相关内容。

## 11. 闭合范围

V266 已闭合：

- EmoteObject clone 的 pending-new、serialize-no-state、unserialize-state-live 三个前沿；
- state Variant 的正常与异常 owner lifetime；
- D3D shell constructor pending storage 与 completed listener shell 的 EH 分界；
- inner failure向 outer shell/listener 泄漏的放大矩阵；
- 四 ABI 的 in-function、out-of-function、split landing 与 SJLJ lowering；
- clone/ctor 参数的 D3DImage→D3DLayer 过时 metadata 纠错。

这不表示整个 motionplayer 已 100% 完成；其余对象和方法的 partial construction、container
mutation commit、script reentry 以及 deleting-dtor 对称性仍需继续按四参考逐片闭合。
