# Follow-up：114-entry stack-frame / local-lifetime surface 闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 114 个 MANIFEST FDE 精确分为 **57 个有栈帧 + 57 个无栈帧**。
- 57 个有栈帧入口中，**52** 个在函数入口建帧；另外 **5** 个只在诊断/转换慢路径
  shrink-wrap 建帧，不能把这 5 个 16-byte call frame 误读成入口作用域 C++ 对象。
- **39/39** 个 LSDA-bearing 函数都有栈帧；另外有 **18** 个 framed-but-unwind-only
  函数。于是 75 个 unwind-only FDE 又精确分成 `57 frameless + 18 framed`。
- **31** 个函数调用 `__stack_chk_fail@0x406D70`；其中 27 个带 LSDA，4 个只有
  unwind 元数据。栈保护与析构 cleanup 是两条独立证据，不能互相替代。
- 57 个 framed 函数全部建立 `X29` frame pointer；callee-saved GPR 恰有 10 种集合。
  SIMD callee-save 只有一处：`CreateVariant_guess@0x59673C` 保存 `D8`。
- fresh IDA 反编译、原始 AArch64 指令和本地源码局部对象逐项对照后，**没有新增
  `cpp/` GAP**；本轮不修改 `cpp/`。

## 机械取证方法

本轮同时使用两条独立路径：

1. IDA 逐个读取 114 个函数的 frame size，并扫描完整 FDE，而不是只看入口前几条
   指令。这样能捕获 5 个 shrink-wrapped 分支帧。
2. `verify_elf_surface.py` 直接从权威 ELF 解码 AArch64 指令：
   - `SUB SP, SP, #imm`；
   - `STP Xt1, Xt2, [SP, #negative]!`；
   - `STR Xt, [SP, #negative]!`；
   - 所有 SP-relative callee-saved `X19..X30` / `D8..D15` store；
   - `ADD X29, SP, #imm` frame-pointer setup；
   - direct `BL __stack_chk_fail@0x406D70`。

verifier 为每个 framed owner 固定：allocation 地址、32-bit 指令字、解码后的 frame
byte 数、GPR/SIMD save mask、canary presence。其余 57 个 MANIFEST owner 必须完全没有
可识别 allocation/save/frame-pointer/canary；因此这不是只检查聚合计数。

## Frame-size 与 allocation 形状

| frame bytes | 函数数 |
|---:|---:|
| `0x00` | 57 |
| `0x10` | 9 |
| `0x20` | 10 |
| `0x30` | 4 |
| `0x40` | 5 |
| `0x50` | 13 |
| `0x60` | 5 |
| `0x70` | 4 |
| `0x80` | 2 |
| `0x90` | 3 |
| `0xA0` | 1 |
| `0x130` | 1 |

allocation 指令族也被精确固定：

| 形状 | 数量 | 含义 |
|---|---:|---|
| `SUB SP,SP,#imm` | 32 | 有显式 local/spill 区的较大帧 |
| pre-index `STP` | 12 | 以第一对 GPR save 同时分配帧 |
| pre-index `STR` | 13 | 以第一只 GPR save 同时分配帧 |

这里的 frame byte 数是 Android NDK/ARM64 编译产物，不是要写进 portable C++ 的对象
偏移或 `sizeof` 契约。源码复原关注的是对象种类、构造时点、析构顺序和调用边界。

## 五个 shrink-wrapped 慢路径

| owner | allocation | fresh Android ARM64 证据 | 本地源码语义 |
|---|---|---|---|
| `0x5975E0` `GetCount` | `0x597668` | 只有未知 raw tag 进入内部错误诊断时才保存 `X29/X30` | `main.cpp` 的 `GetCount` 调用共享 classifier；正常 count 分支没有 RAII local |
| `0x598B58` `GetString` | `0x598BCC` | 非字符串正常分支直接返回；未知 tag 的诊断分支才建帧 | `PSBRawFile.cpp` 的 category gate + packed string view，不引入 owning local |
| `0x5992E8` `GetDouble` | `0x5993D0` | scalar/float/double 分支均 leaf-return；默认转换错误分支才建帧 | `DecodeNumberAsDouble_guess` 保留相同 default diagnostic |
| `0x599438` `GetInt` | `0x59950C` | 正常 tag 直接返回；默认转换错误分支才建帧 | `PSBRawNode::GetInt` 的 default 直接调用同一诊断 helper |
| `0x599554` `GetTypeCategory` | `0x5995B8` | 已知 tag 全部直接返回；未知 tag 才建帧并报告内部错误 | `GetTypeCategory_guess` 保留同一分类、诊断和返回 `-1` continuation |

这五个函数都没有 LSDA。fresh decompile 显示它们的 16-byte frame 只为保存 call ABI 的
`X29/X30`，不是隐藏的 `ttstr`、`tTJSVariant`、容器或 owner 对象。把它们提升成入口帧或
据此新增源码 local 都会把编译器 shrink-wrap 误当成源码 token。

## Callee-saved register surface

| 保存集合 | 函数数 |
|---|---:|
| `X29,X30` | 9 |
| `X19,X29,X30` | 9 |
| `X19..X20,X29,X30` | 5 |
| `X19..X21,X29,X30` | 10 |
| `X19..X22,X29,X30` | 7 |
| `X19..X23,X29,X30` | 5 |
| `X19..X24,X29,X30` | 3 |
| `X19..X25,X29,X30` | 3 |
| `X19..X27,X29,X30` | 2 |
| `X19..X30` | 4 |

唯一 SIMD spill 是：

```text
0x59673C  SUB SP, SP, #0x60
0x596740  STR D8, [SP,...]
```

fresh `CreateVariant_guess@0x59673C` 反编译显示 `D8` 承载默认 `0.0` / float / double
转换值，随后传给 double Variant assignment。对应本地 `CreateVariant_guess` 的 category-3
`DecodeNumberAsDouble_guess(node)` 后写入 `tTJSVariant`；没有第二只 FP local 或额外 SIMD
容器可复原。

## Stack canary 与 LSDA 的交叉关系

| 分类 | 数量 |
|---|---:|
| canary + LSDA | 27 |
| canary + unwind-only | 4 |
| LSDA 总数 | 39 |
| framed + unwind-only 总数 | 18 |
| frameless + unwind-only 总数 | 57 |

四个 canary-but-unwind-only 函数是：

- `std::vector<std::string>::reserve@0x599174`：emitted STL capacity/move path；没有
  需要本函数 LSDA 清理的 owning stack object。
- `PSBMedia::CheckExistentStorage@0x5998C4`：fresh decompile 只有 by-reference
  `uint32_t size`，并保持 `EnsureContainer && GetResourceData != nullptr` 短路。
- NCB root `PropGet@0x59B28C`：stack 上是 POD params functor/converter storage；fresh
  decompile 保持 NativeInstanceSupport → member-pointer invoke 链。
- NCB root `PropSet@0x59B378`：同类 POD params functor + 单参数槽；read-only setter 的
  null member-pointer gate 没有 owning C++ local cleanup。

这证明 `stack canary != RAII lifetime`；也解释了为什么不能用一次 `__stack_chk_fail` xref
反推 `ttstr`/Variant/容器对象。

## 源码局部对象与 LSDA 对照

以下是较大的 lifetime family；逐函数 call-site landing 拓扑仍由既有 39-entry LSDA
manifest 精确约束：

| Android owner | frame | 反编译中的局部对象/生命周期 | 本地对照 |
|---|---:|---|---|
| `EnumMembers@0x596F50` | `0x130` | 四只 Variant、三参数数组、dictionary `std::string`；逆序 cleanup | `main.cpp` 保留 `name/memberFlags/memberValue/callbackResult`、`params[3]` 和分支内 `key` |
| `PSBFile::Load@0x598268` | `0x70` | by-value Variant、path `ttstr`、filter/owner 过渡；异常路径清理当前存活对象 | `PSBRawFile.cpp` 保留 string/octet 两路、MDF buffer 与 `Adopt` 边界 |
| `LoadStorage@0x598538` | `0x60` | placed-path `ttstr`、owned stream、raw data；read 抛出只析构 stream | 本地保留 `unique_ptr<tTJSBinaryStream>`，并故意不把 data 改成 RAII buffer |
| strict/bool dictionary lookup `0x598C58/0x598D58` | `0x50/0x50` | retained raw-node result、诊断 `ttstr` 或 output owner 替换；cleanup 层次不同 | 本地分成 strict return-by-value 与 bool output 两个独立方法 |
| `GetDictionaryKeys@0x598E64` | `0x80` | returned vector、复用 string、packed views；reserve/emplace 慢路径 | 本地保留 `vector<string> result`、`string key` 及 dead offsets view |
| `ContainsDictionaryKey@0x5995D8` | `0x50` | raw-node temporary 在 category gate 前构造并在每个出口释放 | 本地同样先构造 `PSBRawNode value` 再分类 |
| `GetListAt@0x5999F4` | `0x90` | raw-node output、dictionary string、逐项 ttstr 转换 | `PSBMedia.cpp` 保留 container/list 分支及 dead offsets view |
| `EnsureContainer@0x599E04` | `0x60` | container `ttstr`、native file、adaptor dispatch、temporary Variant | 本地保留 `container/file/object/nextFile` 的 publish 次序 |
| `GetResourceData@0x59A0B4` | `0x50` | local raw node；仅 Resolve 成功后读取 resource | 本地同一 `value` 对象和短路出口 |
| `Resolve@0x59A4B0` | `0x80` | root/current raw node、rest/segment ttstr、narrow holder、strict-result temporary | 本地保留分段 scope、AddRef/Release no-op token 与成功尾部 output commit |
| NCB registration/adaptor `0x59A330..0x59B48C` | `0x40..0x90` | class/adaptor holders、params functor、result Variant、诊断 ttstr | 本地 NCB class/property/factory 声明生成同一 wrapper family |
| load wrapper `0x59B570/0x59B708` | `0xA0/0x70` | params functor、argument/result Variant copy chain | 本地 `Method("load", &PSBFile::Load)` 与 by-value `Load(tTJSVariant)` 保留该生命周期 |

未发现以下任何新增证据：额外 owning 容器、不同的 owner publish 顺序、被省略的 local
Variant/ttstr、额外 catch scope，或应从共享 helper 改成手写 wrapper 的调用边。因此
stack/lifetime surface 结论为 `ALIGNED`，不是因为 frame byte 数偶然相同，而是因为对象
种类、构造时点、析构 landing 与本地数据流能逐层对应。

## 可复现门禁

```bash
python3 -m py_compile \
  analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py

python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump \
  /Users/bytedance/Developer/emsdk/upstream/bin/llvm-dwarfdump
```

新增输出：

```text
stack_frame_surface=true functions=114 framed=57 frameless=57 entry_frames=52 shrink_wrapped=5 canaries=31 lsda_frames=39 unwind_only_frames=18 gpr_patterns=10 simd_spills=1 allocation_words=true saved_registers=true
```

完整 verifier 仍同时通过 FDE、static objects、literal、switch、direct/indirect call、
external callee、39 张 LSDA/232 call-sites 与 raw LSDA 子集门禁。
