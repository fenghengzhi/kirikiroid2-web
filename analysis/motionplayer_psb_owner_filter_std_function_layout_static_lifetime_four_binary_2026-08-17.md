# MotionPlayer PSB OwnerFilter `std::function` 内部布局、静态生命周期与并发边界四参考复核（2026-08-17）

## 1. 结论

本轮从上一轮 `commandKeyMemberHint_guess` 的物理后继地址继续追踪。四端共同否定了
“后继 4-byte word 是另一个 TJS member hint”的候选：LP64 两端的 `key + 4`、iOS
armv7 的 `key + 4` 都没有任何引用，Android armv7 则因 ABI 对齐无需该空洞。真正的下一
个对象是同一个 process-wide

```cpp
PSB::PSBFile::OwnerFilter emotePSBDecryptFilter;
```

即 `std::function<void(PSBRawOwner &)>`。它由两条 setter 共同替换，由
`ResourceManager::load` 直接借用，并在 process/static teardown 时析构。四端语义完全一致，
但内部容器布局分成两套标准库 ABI：

- Android/libstdc++：erased target buffer + manager + invoker；arm64 为 32 B，armv7 为
  16 B；
- iOS/libc++：inline target storage + active-target pointer；arm64 为 32 B，armv7 为
  **20 B**。

两个实现都先 copy-construct 临时目标，再与全局对象交换，最后销毁旧目标。因此 incoming
copy 抛异常时旧 filter 原样保留；成功时旧目标在 replacement 返回前释放。四端也都没有
mutex、atomic snapshot 或每次 load 的 `std::function` 副本：setter 与 load 并发是 shipped
data-race boundary，本地不能擅自用锁掩盖。

## 2. 物理对象映射与空洞边界

| 目标 | `commandKey` | key 后空洞 | OwnerFilter object | size | 判别字段 | 后继 `g_randomMemberHint` |
|---|---:|---:|---:|---:|---:|---:|
| Android arm64-v8a | `0x1AB52D8` | `0x1AB52DC..E0`，4 B | `0x1AB52E0` | 32 B | manager `0x1AB52F0` | `0x1AB5300` |
| Android armeabi-v7a | `0x11117E4` | 无 | `0x11117E8` | 16 B | manager `0x11117F0` | `0x11117F8` |
| iOS arm64 | `0x101B697A0` | `0x101B697A4..A8`，4 B | `0x101B697A8` | 32 B | target `0x101B697C0` | `0x101B697C8` |
| iOS armv7 | `0x187D4A8` | `0x187D4AC..B0`，4 B | `0x187D4B0` | 20 B | target `0x187D4C0` | `0x187D4C8` |

iOS armv7 的 20-byte object 后另有 `0x187D4C4..C8` 4-byte tail-alignment gap。所有
object bytes 在文件初态均为零；动态初始化只再次清空真正的 empty discriminator
（libstdc++ manager 或 libc++ active-target pointer），随后登记整个对象的 `atexit`
析构。

这也解释了最初候选为什么不能按“相邻 4-byte global”推进：

```text
Android A64 / iOS A64 / iOS A32:
    commandKey uint32
    ABI alignment gap
    std::function object

Android A32:
    commandKey uint32
    std::function object immediately follows
```

物理链接顺序和静态初始化 coalescing 可用于发现对象，却不能证明原作者把这些声明写在同一
个 source block，更不能把 OwnerFilter 的 storage words 命名成 member hints。

## 3. 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| process-global replacement wrapper | `0x6A5BB0` | `0x57B174` | `0x1001010B0` | `0xFE1E0` |
| libc++ copy/swap/destroy replacement body | — | — | `0x1001010C4` | `0xFE1F0` |
| OwnerFilter copy helper | inline in wrapper | `0x5A7248` | `0x100139A44` | `0x139CC0` |
| OwnerFilter swap helper | inline in wrapper | inline in wrapper | `0x1001398D0` | `0x139B4C` |
| process-exit destructor | `0x6CB554` | `0x592EFC` | `0x100101068` | `0xFE1C2` |
| static-init bundle | `0x42F1F8` | `0x3016E8` | `0x10014FC74` | `0x151C98` |
| `ResourceManager::load` | `0x6A616C` | `0x57B338` | `0x1001012D8` | `0xFE40C` |

参考文件均 stripped。新恢复的 helper/global 名称继续使用 `_guess`，不声称恢复作者符号。
Android 的析构地址已有标准库导出/识别名；iOS 两端是本轮按职责命名的本地实例。

## 4. data-xref 拓扑

按 object 内每个 pointer-sized/word-aligned候选地址重新查询全部 data xref 后：

| 目标 | raw data xrefs | 归一后直接触达 global 的语义函数 |
|---|---:|---:|
| Android arm64-v8a | 10 | 3 |
| Android armeabi-v7a | 16 | 3 |
| iOS arm64 | 4 | 3 |
| iOS armv7 | 9 | 3 |

四端的三类直接 consumer 都是：

1. static-init/`atexit` registration；
2. process-global replacement wrapper；
3. `ResourceManager::load`。

Android raw 数量较多是 ADRP/ADD、MOVW/MOVT、字段级 manager/invoker 访问和 literal-pool
引用造成；iOS 的 copy/swap/destroy helper 接受 base pointer，内部通过固定 offset 访问，故不会
额外产生对 global 绝对地址的 xref。按函数归一后没有第四类消费者。

## 5. Android/libstdc++ 内部容器

四端反编译与对象大小共同恢复出 Android 两个 ABI 的等价布局：

```cpp
// AArch64: sizeof == 32
struct StdFunctionOwnerFilterLibstdcxxA64_guess {
    unsigned char erasedTarget[16];
    void *manager;
    void *invoker;
};

// ARM32: sizeof == 16
struct StdFunctionOwnerFilterLibstdcxxA32_guess {
    unsigned char erasedTarget[8];
    void *manager;
    void *invoker;
};
```

`manager == nullptr` 是 empty discriminator。copy path 的顺序是：

```text
temporary.manager = null
if incoming.manager != null:
    incoming.manager(temporary, incoming, op=2)   // clone erased target
    temporary.invoker = incoming.invoker
    temporary.manager = incoming.manager          // last publication
```

只有 clone 成功后才把 manager/invoker 发布到 temporary；失败时 replacement 尚未触碰
process-global object。随后 wrapper 交换完整 buffer、manager 和 invoker，取出的旧 manager
以 `op=3` 销毁旧 erased target。

arm64 把 copy 与 swap 全部内联进 wrapper；armv7 把 copy 提取为独立 helper、swap 仍内联。
static initializer 只显式执行 `global.manager = nullptr`，再向 `__cxa_atexit` 登记
`std::_Function_base::~_Function_base(&global)`。invoker/target buffer 的初始零来自 BSS，不是
额外的源级清零 loop。

## 6. iOS/libc++ 内部容器

iOS 两端恢复为：

```cpp
// AArch64: sizeof == 32
struct StdFunctionOwnerFilterLibcxxA64_guess {
    unsigned char inlineStorage[24];
    void *target;
};

// ARM32: sizeof == 20
struct StdFunctionOwnerFilterLibcxxA32_guess {
    unsigned char inlineStorage[16];
    void *target;
};
```

active-target pointer 有三态：

```text
target == null          empty
target == object base   callable lives in inlineStorage
otherwise               target points to heap callable
```

copy helper 对三态分别执行：

- empty：destination.target = null；
- inline：先把 destination.target 指向 destination 自身，再经虚表 clone-into 到它的
  inline storage；
- heap：经虚表 clone 分配/复制新 heap callable，再发布返回指针。

swap helper 覆盖 inline/heap/empty 的所有组合；关键不是简单交换 pointer，而是在 inline
对象移动后把 self pointer 重新绑定到新的 object base。replacement body 的源级形状是：

```cpp
StdFunctionOwnerFilter temp(incoming); // may throw; global untouched
temp.swap(global);
temp.~StdFunctionOwnerFilter();        // destroys former global target
return &global;
```

arm64 虚表 slot byte offsets为 heap-clone `+16`、inline-clone `+24`、inline-destroy `+32`、
heap-destroy `+40`；armv7 按 4-byte pointer 缩放为 `+8/+12/+16/+20`。static initializer 只把
active-target pointer 清零并登记专用 dtor。dtor 对 `target == self` 调 inline destroy，对
non-null external target 调 heap destroy/delete，对 null 不操作。

## 7. source-level replacement、所有权和异常边界

四端共同恢复的 source-level 结构仍应写成标准 C++，不能把上述实现细节硬编码到 portable
源码：

```cpp
PSB::PSBFile::OwnerFilter emotePSBDecryptFilter;

void replace(const PSB::PSBFile::OwnerFilter &incoming) {
    emotePSBDecryptFilter = incoming;
}
```

生命周期/data flow 是：

```text
zero initialization
  -> std::function default construction + atexit registration
  -> empty filter is visible to any early ResourceManager::load
  -> setSeed or setFunc builds a local OwnerFilter
  -> copy-before-swap replacement
  -> global is borrowed by every later ResourceManager::load
  -> next replacement releases former target before returning
  -> final global target released at process/static teardown
```

因此：

- filter 不属于任何 `ResourceManager` instance；销毁 manager 不会重置它；
- seed setter 与 function setter 写同一个 slot，互相替换会释放对方的旧 target；
- function target 捕获的 TJS Object/ObjThis owner 会跨 manager instance 长寿存在；
- incoming copy/clone 抛异常时 global 不变；
- `load` 不是把 global 复制到局部后再调用，而是把 global lvalue 直接传给 PSB loader；
- 没有并发保护。setter/load 或两个 setter 并发访问该 object 是普通 C++ data race；这是
  四参考共同边界，不应引入本地 mutex、RCU、atomic shared pointer 或 snapshot copy。

## 8. 与 `commandKey`、random hint 和源结构的边界

上一轮已经证明 `commandKeyMemberHint_guess` 是六类 dispatch consumer 共用的独立
`tjs_uint32`。本轮进一步证明：

```text
commandKey end
    != another member hint
    -> optional ABI padding
    -> process-global OwnerFilter std::function
    -> optional tail padding
    -> g_randomMemberHint_guess
```

本地 `RuntimeSupport.cpp` 为 portable 复用集中定义很多 hint，所以文本上 `commandKey` 后是
`mtx`。这只是本地 consolidation，不是参考 BSS 排列的声明。源码注释已明确此边界，避免后续
分析把 portable declaration order 当成 recovered native source order。反过来也不能仅凭
reference BSS adjacency 把 ResourceManager 的 OwnerFilter 搬进 MotionDispatch hint block；
它的语义 owner、setter/load 数据流和 static destructor 都清楚地属于 PSB resource subsystem。

## 9. 本地落点

生产控制流原已使用正确的 process-global `OwnerFilter`，本轮无需修改算法，只补充新证明的
边界：

- `ResourceManager.cpp`：记录 default-empty、跨 instance 生命周期、copy-before-swap 强异常
  保证、direct-lvalue load 和无同步/data-race；
- `MotionDispatch.h` / `RuntimeSupport.cpp`：记录 `commandKey` 的物理后继不是另一个 hint，
  portable 集中声明顺序不等于 reference BSS source order；
- `motionplayer_psb_decrypt_filter_boundary_four_binary_2026-08-15.md`：补入本轮内部容器、静态
  生命周期和并发边界；
- `motionplayer_render_source_key_shared_hint_read_write_family_four_binary_2026-08-17.md`：补入
  后继 candidate 的最终裁决。

这些均为注释/证据增强，不改变 portable `std::function` 表达或运行时 ABI。

## 10. Recovery IDB 回写

四份 recovery IDB 均完成：

- 新建 4 个 ABI-specific struct type；
- 把匿名 BSS region 建成 size 32/16/32/20 的
  `emotePSBDecryptFilter_guess` typed data item；
- 新恢复 9 个 `_guess` helper 名：Android armv7 copy 1 个、iOS 两端各 4 个；
- 向 replacement/copy/swap/destroy 应用精确 typed prototype；Android arm64 的
  `ResourceManager::load` 继续保留 X0/X1/X8 hidden-result `__usercall` ABI；
- 写入 31 条 data/gap/init/load/replacement/helper 注释；
- 添加 4 个 V187 bookmark；
- 对 22 个相关 function view 执行 force-recompile/readback；
- fresh pseudocode 已直接显示 `manager`、`invoker`、`target == self/null/heap` 和新 global
  名称；
- 四库均已原位保存成功。

## 11. 工程验证

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两套完整测试 TU syntax-only 均 exit 0；只有仓库
  既有 `_tss` deprecated warning；
- Web Debug 与 Wasmtime/Headless Debug full link 均 exit 0；只有既有 `_tss`、imagepacker
  `nodiscard` 和 Emscripten linker warning；
- Web wasm 为 `85,647,577` bytes、539 imports / 69 exports；Headless wasm 为
  `84,994,718` bytes、538 imports / 69 exports；
- 两份 Wasm 均由当前 Node `WebAssembly.Module` 成功解析；
- `llvm-objdump -h` 的 TYPE/IMPORT/FUNCTION/TABLE/TAG/GLOBAL/EXPORT/START/ELEM/
  DATACOUNT/CODE/DATA/name/target_features 全表均与 V186 相同。关键值仍为：Web
  FUNCTION `0x1BD24`、GLOBAL `0xD5B2`、CODE `0x1A407D5`、DATA `0x5A3F37`、name
  `0x3184928`；Headless FUNCTION `0x1BA43`、GLOBAL `0xD5DA`、CODE `0x19E8783`、
  DATA `0x5A1187`、name `0x31407BE`；
- 两配置 CTest 均未注册测试但 exit 0；
- `git diff --check` exit 0，仅报告现有 LF/CRLF 提示。

因此所有可比 byte-size、section-size 和 import/export ABI 指标相对 V186 均为零变化，符合
本轮仅增加注释/证据而不改变 production 语义的预期。
