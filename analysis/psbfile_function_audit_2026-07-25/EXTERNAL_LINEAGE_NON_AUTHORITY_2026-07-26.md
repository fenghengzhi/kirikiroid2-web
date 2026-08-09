# 外部 `psbfile.dll` 谱系旁证与不可采信边界

## 用途

本文件只记录一次已穷尽的辅助线索调查，防止后续重复把 Windows DLL、公开移植或
同产品字符串误当成 Android `libkrkr2.so` 的源码权威。114 个函数报告、六维判定和
任何 `cpp/` 修改仍只能由 Android 二进制本身支撑；本文件不参加 verifier 的 verdict
统计，也不能单独把 `_guess` 名、源码 token 或 helper factorization 提升为已证明事实。

## 本地样本身份

| 样本 | SHA-256 | 格式 | 内嵌 PDB 路径 |
| --- | --- | --- | --- |
| 千恋万花 `plugin/psbfile.dll` | `fc4fa66cdfbb1a9b2670738d5aa798e97973fbbda0c79da43a37f405e894e464` | PE32/i386 | `C:\dwork\m2\m2tools\plugins\psbfile\Release\psbfile.pdb` |
| DRACU-RIOT! `plugin/psbfile.dll` | `465abd6c0483a7df7b4d7818b844378b23c6436585688b09a84ba79fd13cd0eb` | PE32/i386 | `c:\dwork\game\pc2_dracuriot\plugin\psbfile.pdb` |

两份文件的 hash、PE 类型、PDB 路径、`PSBDispatch` MSVC RTTI 以及
`psb: can't convert value to {bool,long int,double}.` 字符串均由本地只读静态扫描
独立确认。它们强力证明同一 M2 产品/代码谱系，但不证明与 Android 目标使用同一源码
revision；两份 Windows 样本彼此的注册表面、RTTI 继承关系和构建年代本身也已分叉。

## 与 Android 的决定性差异

| 主题 | Windows 2016 静态形状 | Android `libkrkr2.so` 直接证据 | 证据结论 |
| --- | --- | --- | --- |
| packed width | 四字段非多态 record 的独立 accessor 只接受 width 1..4，其他值抛 internal error | `0x59641C`、`0x59659C`、`0x596BC4`、`0x596C70`、`0x5996E4` 等接受 width 1..5；无效 width 按各 caller 返回 0/base | 算法同源，但损坏边界与 revision 不同 |
| numeric tag `0x0B` | 56-bit 路径带 signed 语义 | `0x5992E8` 保留不扩展 bit 55 的 56-bit 位型，再转 double | 不能用 Windows decoder 恢复 Android 源码 token |
| integer getter | Windows 错误文案和 accessor 为 `long int` 路径 | `0x599438` 的消费者与 W-register 数据流证明返回 signed 32-bit `int` | ABI/接口已发生版本或平台分叉 |
| resource helper | data pointer 与 length 是两个独立 helper | `0x596C70`、`0x5996E4` 是 pointer + `uint32_t` size-out 的单一 helper；`0x59A0B4` 又内联同形解码 | Windows 不能证明 Android 的 helper factorization |
| name decoder | 同源三表回溯，但 scratch 是 MSVC SSO `std::string`，并保留额外 v1 分支 | `0x597B1C` 的三指针增长、reverse、assign 与 EH 路径证明 scratch 为 `std::vector<char>` | 容器与分支拓扑不是同一源码快照 |

Windows record 的 `nBytes/count/width/values` 四个概念量可作为发现 Android 分析目标的
线索，但其类型名、字段名、字段顺序、显式构造器和 `operator[]` token 在 Android 中仍被
SROA/内联抹除。尤其不能用 MSVC x86 的 load/fault 顺序替代 AArch64 目标的直接证据。

## Android 编译器线索的限制

Android IDB 在 `0x1582400` 内嵌了一份 OpenCV 3.0.0 build configuration，字面记录
OpenCV 子组件使用 Android NDK r10e、`aarch64-linux-android-g++ 4.9`、gnustl_static 和
`-O3`。权威 ELF 的 `.comment` 又同时保留 `GCC 4.9.x 20150123`、`GCC 4.9
20140827/20150123`、Android Clang 3.8 和 Android Clang 5.0 标记，证明最终 `.so` 含多个
工具链产生的对象。这些信息不能把某一个编译器或命令唯一归属到 psbfile translation
unit。因此本轮没有把现代 Clang 或任意 GCC comparator 的输出升级为 psbfile 源码事实。

## 对审计的最终影响

- 可由外部线索关闭的 Android `EVIDENCE_LIMITED`：**0**。
- 可由外部线索证明的 Android `HAS_GAP`：**0**。
- 生产代码改动：**无**。
- 114 函数统计保持 `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。

要突破这些源码结构上限，需要同源未优化/DWARF 构建、Android 对应源码，或能由
Android 二进制独立建立 exact type/helper 边界的新符号证据；继续重述现有 O3 标量化
机器码或采用不同 revision 的 Windows 实现都不能提高证据等级。
