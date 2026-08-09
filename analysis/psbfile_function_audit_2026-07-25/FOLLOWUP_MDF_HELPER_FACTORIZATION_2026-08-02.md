# Follow-up：共享 MDF 解压 helper 因子化闭环

日期：`2026-08-02`。本文件记录先在 iOS 1.3.9 同源 PSBFile 簇的 retained-function
普查中发现、再由 Android 权威 arm64 两份完整 inline clone 约束的共享 MDF 解压边界。
唯一行为权威是 `reference/libkrkr2/libkrkr2.so`；iOS arm64 只用于在目标兼容的源码
factorization 候选之间作独立复核。

## Android 1.4.4：两份完整 clone

本轮 fresh IDA MCP `decompile(addr="0x598268")`、`decompile(addr="0x598538")`，并取
完整 137/115 指令 disassembly。两处 MDF 子图分别为：

- `PSBFile_Load@0x598268`：gate `0x5982B0..0x5982C8`，body
  `0x5983DC..0x59841C`；
- `PSBFile_LoadStorage_guess@0x598538`：gate `0x5985E0..0x5985F8`，body
  `0x598668..0x5986B8`。

除 caller 寄存器和成功后的使用方式外，两段完整执行同一算法：

```text
if size < 11 || u32(source) != 0x0066646D: return null
expected = u32(source + 4); actual = expected
decoded = AlignedAlloc(expected, 4)
status = uncompress(decoded, &actual, source + 8, size - 8)
if status != 0: if decoded: operator delete[](decoded); return null
size = u32(actual)
return decoded
```

caller 对 null 的处理不同且必须留在 caller：Octet `Load` 按当前 size 分配并复制原
source；`LoadStorage` 保留原 data，只在 decoded 非空时 `TJSAlignedDealloc(data)` 后替换。
因此“zlib 成功但 decoded 为空”会先写回 actual size，再进入两条不同的 null 路径；共享
helper 不能把 replacement/copy 也吞进去。

## iOS arm64：额外独立共享边界与精确 caller 集

iOS arm64 `0x1000ED5B4..0x1000ED654` 完整实现上述 8 行：`X0=source`、
`X1=uint32 size in/out`、返回 decoded/null。对全 `__text` 反汇编穷举，恰有两条 direct
call：

- `0x1000ED208`：typed `PSBFile::Load` 的 Octet 分支；
- `0x1000ED508`：storage loader。

这组证据证明共同 helper 边界及 `(source, mutable 32-bit size) -> decoded/null` 数据流；
它仍不能恢复 helper 原名、pointer/reference 拼写或 TU/class 内外位置，所以本地使用
`tryDecodeMdf_guess`，不冒充精确 identifier。

## 修改前差异与本地逐项映射

修改前 `PSBRawFile.cpp` 在 `PSBFile::Load` 与 `PSBFile::LoadStorage` 内手写两份 MDF
算法。行为与 Android 相符，但源代码结构和调用链没有复原同源 iOS arm64 保留的共享边界，
属于正证据确认的 factorization GAP；oracle-inert 不是保留重复体的理由。

当前本地：

- `cpp/plugins/psbfile/PSBRawFile.cpp:21-48`：
  `tryDecodeMdf_guess(const uint8_t *, uint32_t &)` 逐行复刻上述 gate、默认 null、
  expected/actual、`align_bits=4`（16-byte）、zlib、条件 `operator delete[]`、成功 size
  写回与返回；
- `PSBRawFile.cpp:461-468`：Octet `Load` 读取借用 source/size，调用 helper，null 时才按
  当前 size 复制原 source；
- `PSBRawFile.cpp:498-506`：storage caller 以 `dataSize` 作为同一个 in/out 对象调用
  helper，只在 non-null 时配对释放/替换 input allocation；
- `PSBRawFile.cpp:470-477` 与 `:508-511`：两条 caller 后续仍分别保持 Octet 的
  Adopt-false delete/throw 和 storage 的 Adopt-false leak，helper 没有改变 owner 交接。

## 六维影响

| 维度 | 闭环影响 |
| --- | --- |
| 源代码结构 | 删除两份手写 MDF 重复体，恢复权威 arm64 完整 clones 与 iOS arm64 共同支持的单一 shared helper；精确 token 仍以 `_guess` 标注证据上限。 |
| 数据流 | 两 caller 都把同一个 mutable `uint32_t size` 交给 helper；成功写回 actual，失败/非 MDF 保持原值，success-null 仍保留已写回值。 |
| 调用链 | 源码层恢复 `Load/LoadStorage → tryDecodeMdf_guess → alloc/uncompress/delete`；权威 arm64 O3 在两 caller 内完整 inline，iOS arm64 保留 direct call。 |
| 对象生命周期 | decoded 仍为 raw allocation；失败只在 non-null 时直接 `delete[]`，storage 只在 helper 返回 non-null 时释放原 input，异常无新增 RAII。 |
| 内部容器实现 | N/A；该边界不建立容器。 |
| 边界行为 | 11-byte gate、完整小写 `mdf\0` u32、`align_bits=4`（16-byte）、32-bit size 写回、success-null 与两 caller 的不同 fallback 全部保持。 |

该闭环不改变 114 个 emitted 入口及 verdict 数量；`0x598268/0x598538` 当前仍为
`ALIGNED`，但其 source factorization 依据从“两个行为相同的手写块”推进到权威 arm64
clones 约束、iOS arm64 独立复核的共享边界。

## 验收

- Mac Debug 与 Release 均重建 `psbfile-dll`、`motionplayer-dll`、
  `motionplayer-ttstr-hash-test` 并运行通过，分别为 `583/10`、`1386/21`、`109/24`。
- Mac Release `PSBRawFile.cpp.o` 中无独立 `tryDecodeMdf_guess` 符号；优化器把源码 helper
  分别内联到 `PSBFile::Load@0x1584` 的 `0x15DC..0x16AC` 与
  `LoadStorage@0x1798` 的 `0x183C..0x189C`。两段都保留 `size<11`、完整 u32 signature、
  allocation/uncompress、失败条件 delete 与成功 size write-back，说明源码级共享边界不会
  阻止目标优化产物恢复 Android 的双 inline-clone 形态。
- Web Debug 完整增量链接成功，`index.wasm=84,490,820` bytes；Wasmtime headless guest
  重新链接成功，`krkr2_wasmtime_guest.wasm=148,876,277` bytes，二者 mtime 均晚于本次
  `PSBRawFile.cpp`。
- `verify_audit.py` 为 `PASS`：114/114 报告齐全，`source_references=1700`，判定仍为
  `ALIGNED:99,EVIDENCE_LIMITED:15`；`git diff --check` 通过。
