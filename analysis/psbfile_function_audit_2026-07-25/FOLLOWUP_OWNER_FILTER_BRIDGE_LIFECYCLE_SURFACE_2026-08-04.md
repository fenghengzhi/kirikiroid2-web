# OwnerFilter 跨模块桥 / manager / invoker 生命周期闭环（2026-08-04）

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

本轮从上一轮已经闭合的外部 consumer/函数指针面继续追踪唯一携带非空
`PSBFile::OwnerFilter` 的跨模块路径：

```text
EmotePlayer 两个 setter
  -> TU-static OwnerFilter@0x1AB82E0
  -> ResourceManager_loadResource@0x6A8D8C
  -> PSBFile_LoadStorage_guess@0x598538
  -> PSBFile_Adopt_guess@0x598708
  -> seed/function invoker
```

Android 目标明确使用 32-byte libstdc++ `std::function` 形状：16-byte target storage，随后是
manager 与 invoker 两只 8-byte 指针。对象在 TU 静态初始化期默认构造并登记进程退出析构；
setter 先构造命名临时对象，再 copy-assign 到全局；`LoadStorage`/`Adopt` 全程只转发同一引用；
`Adopt` 是唯一真正执行 invoker 的位置。

本地 `ResourceManager.cpp`、`PSBRawFile.h/.cpp` 已逐项表达相同源码结构、数据流、复制次数、
引用计数与析构顺序。最终判定：**ALIGNED / 无新增生产 GAP**。本轮没有修改 `cpp/`、fixture
或测试物料，因此不触发构建。

## fresh 反编译证据

本轮 fresh decompile：

- `motionplayer_static_init@0x42EE18`；
- `EmotePlayer_setEmotePSBDecryptSeed_callback@0x685D30`；
- `EmotePlayer_setEmotePSBDecryptFunc_callback@0x685E60`；
- seed invoker `0x6863CC` 与 manager `0x686450`；
- function invoker `0x6864C0`、manager `0x6864C8`、call body `0x6865B4`；
- replacement wrapper `0x6A87D0` 与 copy-assignment body `0x6A87E8`；
- `ResourceManager_loadResource@0x6A8D8C`；
- `PSBFile_LoadStorage_guess@0x598538` 与 `PSBFile_Adopt_guess@0x598708`。

关键逻辑压缩为十行：

```text
static init leaves the 32-byte BSS target/invoker empty, clears manager, and registers _Function_base dtor
seed setter converts p[0] to int64, allocates an 8-byte target, and stores all 64 captured bits
seed manager op=2 allocates/copies 8 bytes; op=3 deletes; invoker consumes only the low W32
function setter AddRefs Object/ObjThis into a 16-byte closure and wraps it in a refcount control block
function manager op=2 clones the 8-byte holder and retains the control block; op=3 releases it
replacement wrapper materializes the TU-static object and tail-calls a separate copy-assignment FDE
copy assignment manager-op2 clones source, swaps target+manager+invoker, then manager-op3 destroys old
ResourceManager passes that exact global reference to LoadStorage; LoadStorage forwards it to Adopt
Adopt tests manager, loads invoker, calls invoker(filter, owner), then refreshes the mutated owner header
each setter destroys its temporary independently; process exit destroys the final global target via atexit
```

## 被纠正的 FDE / IDB 边界

旧 IDA 函数曾把 `0x6A87D0..0x6A88CC` 合并成一个函数。原始 `.eh_frame` 给出两个相邻但
独立的边界：

| FDE | 大小 | 实际层级 |
| --- | ---: | --- |
| `0x6A87D0..0x6A87E8` | `0x18` | 六条指令的 TU helper；物化全局地址并 tail-call |
| `0x6A87E8..0x6A88CC` | `0xE4` | 独立栈帧的 `std::function` copy-assignment body |

`0x6A87E8` 有自己的 `SUB SP` 序言、独立 FDE 与异常边界，不能作为 `0x6A87D0` 的普通
basic block。IDB 已按上述边界拆分；第二段标为
`PSBOwnerFilter_copy_assign_guess`，并应用 32-byte `PSBOwnerFilter_arm64` 类型。seed invoker
`0x6863CC` 也补成 void OwnerFilter invoker ABI；TU-static 全局补成相同结构类型。所有新增
名字均保留 `_guess`，没有把行为推断冒充二进制保留的源码 token；修正已保存。

这项拆分没有制造本地源码缺口。`replaceEmotePSBDecryptFilter_guess` 是源码 helper，helper
中的普通 `emotePSBDecryptFilter = filter` 自然调用独立的 `std::function::operator=` 实体；
二进制两只 FDE 正是该源码层级经 ARM64 编译后的结果。

## 32-byte TU-static 对象生命周期

全局对象位于 `.bss` 的 `0x1AB82E0..0x1AB8300`：

| 偏移 | ABI 内容 | 证据 |
| ---: | --- | --- |
| `+0x00..+0x0F` | target storage | copy-assignment 的 `LDR/STR Q` |
| `+0x10` | manager | static init `0x42EF1C` 清零；Adopt 在 `0x598844/0x598948` 读取 |
| `+0x18` | invoker | Adopt 在 `0x598850` 读取并于 `0x598858` `BLR` |

`motionplayer_static_init@0x42EE18` 在 `0x42EF00/04` 直接物化对象地址，在 `0x42EF1C` 清
manager，然后从 GOT 取得 `std::_Function_base::~_Function_base` 并于 `0x42EF2C` 调
`__cxa_atexit`。该初始化器的三条直接调用全部是 `__cxa_atexit`；不存在
`__cxa_guard_acquire/release`，因此不是 function-local lazy static。

完整 `.text` 的精确 ADRP/ADD 扫描只找到三次该全局地址物化：

- static init `0x42EF00/04`；
- replacement wrapper `0x6A87D0/74`；
- ResourceManager load `0x6A8EF0/F4`。

## copy-assignment 与临时析构

薄包装器 `0x6A87D0` 把全局地址放入 `X0`、输入 const 引用放入 `X1`，在 `0x6A87E4`
tail-call `0x6A87E8`。后者的顺序固定为：

1. `0x6A8814` 读取 source manager；非空时以 `W2=2` 在 `0x6A8828` copy-clone target；
2. 取 source 的 manager/invoker，并保留新 target；
3. `0x6A8844..0x6A8850` 读取旧全局 target 与 manager/invoker；
4. `0x6A8854..0x6A885C` 发布新 target 与 manager/invoker；
5. 旧 manager 非空时以 `W2=3` 在 `0x6A8870` 销毁旧 target；
6. 返回全局对象地址。

两个 setter 在 copy-assignment 返回后，又分别于 `0x685E08`、`0x685F5C` 以 manager
`op=3` 销毁自己的临时 `std::function`。所以“copy 到全局后销毁源临时”和“替换时销毁旧
全局目标”是两条不同的生命周期边，不能折叠成 move-assignment。

## seed target

`0x685DC8..0x685DE8` 先分配 8 bytes，把普通 TJS Integer 转换得到的完整 `X19` 写入，
再发布 manager `0x686450` 与 invoker `0x6863CC`。manager 的操作严格为：

- op=1：返回 target 地址；
- op=2：重新分配 8 bytes，复制完整 X64 capture；
- op=3：删除 target。

invoker 从 `owner->header->encryptData` 到 `chunkOffsets` 逐字节异或。只有
`0x6863F8` 在初始化 xorshift 状态时从 8-byte capture 读取低 W32；这不把 setter/copy 的
capture 宽度降为 32 位。长度先截成 signed 32-bit，`<=0` 直接返回；常量与更新顺序为
`123456789, 362436069, 521288629, seedLow32`。

## TJS function target

`0x685E8C..0x685F3C` 的对象图为：

```text
16-byte Object/ObjThis closure
  <- 16-byte pointer + refcount control block
       <- 8-byte std::function lambda target
```

setter 对 Object 与 ObjThis 分别 AddRef；控制块引用数从 2 变为 3，随后全局 copy 又由
manager op=2 克隆 8-byte holder 并增加控制块引用。manager op=3 在引用归零时按
Object、ObjThis、closure allocation、control block、holder 的顺序释放。

invoker `0x6864C0` 只取 holder 中的控制块并 tail-call `0x6865B4`。call body 以
`owner+0x58` data 和 `owner+0x60` 的 W32 size 构造 32-byte `CBinaryAccessor`，建立 object
与 integer 两个 Variant，以 `numparams=2` 调 TJS closure，忽略返回值，再逆序析构两个
Variant。Android 对 accessor 构造器初始引用不做平衡 Release；本地明确保留这一泄漏边界。

## LoadStorage -> Adopt -> invoker

`ResourceManager_loadResource@0x6A8D8C` 在 `0x6A8EF0/F4` 把全局地址放入第三参数 `X2`，
于 `0x6A8F00` 调 `LoadStorage@0x598538`。`LoadStorage` 在 `0x598608` 把原 filter 保存值放入
第四参数 `X3`，于 `0x59860C` 原样调用 `Adopt@0x598708`。

`Adopt` 先验证并安装新 `PSBRawOwner`。旧 owner 是否存在会产生两条控制流，但两条都只读
同一个 `filter+0x10` manager：`0x598844` 与 `0x598948`。manager 非空时二者收敛到：

```text
X1 = self->owner
X8 = filter->invoker     // +0x18
X0 = filter
BLR X8                   // 0x598858
```

invoker 返回后才从可能已被解密的 owner data 重建 header 并执行完整 `Refresh(true)` 边界
检查。空 filter 不执行 invoker，也不 Refresh，直接返回 true。完整 MANIFEST 间接调用面
中 `0x598858` 也是唯一 `owner-filter-invoke` 角色。

## 本地逐行对照

| Android 数据流 | 本地实现 | 对照 |
| --- | --- | --- |
| 32-byte TU-static + atexit | `ResourceManager.cpp:121-123` | namespace-scope `OwnerFilter`，非 lazy accessor |
| wrapper -> copy-assignment | `ResourceManager.cpp:125-131` | const-ref helper 内普通 copy assignment，与两只 FDE 层级一致 |
| seed 8-byte capture/低 W32 使用 | `ResourceManager.cpp:133-166` | 捕获 `tjs_int64`，仅 xorshift 初始化 cast 为 `uint32_t` |
| function closure/control block | `ResourceManager.cpp:169-210` | Object/ObjThis closure + `tRefHolder` + pointer-sized lambda capture |
| 两个命名临时对象 | `ResourceManager.cpp:271-299` | 先 `const auto filter`，再 const-ref replacement，离开函数独立析构 |
| ResourceManager 直接传全局引用 | `ResourceManager.cpp:302-321` | `loaded.LoadStorage(path, emotePSBDecryptFilter)` |
| OwnerFilter 源码类型 | `PSBRawFile.h:77-113` | `std::function<void(PSBRawOwner &)>`，LoadStorage/Adopt 均收 const ref |
| LoadStorage 原样转发 | `PSBRawFile.cpp:482-513` | 独立读取/MDF 路径后 `Adopt(data,dataSize,filter)` |
| Adopt gate/invoke/Refresh | `PSBRawFile.cpp:516-539` | `if(filter) filter(*owner_); return owner_->Refresh(true)` |

没有一项出现 Android 正证据与本地实现不一致，因此没有满足前置条件的 `cpp/` 修改。

## 机械门禁

`verify_elf_surface.py` 新增输出：

```text
owner_filter_bridge_surface=true fdes=13 manifest_fdes=2 external_fdes=11 global_materializations=3 callable_materializations=4 direct_edges=6 manager_calls=4 op2=1 op3=3 invoker_calls=1 semantic_words=32 byte_ranges=9 range_bytes=1948 atexit=3 split_assignment_fdes=true sha256=true
```

门禁固定 6,057-byte canonical surface，SHA-256 为
`c4d35a151afdcca17cc18decdc44d7b6bf650b9a199d0f829ba98d4af0e21f80`，并验证：

1. 13 个相关 FDE 的精确边界，以及 `0x6A87D0/0x6A87E8` 的强制拆分；
2. 32-byte TU-static 对象确实完整位于 writable allocated `.bss`；
3. 全 `.text` 恰好三处全局地址物化，两个 setter 恰好四处 callable 物化；
4. 六条 direct edge 的 BL/B 类型、目标与 exact word；
5. 四次 manager BLR 的目标寄存器和 `1×op2 + 3×op3` 操作数生产；
6. Adopt 两个 manager gate、唯一 invoker load/BLR；
7. 32 个 source-facing semantic word 与 9 段完整局部控制流，共 1,948 raw bytes；
8. static initializer 只有三次 atexit 调用，以及 `_Function_base` destructor 的 GOT relocation。

完整 ELF 门禁通过。结论只使用 Android ARM64 二进制、fresh IDA 反编译、原始 FDE/指令/
relocation 与本地逐行对照。
