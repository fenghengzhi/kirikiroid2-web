# Follow-up：raw `PSBFile` holder 的 hidden-sret 与所有权移动 oracle

日期：`2026-08-03`。本轮只使用 Android arm64 `libkrkr2.so` 与仓库已有、未修改的
`reference/xp3/logo_test/m2logo.mtn`，把 `PSBFile::GetRoot` 和 stripped transfer helper
的 raw holder 生命周期接入设备 oracle。没有修改 `cpp/` 生产实现，没有生成或改写
fixture，也没有构建 APK 或额外二进制产物。

fresh Android 证据与当前生产实现一致，未发现新的确定 GAP；审计统计仍为
`99 ALIGNED / 15 EVIDENCE_LIMITED / 0 HAS_GAP`。`0x598A64` 的精确 helper 名与源码 token
仍受 stripped/O3 证据上限约束，本 oracle 不把运行时行为一致误写成命名证据。

## fresh Android arm64 证据

本轮重新反编译并核对完整指令：

- `PSBFile_GetRoot_guess @ 0x598A3C`
- `PSBFile_Transfer_guess @ 0x598A64`
- one-pointer holder release helper `sub_695CBC @ 0x695CBC`

不超过 10 行的关键伪代码：

```text
GetRoot(self X0, result X8):
  owner=self.owner; entries=owner.header.entries   // 无 null guard
  result.owner=owner; if owner: owner.refCount++
  result.node=entries
Transfer(source X0, result X8):
  owner=source.owner; result.owner=owner
  if owner && owner.refCount==0: destroy owner.raw; delete owner
  source.owner=null                                // 正常 owner 不增减 ref
Release(holder X0): if owner && --owner.refCount==0: owner.dtor(); delete owner
```

关键指令边界：

- `0x598A3C` 的普通参数在 `X0`，16-byte `PSBRawNode` 隐式结果在 `X8`；先经
  `owner + 8 -> header + 0x40 -> entries` 取 root，再写 owner、条件执行一次 32-bit
  retain、写 node。
- `0x598A64` 的普通参数仍在 `X0`，8-byte `PSBFile` 隐式结果在 `X8`；正常
  `refCount==1/2` 路径只复制指针并清空 source，没有 AddRef/Release。入场 refcount 为零的
  删除分支保留为边界证据，但本轮不制造非法 owner 去触发。
- `0x695CBC` 读取 holder 首指针并递减 owner；减到零才调用
  `PSBRawOwner_dtor_guess@0x598B3C` 与 `operator delete`。函数没有把 holder 槽位置零。

## 本地生产实现对照

- `cpp/plugins/psbfile/PSBRawFile.cpp` 的 `PSBFile::GetRoot` 返回
  `{*this, owner_->GetHeader()->entries}`；`PSBRawNode(const PSBFile&, node)` 先通过
  `PSBFile` holder 取得一次 owner 引用，再保存同一 entries 指针，对应 Android 的
  `{owner,node}` 与一次 retain。
- 同文件 `PSBFile::Transfer_guess` 使用 Rule-of-Three 表达式
  `PSBFile result(*this); *this = PSBFile(); return result;`。Android O3 可见边界是
  hidden-sret 复制、正常路径净引用数不变、source 清空，并保留入场零引用删除分支；当前
  表达式与该可见数据流、对象生命周期和边界一致。
- `PSBRawFile.h` 的 holder 仍只有一个 intrusive owner 指针，析构只在非空时 Release；
  没有 `shared_ptr`、额外容器或旁路所有权。

因此本轮不改生产代码；新增 oracle 固定此前只能由反编译/源码对照证明、但尚未经过设备
调用边界观察的 raw holder 生命周期。

## harness hidden-sret 通道

`tests/differential/oracle_runner/harness/harness.cpp` 新增：

```text
CALL_SRET <fn_hex> <out_hex> <size_dec> <nints> <int_hex>*
```

harness 现使用一个恰好 32 字节、带 user-provided 空析构的返回类型。该类型属于
non-trivial-for-calls，AAPCS64 因而由编译器把临时结果地址放入 `X8`，最多八个普通整数
参数仍位于 `X0..X7`。调用结束后仅把请求的 1..32 字节复制到 guest 输出槽；本报告的
holder 调用仍只请求 8/16 字节。空析构刻意
不解释或释放返回的 owner 指针；Python adapter 接管复制出的每个 holder，并调用 Android
原始 `0x695CBC` 恰好释放一次。

这不是手写寄存器猜测：本机 arm64 对完整 `harness.cpp` 的 `-O2` 汇编在该间接调用处
明确生成：

```asm
add x8, sp, #288
blr x28
```

随后才从同一临时槽 `memcpy` 到 RPC 指定地址。当前未配置 legacy Android NDK，故这项
记录为 AAPCS64/完整 harness 静态验证，不冒充 Android 构建或设备运行结果。工作区已有
prebuilt 均为 `arm64-v8a`，但属于修改前产物且不含 `CALL_SRET`；设备执行前必须先用
`build_legacy.sh` 重建 `libharness.so`，再按既有流程重新打包 harness APK。

## 天然样本与断言

输入 SHA-256：
`4382de8283cc0782fd269b16d3157bf3a9ec28916440f9192690eb178c0c18fe`。

`run_raw_holder_lifecycle_case` 执行：

1. 用未修改的 `m2logo.mtn` 经 `PSBFile.load(octet)@0x598268` 得到 refcount=1 的 owner；
   同时独立从原始 PSB header `+36` 解码 entries offset，要求 inline header 的 entries
   指针等于 `owner.raw + offset`。
2. 以 16-byte sentinel 输出槽调用 `GetRoot@0x598A3C`，要求完整覆盖为
   `{same owner, expected entries}`，root tag 与天然字节相同，owner refcount `1 -> 2`。
3. 以 8-byte sentinel 输出槽调用 `Transfer@0x598A64`，要求结果 owner 不变、source
   holder 清零、owner refcount 保持 2。
4. 对一个天然空 holder 再调用 Transfer，要求结果和 source 都为零；不制造
   `owner.refCount==0` 的非法对象。
5. 用 `0x695CBC` 先释放 GetRoot 结果，要求 refcount `2 -> 1` 且 helper 不清空槽；再
   释放 Transfer 结果触发 terminal owner 析构，且第二个槽同样保持原指针位模式。异常
   路径按实际完成的所有权移动状态回收每个已知引用，避免 double release。

`--shape-boundary --trace` 的 shape 目标由 51 扩为 53 个唯一地址，新增
`0x598A3C/0x598A64`；`ADDR_NAMES/ARG_COUNTS/RETURN_KINDS` 三张映射均完整。hidden-sret
两入口的普通参数计数均为 1，return kind 记为 `void`，因为可观察结果来自 `X8` 而不是
`X0`。既有 `PSBRawOwner_dtor@0x598B3C` trace 目标继续观察 terminal cleanup。

## 当前验证

- 四个相关 Python 文件 `py_compile`：通过。
- `AdbHarnessEngine.call_sret` 协议编码 smoke：通过；负整数按 u64 编码，四组
  address/size/arg-count 边界全部拒绝。
- adapter 控制流 smoke 使用未修改的真实 `m2logo.mtn` 字节与内存模型跑通
  `entries=0xB4B`、root tag `0x21` 及 `1 → 2 → 2 → 1 → terminal`；它只验证 host
  断言/cleanup 编排，不替代 Android oracle。
- 完整 `harness.cpp` 在本机 arm64、`-std=gnu++11 -O2 -Wall -Wextra`：编译通过；汇编
  明确在间接调用前设置 `X8`。
- shape trace：53/53 地址唯一，三张元数据映射无缺项。
- legacy Android NDK 当前未配置；既有 arm64 prebuilt 不含 `CALL_SRET`，本轮未把宿主
  编译结果冒充 ABI-compatible Android 产物，故尚未重建/重打包。
- 后续当前 legacy-ABI ARM64 harness 已重建并重打包；真实 Android
  `--shape-boundary` 的 raw holder、Dictionary 与 key-vector 路径在无 trace 和全量 trace
  中全部 `ok`。固定产物哈希与事件数见
  [FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md](FOLLOWUP_ANDROID_ARM64_RUNTIME_ORACLE_2026-08-03.md)。

后续同一 hidden-sret 通道已扩展到 raw Dictionary strict/non-strict lookup、两级 miss、
destination overwrite 与 `self==out` alias；shape trace 由 53 扩为 59。详见
[FOLLOWUP_RAW_DICTIONARY_LIFECYCLE_ORACLE_2026-08-03.md](FOLLOWUP_RAW_DICTIONARY_LIFECYCLE_ORACLE_2026-08-03.md)。
随后 carrier 扩到 32 字节，以承载 24-byte gnustl key vector；shape trace 扩为 63，见
[FOLLOWUP_RAW_DICTIONARY_KEYS_VECTOR_ORACLE_2026-08-03.md](FOLLOWUP_RAW_DICTIONARY_KEYS_VECTOR_ORACLE_2026-08-03.md)。
