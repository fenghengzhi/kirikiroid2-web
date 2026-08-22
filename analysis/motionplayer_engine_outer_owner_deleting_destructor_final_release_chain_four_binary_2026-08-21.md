# EmoteEngine 外层 owner、deleting destructor 与最终 allocation release 链（四参考二进制，2026-08-21）

## 1. 本纵切面的目的

本报告是 V265。V264 已经闭合 `EmoteEngine::~EmoteEngine()` 内部从 wind、HM7–HM4、
三个 Variant、七个 controller owner 到 Player owner 的正常逆声明顺序，但它刻意停在
**普通析构函数返回**这一层。本轮继续向外追踪，回答下列容易被反编译伪代码混在一起的问题：

- `EmoteEngine` 自身是否有 virtual/deleting destructor；
- 普通析构完成以后，究竟由谁对 Engine allocation 调用 scalar `operator delete`；
- `EmoteObject`、`ncbInstanceAdaptor<EmotePlayer>` 与 `D3DEmotePlayer` 是哪三个不同层级；
- adaptor 的 sticky/native 标志如何控制 Engine 销毁，adaptor shell 又何时释放；
- D3D 外层两个 `EmoteObject*` slot 的销毁、清零和 D3D shell deleting destructor 顺序；
- typed NCB constructor 的成功发布、正常 attach 失败和异常回滚如何分叉。

本轮只把 `reference/binaries/` 当前四份 recovery IDB 作为原始证据，不沿用旧
`libkrkr2.so` 注释来推断结论。检查对象是：

| 简称 | 当前参考目标 |
|---|---|
| Android A64 | `Kirikiroid2_1.3.9_Android_arm64-v8a.so` |
| Android A32 | `Kirikiroid2_1.3.9_Android_armabi-v7a.so` |
| iOS A64 | `Kirikiroid2_1.3.9_iOS_arm64` |
| iOS A32 | `Kirikiroid2_1.3.9_iOS_armv7` |

## 2. 最终结论

### 2.1 `EmoteEngine` 没有 deleting destructor

四端共同结论非常明确：

1. `EmoteEngine` 是非多态对象；对象开头不是该类的 vptr。
2. 每端只有一个供真实 owner 调用的 ordinary/nonvirtual destructor。
3. ordinary destructor 只销毁 Engine 内部成员，不释放 `this` 指向的 Engine allocation。
4. 每个拥有 Engine allocation 的外层路径都会显式执行：

```text
engine->~EmoteEngine();
operator delete(engine);
```

5. 四端都没有可独立定位的 `EmoteEngine deleting destructor`。

因此，反编译里紧随 Engine destructor 的 `operator delete` 是**调用者的 owner 协议**，不能
把二者合并命名成 Engine deleting destructor。真正存在独立 deleting destructor 的是带 vptr
的 NCB adaptor shell 和 D3D wrapper shell；它们释放的是各自外层 allocation。

### 2.2 三层 ownership 不可合并

```text
D3DEmotePlayer shell allocation
  owns primary/secondary EmoteObject allocations
    each EmoteObject owns one EmoteEngine allocation
      EmoteEngine owns Player/controllers/containers/variants/wind

ncbInstanceAdaptor<EmotePlayer> shell allocation
  conditionally owns one EmoteEngine allocation through native + sticky
```

两条外层链最终都能到达 Engine，但语义不同：

- `EmoteObject` 是无 vptr 的 raw aggregate owner；其普通析构销毁 Engine、ResourceManager 和
  path vector，调用者另行释放 `EmoteObject` allocation。
- `ncbInstanceAdaptor<EmotePlayer>` 是 NCB 壳；其 `native && !sticky` 条件决定是否销毁 Engine，
  adaptor 的 deleting destructor 最后释放 adaptor shell。
- `D3DEmotePlayer` 拥有最多两个 `EmoteObject` allocation；其 deleting destructor 在普通/完整
  析构和 listener base teardown 后释放 D3D shell。

## 3. Engine ordinary destructor 的全量直接引用

### 3.1 入口

| 目标 | `EmoteEngine` ordinary destructor |
|---|---:|
| Android A64 | `0x67C898` |
| Android A32 | `0x5610E8` |
| iOS A64 | `0x1001B8B4C` |
| iOS A32 | `0x1B814E` |

### 3.2 Android arm64：六个引用点覆盖四类 owner 情形

| 引用点 | 所属函数/语义 |
|---:|---|
| `0x67C820` | `EmoteObject` ordinary destructor |
| `0x6836D4` | adaptor shared native teardown |
| `0x683728` | adaptor complete destructor 内联 native teardown |
| `0x683788` | adaptor deleting destructor 内联 native teardown |
| `0x689E14` | typed NCB construct 的正常 attach-failure release |
| `0x689E70` | 同一 constructor 的异常 cleanup release |

这里虽然 complete/deleting/shared 三个 adaptor entry 因优化分别含有 Engine dtor call，它们仍然
是同一个 `native && !sticky` source-level 协议的不同 lowering，不是三个 Engine destructor 变体。

### 3.3 Android armv7：四个有意义引用

| 引用点 | 所属函数/语义 |
|---:|---|
| `0x5610C8` | `EmoteObject` ordinary destructor |
| `0x5651D6` | adaptor shared teardown body |
| `0x56A382` | typed NCB construct 的正常 attach-failure release |
| `0x56A3CC` | main function 边界外的 EH cleanup fragment |

### 3.4 iOS arm64 与 armv7

| 目标 | EmoteObject | adaptor shared teardown | 正常 attach failure | EH cleanup |
|---|---:|---:|---:|---:|
| iOS A64 | `0x1001B5070` | `0x1001B91A0` | `0x1001C606C` | `0x1001C60C0`，cleanup entry `0x1001C608C` |
| iOS A32 | `0x1B4CD8` | `0x1B8848` | `0x1C3266` | `0x1C32CE`，cleanup entry `0x1C32B0` |

iOS 两端同样没有 Engine deleting destructor。iOS A64 把 catch cleanup 切成独立 function，
iOS A32 使用 SJLJ dispatcher/cleanup；这是异常 ABI 形态差异，不改变 source ownership。
iOS A32 的 IDA listing 还会把 ordinary destructor 前一条 `0x1B814C` NOP/fall-through 记成
无函数归属的引用；它不是额外调用者，也没有形成独立 destructor。

## 4. `EmoteObject`：Engine allocation 的第一种 raw owner

### 4.1 完整布局

`EmoteObject` 无 vptr，四端可归一成三个声明顺序成员：

| 字段 | 64-bit offset | 32-bit offset | ownership |
|---|---:|---:|---|
| ResourceManager pointer | `+0x00` | `+0x00` | owning raw pointer |
| EmoteEngine pointer | `+0x08` | `+0x04` | owning raw pointer |
| path vector | `+0x10` | `+0x08` | inline STL vector |
| `sizeof(EmoteObject)` | `0x28` | `0x14` | — |

### 4.2 ordinary destructor 入口与共同顺序

| 目标 | entry/thunk |
|---|---|
| Android A64 | `0x67C800` |
| Android A32 | `0x5610BE` |
| iOS A64 | `0x1001B5058`，`0x1001B50A0` thunk |
| iOS A32 | `0x1B4CCE`，`0x1B4CF8` thunk |

共同 source-level 伪代码为：

```cpp
EmoteObject::~EmoteObject() {
    if (engine_) {
        engine_->~EmoteEngine();
        operator delete(engine_);
    }
    if (resourceManager_) {
        resourceManager_->~ResourceManager();
        operator delete(resourceManager_);
    }
    paths_.~vector();
}
```

这里顺序看似与成员声明逆序不同，是因为当前 portable 类型本身已经是显式 raw-owner wrapper，
析构 body 先释放两个 pointee，随后 compiler-generated member teardown 销毁 path vector。最重要的
边界是：`EmoteObject::~EmoteObject()` **从不 delete 外层 `EmoteObject` allocation**。它的 owner
在普通析构返回后另行调用 scalar delete。

## 5. `D3DEmotePlayer`：两个 EmoteObject owner slots 与外层 shell release

### 5.1 slot 布局

| 字段 | 64-bit offset | 32-bit offset |
|---|---:|---:|
| primary `EmoteObject*` | `+0x18` | `+0x10` |
| secondary `EmoteObject*` | `+0x20` | `+0x14` |

四端一致的 clear/replacement/dtor owner 协议为：

```cpp
if (secondary) {
    secondary->~EmoteObject();
    operator delete(secondary);
}
if (primary) {
    primary->~EmoteObject();
    operator delete(primary);
}
primary = nullptr;
secondary = nullptr;
```

关键边界有三点：

- secondary 总在 primary 之前；
- 两个 pointee 都销毁/delete 后才把两个 raw slot 清零；
- 用两个独立 `unique_ptr::reset()` 会改变销毁过程中可观察的 slot 内容，因此源码保留 raw pair。

### 5.2 四端 helper、load、complete destructor 与 deleting destructor

| 目标 | clear helper | load/replacement | complete dtor | deleting dtor |
|---|---:|---:|---:|---:|
| Android A64 | `0x530164` | `0x5301B4` | `0x533FE0` | `0x534078` |
| Android A32 | `0x4948C4` | `0x494920` | `0x497870` | `0x497894` |
| iOS A64 | `0x100232C1C` | `0x100232CB0` | `0x100236374` | `0x1002363A8` |
| iOS A32 | `0x231840` | `0x231890` | `0x235076` | `0x23509A` |

Android A64 的 load 与 complete destructor 把 pair teardown 内联；Android A32 与 iOS 两端更常
调用 shared helper。iOS A32 load 在清空旧 pair 后分配 `0x14` byte 的 `EmoteObject` 并只在构造
完成后发布为 primary，这与 32-bit layout 相互验证。

complete destructor 在 pair teardown 后继续 listener/base teardown；deleting destructor 在相同
teardown 完成后才对 `D3DEmotePlayer` shell 调用 `operator delete(this)`。所以 D3D deleting
destructor 与 Engine allocation release 相隔两层：

```text
D3D deleting dtor
  -> EmoteObject ordinary dtor
       -> Engine ordinary dtor
       -> delete Engine allocation
  -> delete EmoteObject allocation
  -> listener/base teardown
  -> delete D3D shell allocation
```

## 6. `ncbInstanceAdaptor<EmotePlayer>`：native/sticky 条件 owner

### 6.1 layout 与 shared teardown

| 字段 | 64-bit offset | 32-bit offset |
|---|---:|---:|
| adaptor vptr | `+0x00` | `+0x00` |
| native Engine pointer | `+0x08` | `+0x04` |
| sticky flag | `+0x10` | `+0x08` |

共同 source-level 协议为：

```cpp
if (native && !sticky) {
    native->~EmoteEngine();
    operator delete(native);
}
native = nullptr;
sticky = false;
```

`sticky == true` 只抑制 native Engine 的析构/delete；shared/complete 路径仍清 native slot 和 sticky
标志。deleting destructor 中某些清字段写入被 dead-store elimination 移除，因为 adaptor shell
紧接着就释放，不能由此推断 deleting path 具有不同 source policy。

### 6.2 四端 entry cluster

| 目标 | Invalidate thunk | complete dtor | deleting dtor | shared `_deleteInstance` body |
|---|---:|---:|---:|---:|
| Android A64 | 调用 shared body | `0x6836F4` | `0x683754` | `0x6836B0` |
| Android A32 | `0x565178` cluster entry | `0x56517C` subentry | `0x5651A4` subentry | `0x5651C8` subentry |
| iOS A64 | `0x1001B9104` | `0x1001B9108` | `0x1001B914C` | `0x1001B9180` |
| iOS A32 | `0x1B87E2` | `0x1B87E8` | `0x1B8814` | `0x1B883A` |

Android A32 是最容易被 IDA 函数边界误导的一端：`0x565178`–`0x5651C8` 是一个连续 cluster，
包含 Invalidate/complete/deleting/shared 四个真实入口。旧报告把 `0x5651C8` 写成整个 adaptor dtor
入口是不准确的；本轮已经把它修成 shared body，并单独标出三个前置入口。

adaptor deleting destructor 的末尾 scalar delete 释放的是 **adaptor shell**。它在此前条件性释放
Engine allocation，但绝不能命名成 Engine deleting destructor。

## 7. typed NCB constructor：pending Engine 的发布与两条失败路径

| 目标 | typed constructor | EH cleanup entry/fragment |
|---|---:|---:|
| Android A64 | `0x689D7C` | 同一函数内 landing |
| Android A32 | `0x56A310` | `0x56A3C2`/`0x56A3CC` 边界外 fragment |
| iOS A64 | `0x1001C5FBC` | `0x1001C608C` |
| iOS A32 | `0x1C31C8` | `0x1C32B0` |

四端共同数据流为：

```text
allocate raw Engine storage
construct complete EmoteEngine in pending local
attempt to acquire/attach typed NCB adaptor

success:
  publish Engine pointer into adaptor.native
  return success

normal missing/failed attachment:
  Engine ordinary destructor
  scalar delete pending Engine allocation
  return TJS_E_FAIL (-1008)

exception before publication:
  if pending Engine exists:
    Engine ordinary destructor
    scalar delete pending Engine allocation
  resume/rethrow original exception
```

成功路径的所有权 commit 是 adaptor native slot 写入；在此之前 Engine 只是 local pending owner。
正常 attach failure 不是异常，它明确 release 后返回 `-1008`。异常 cleanup 只释放仍由 local
持有的 candidate，已经发布的 Engine 不再由该 local cleanup 重复释放。

## 8. 与 V264 的边界拼接

V264 与 V265 可以严格拼成如下生命周期：

```text
outer owner decides to release Engine
  -> EmoteEngine ordinary destructor
       -> wind/HM/Variant/controllers/Player/... complete member teardown (V264)
  -> outer owner invokes scalar operator delete on Engine allocation (V265)
  -> outer owner clears or destroys its own ownership state
  -> optional outer deleting destructor releases outer shell allocation
```

这条拼接解释了为什么 V264 只看到 dying Engine 内 controller/Player slots 的具体清零调度，却
看不到 `delete this`；`delete this` 从来就不属于 Engine ordinary destructor。它也解释了为何
`EmoteObject` dtor、adaptor shared teardown、typed constructor failure 都会出现同样的
`Engine dtor -> scalar delete` 二联序列。

## 9. 源码审计与修正

本轮没有改变运行时行为，只修正会误导后续复原的 ownership 注释：

- `cpp/plugins/motionplayer/EmoteEngine.h`
  - 明确 Engine 非多态，只有 ordinary/nonvirtual destructor；
  - 明确 Engine allocation 的 scalar delete 由外层 owner 提供。
- `cpp/plugins/motionplayer/EmotePlayer.h`
  - 区分 EmoteObject、NCB adaptor 与 D3D shell 三层；
  - 明确 adaptor native teardown 与 adaptor deleting destructor 释放的是不同 allocation；
  - 明确 D3D secondary→primary→paired-zero protocol 及外层 deleting destructor。
- `cpp/plugins/motionplayer/EmotePlayer.cpp`
  - 明确 EmoteObject ordinary destructor 不释放自己的 outer allocation；
  - 明确 D3D complete/deleting destructor 与 pair teardown 的层级。
- `cpp/core/plugin/ncbind.hpp`
  - 给通用 `_deleteInstance` 补充 native/sticky ownership 说明。

同时原位纠正两份旧分析中 Android A32 adaptor cluster 的过时边界：

- `analysis/motionplayer_lifecycle_four_binary_2026-08-11.md`
- `analysis/motionplayer_emoteplayer_classinfo_typed_factory_no_unregistration_four_binary_2026-08-17.md`

源码中没有写入当前参考目标的绝对地址；地址只保留在本分析映射表中。

## 10. 四份 recovery IDB 写回

本轮共写回 **46 条 comment、40 个 bookmark、21 个 `_guess`/语义 rename**，四库均已保存。

### 10.1 Android arm64

- 10 comment、9 bookmark、3 rename；
- rename：
  - `0x6836B0 EmotePlayerAdaptor_deleteInstance_guess`
  - `0x6836F4 EmotePlayerAdaptor_completeDtor_guess`
  - `0x683754 EmotePlayerAdaptor_deletingDtor_guess`

### 10.2 Android armv7

- 12 comment、11 bookmark、1 cluster rename；
- `0x565178 EmotePlayerAdaptor_invalidateThunk_cluster_guess`；
- complete/deleting/shared subentry 与 typed-constructor EH fragment 均用 comment/bookmark 单独标出。

### 10.3 iOS arm64

- 12 comment、11 bookmark、5 rename；
- 分别命名 adaptor Invalidate、complete、deleting、shared deleteInstance 与 constructor cleanup。

### 10.4 iOS armv7

- 12 comment、9 bookmark、12 rename；
- 覆盖 EmoteObject dtor/thunk、D3D clear/load/complete/deleting、adaptor 四入口以及 typed
  construct/cleanup。

iOS armv7 继续使用不同路径的 pre-backup/candidate/`idat -A`/canonical readback 安全流程：

| 状态 | size | SHA-256 |
|---|---:|---|
| V265 pre/candidate initial | 376770393 | `59B095303D26E1266313349A58801918F6A70619C5F72CAC9585A1F2DD6AA8A9` |
| V265 final candidate/canonical | 376819545 | `3A2F07D25F2F7E3A405CD06AF6BB4C73EBB1DAD503DFC67758716E7D2B5DE24A` |

final canonical 已回读 adaptor shared teardown、D3D clear 和 NCB cleanup 的 name/comment 后关闭。

四份最终 recovery IDB：

| 目标 | size | SHA-256 |
|---|---:|---|
| Android A64 | 366794203 | `89BBAE8CC6E4DD698D5388FF0C891918A03BFB3C7131B37D5993ED36E078D4C9` |
| Android A32 | 345936196 | `DA471E7E273B9B518C20376567CE3D43FBF8FEDFA814BA0D75A76192F435A504` |
| iOS A64 | 334966642 | `FCD67DF722B41868DF19AD40F658304DDB65C9F98032A5E0F75A14370C5F851C` |
| iOS A32 | 376819545 | `3A2F07D25F2F7E3A405CD06AF6BB4C73EBB1DAD503DFC67758716E7D2B5DE24A` |

最终 `idb_list` 为 `sessions=[] / count=0`；进程审计只有常驻 MCP 服务，没有 IDA GUI 或
`idat` worker。

## 11. 验证

### 11.1 syntax 与完整构建

- Web motionplayer response-file syntax check：exit 0；
- `KRKR2_WASMTIME_HEADLESS=1` syntax check：exit 0；
- Web debug build：82/82，exit 0；
- Wasmtime debug build：119/119，exit 0；
- `krkr2_wasmtime_guest`：1/1 link+exnref conversion，exit 0；
- 随后 Web 与 Wasmtime 顺序复验均为 `ninja: no work to do`。

警告仅包含项目既有的 `_tss` literal-operator、imagepacker 属性以及 Emscripten
pthread/JSPI/JS library 警告。

### 11.2 wasm 产物

| 产物 | bytes | SHA-256 |
|---|---:|---|
| `out/web/debug/index.wasm` | 85655322 | `6039AA6D8DC48FB7CCC5840CFF7630EEE9838C1AB2809BCEE5B096BCD42EEC6F` |
| `out/wasmtime/debug/index.wasm` | 85002463 | `EB61485C3B53A2F58F211F264E276337455AA267743AAB789C0A7A4E6049004E` |
| `out/wasmtime/debug/krkr2_wasmtime_guest.wasm` | 151479095 | `764FCF9FF850CCA33FD60F71108C1F32E0D3861C4CD221EBFECE88BC44CF50C4` |

两个主 wasm 与 V264 完全相同，符合“本轮只改注释”的预期。guest 相比上轮仅最终调试信息/
exnref 转换产物哈希变化；本轮 section audit 的关键段为：

| 产物 | CODE | DATA | name |
|---|---:|---:|---:|
| Web | `0x01A4109D` | `0x005A3E40` | `0x03185F7B` |
| Wasmtime | `0x019E904B` | `0x005A1090` | `0x03141E11` |
| guest | `0x013D7DCD` | `0x004D1630` | `0x01421EBA` |

`git diff --check` exit 0；工作树仍包含此前连续复原片的既有修改，本轮没有覆盖或回退任何
不相关改动。

## 12. 闭合范围与后续边界

本纵切面已经闭合：

- Engine ordinary destructor 与 scalar allocation delete 的职责边界；
- Engine dtor 的四端直接 caller 集合；
- EmoteObject 的完整 raw-owner teardown；
- adaptor native/sticky/shared/complete/deleting 四入口语义；
- typed NCB attach 的 success、normal failure 与 EH release；
- D3D secondary/primary pair teardown、listener/base teardown 与 D3D shell release；
- Android A32 cluster 和 EH fragment 等平台函数边界差异。

它不宣称整个 motionplayer 生命周期已经 100% 完成。下一步仍应沿外层 class-info/adaptor
publication、clone/replace 失败前沿、D3D listener 注册撤销的重入可见状态，以及其余高价值
对象的 deleting-dtor/constructor-unwind 对称性继续做四参考纵切面。
