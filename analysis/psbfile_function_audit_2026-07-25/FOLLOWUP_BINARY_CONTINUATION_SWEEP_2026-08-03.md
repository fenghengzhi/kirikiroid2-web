# Follow-up：Android ARM64 二进制继续复扫与构建闭环

日期：`2026-08-03`。本轮只以 Android ARM64 `libkrkr2.so` 与当前 IDA 数据库为权威，
继续推进 psbfile 的源码结构、数据流、调用链、对象生命周期、内部容器实现和边界行为
复原。没有使用或恢复 Android/iOS ARMv7 资料，没有访问废弃私库、Git LFS 对象或外部
同版本源码，也没有把这些材料当作继续执行的前置条件。

## 结论

- 对全部 15 个 `EVIDENCE_LIMITED` canonical 入口再次 fresh `decompile`，并针对 packed
  解码、Variant 构造、dispatch 枚举、raw scalar/resource 与 media 生命周期重走目标调用
  链；没有得到证明本地实现偏离 Android 目标的新正证据。
- 额外 fresh 复扫 `PSBRawOwner` 构造/刷新/析构与 `GetDictionaryKeys`，确认当前实现保持
  目标的未初始化边界、signed-64 offset 验证顺序、aligned dealloc 故障边界、旧
  libstdc++ `vector<string>` 三指针 hidden-sret、COW string 复用和异常清理层次。
- 本轮因此没有修改 `cpp/`。总判定保持
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`；`EVIDENCE_LIMITED` 只表示 stripped/O3
  无法唯一恢复部分 C++ token，不是实现阻塞。

## 15 个入口的 fresh 复扫

| canonical 地址 | 目标入口 | 本轮重新锁定的 Android 行为 | 对照结果 |
| --- | --- | --- | --- |
| `0x59641C`、`0x59659C` | `PSB_FindNameIndex_guess`、`PSB_FindDictionaryValueOffset_guess` | packed key/index 解码层次、miss/default 与 caller 契约 | 本地 helper 分层与边界一致；精确 helper token 继续保留 `_guess` |
| `0x59673C` | `PSBValueDispatch_CreateVariant_guess` | void/bool/int/real/string/octet/list/dictionary 分类；helper-return 默认值；String 直接窄串赋值；Resource null gate；List/Dictionary 新 dispatch 与 ref 链 | `main.cpp` 的分类、构造、引用与默认路径一致 |
| `0x596BC4`、`0x596C70` | `PSBValueDispatch_getString_guess`、`PSBValueDispatch_getResource_guess` | String/Resource 专用路径及其在 `CreateVariant` 中的 O3 inline clone | 本地共享 helper 与 caller 展开语义一致 |
| `0x596F50` | `PSBValueDispatch_EnumMembers` | 先构造四个 Variant；Array 十进制 key；Dictionary packed key/value；callback 后重载 `self->node`；`NO_VALUE` 的 2/3 参数分叉；逆序清理 | 本地枚举数据流、重入边界和对象生命周期一致 |
| `0x597B1C` | `PSB_DecodeName_guess` | packed name 状态推进、复用 destination string 与异常清理 | 本地 decoder 调用层次和 COW string 生命周期一致 |
| `0x597AD4` | `PSBValueDispatch_ctor_guess` | dispatch vptr/valid/owner/node 初始化与 owner 引用获取 | 本地字段建立顺序与 retain 生命周期一致 |
| `0x598A64` | `PSBFile_Transfer_guess` | hidden-sret holder 转移、owner retain、source 清空与 terminal release | 本地所有权移动一致；精确返回类型拼写仍不可由 ABI 唯一化 |
| `0x598B58` | `PSBRawNode_GetString_guess` | 非 String 分类返回 null；`0x15..0x18` 读取 1/2/3/4-byte index；`0x2C` 保持 index 0；无效 packed width 取 offset 0；未知 tag 抛固定 internal error；不补 bounds guard | `PSBRawFile.cpp` 保持相同分类、默认值和故障边界 |
| `0x5992E8`、`0x599438` | `PSBRawNode_GetDouble_guess`、`PSBRawNode_GetInt_guess` | narrow/wide integer、raw double helper 分层和 category 默认路径 | 本地 scalar helper 拆分与位宽/截断边界一致 |
| `0x5996E4`、`0x59A0B4` | `PSBRawNode_GetResource_guess`、`PSBMedia_GetResourceData_guess` | resource index/offset/length 读取、borrowed raw data 到 media 输出链和 guard 顺序 | 本地调用链、指针所有权和长度传播一致 |
| `0x599DD8` | `PSBMedia_GetLocallyAccessibleName_guess` | 若 `ttstr` storage 非空则 Release 并置 null；无额外赋值 | 本地 `name.Clear()` 复刻同一对象生命周期 |

这些入口的行为候选已经由目标机器码充分约束；当前不能唯一化的仅是 identifier、
member/free、pointer/reference、`const`、header-inline 或 helper factorization 等源码 token。
继续执行时应直接采用二进制支持最强的 target-compatible 候选并保留 `_guess`，不等待源码。

## 生命周期与容器加深复扫

### `PSBRawOwner`

fresh `decompile` 覆盖：

- `PSBRawOwner_ctor_guess@0x598AAC`：`refCount=0`，写入 `data/size`；`data==null` 时不建立
  header/headerStorage；非空时让 header 指向内嵌 storage，再顺序读取签名、版本、encrypt
  与 8 个 offset。
- `PSBRawOwner_Refresh_guess@0x598960`：无条件重建同一 header view；不验证 offset 时直接
  true；验证时用 signed-64 `size` 依序执行目标的 `>`/`>=` 比较，保留各 offset 边界差异。
- `PSBRawOwner_dtor_guess@0x598B3C`：只把 `data` 交给 aligned dealloc；不判空，也不析构
  header 子对象。deallocator 读取 `data[-1]`，因此 null 输入仍保留目标故障边界。

本地 `PSBRawOwner` 字段、构造/刷新/析构与 `TJSAlignedDealloc` 已逐项覆盖这些步骤，不需
生产修改。

### `PSBRawNode_GetDictionaryKeys_guess@0x598E64`

完整 decompile 加 193 条指令复扫重新确认：

1. 返回值是 hidden-sret 的旧 libstdc++ 三指针 `vector<string>`，入口先清零输出。
2. 只有 category 7 才建立可复用的空 COW string，并构造 packed keys view；offsets view
   虽不消费仍存在于目标数据流。
3. 先 `reserve(count)`；W32 stride 的 packed width 为 1..5，无效 width 令 index 为 0，
   width 5 保留低 8-bit 行为。
4. 每项把同一个 string 交给 `DecodeName`，再按 lvalue copy/emplace 入 vector；扩容走
   `_M_emplace_back_aux<std::string &>`。
5. 正常与异常出口保持 string、packed view、vector 三层清理。

本地实现保持上述容器选型、复用方式、死 view、扩容调用和异常生命周期；未发现 GAP。

## 构建与运行门禁

| 门禁 | 结果 |
| --- | --- |
| Mac Debug `psbfile-dll` 直接执行 | `598 assertions in 11 test cases`，全通过 |
| Mac Release `psbfile-dll` 直接执行 | `598 assertions in 11 test cases`，全通过 |
| Mac Debug `motionplayer-dll` consumer | `1386 assertions in 21 test cases`，全通过 |
| Mac Debug `motionplayer-ttstr-hash-test` | `109 assertions in 24 test cases`，全通过 |
| Web Release `psbfile` | 238-step rebuild 成功，生成 `out/web/release/cpp/plugins/psbfile/libpsbfile.a` |
| Web object 的 Resource null gate | `CreateVariant_guess` 在 `0x5dc0..0x5dda` 先读取并分支检查 `chunkData`；仅 `0x5ddd+` 读取 lengths/offsets，优化器未跨越 gate |
| 114-address 审计门禁 | `reports/tree/manifest=114`，`ALIGNED:99,EVIDENCE_LIMITED:15` |
| ELF/FDE/LSDA 门禁 | 114 个目标 FDE；39 LSDA/232 call-sites=`77 no landing / 80 cleanup / 75 catch-all`；raw 子集 51=`18/16/17` |
| 工作树文本完整性 | `git diff --check` 通过 |

一次包含无关 `tjsString` 的组合 Mac 链接触发现 `libarchive.a` 的 Expat 符号未被链接；这是
既存的非 psbfile 链接配置问题。psbfile、Web psbfile 与 motionplayer 目标均已独立构建/
执行通过，本轮没有借此扩大修改范围。

## 继续执行边界

当前没有证据支持为了这 15 个 token 停止复原或寻找另一份源码。后续继续从 Android
ARM64 二进制本体选择最强源码候选；只有出现新的目标内正证据时才消除 `_guess`。运行时
失败/极端边界仍只接受现成天然输入，不生成或篡改 fixture。
