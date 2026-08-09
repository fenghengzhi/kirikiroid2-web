# Follow-up：完整数值 ctree 与机器立即数分层

日期：2026-08-03。本轮只分析权威 Android ARM64
reference/libkrkr2/libkrkr2.so；没有修改 cpp/、测试或 fixture。

## 结论

- 114 个 MANIFEST FDE 中共有 1,181 条 Hex-Rays cot_num，分布于 95 个 owner。
  其中 1,133 条有具体 EA，48 条是优化器 synthetic 表达式；后者全部能沿 ctree
  ancestor 找到最近的真实机器锚点。
- 1,181 行折叠为 1,023 个具体 EA 与 1,055 个唯一 realization anchor；1,055/1,055
  全部 normal-entry reachable，landing-only 为 0。956 个锚点只承载一行，99 个锚点
  承载多行，最大 4 行。
- 数值面包含 54 个 unsigned-64 canonical value、22 种直接父表达式、22 种 Hex-Rays
  类型和 39 种 realization mnemonic。完整语义文本为 132,066 bytes，SHA-256 为
  b477202507136c4011ca44f048335feee882ab0efed397a475cbf821cf78f3e0。
- fresh 反编译与本地逐项对照没有发现生产实现 GAP；因此本轮不修改 cpp/，也不触发
  Web 构建。

## 与机器立即数的边界

IDA 指令层在同一 114 个 FDE 中给出 1,208 个 o_imm operand、1,160 个站点；ctree
数值只有 1,023 个具体站点：

| 分区 | 数量 |
| --- | ---: |
| ctree 与 machine immediate 交集 | 485 |
| 只有 ctree 数值 | 538 |
| 只有 machine immediate | 675 |

这两个集合不能互换：

- ctree-only 行包括经 LDR/STR/LDUR/STP/CSEL 等实现的默认值、索引、错误码和条件结果；
  相应机器指令未必带 o_imm。
- machine-only 行以 ADD=267、ADRL=111、SUB=97、CMP=67、MRS=62、ADRP=47 为主，
  还包含 BFI=22、TBZ=22 等。它们混合了 frame/local 地址、重定位物化、系统寄存器
  selector、已由 branch/switch 门禁覆盖的 predicate，以及少量源码运算；不能把每个
  ARM64 immediate 都反向写成 C++ token。
- 0x00425350（PSB）与两处 0x0066646D（MDF）会因地址区间碰撞被 Hex-Rays 表示为
  cot_obj，不属于本次 cot_num。它们仍由既有 code_reference_artifact_surface 的
  3 个 MOV-wide 行独立约束。

因此本门禁固定的是“反编译器恢复出的完整数值表达式面”，并显式保留它与机器立即数、
地址重定位及 ABI layout 的边界。

## 机械父表达式分层

下表只按 ctree 的直接 parent opcode 分组，不从变量名猜语义：

| 机械组 | parent | 行数 |
| --- | --- | ---: |
| packed / pointer / index 算术 | add/sub/idx/mul | 617 |
| 默认值、赋值与根表达式 | asg/None | 330 |
| 调用实参 | call | 61 |
| 位拼接与掩码 | band/ushr/shl/sshr/xor | 86 |
| 比较边界 | ugt/ne/eq/sge/sgt/sle/slt/uge/ult | 85 |
| 显式 cast | cast | 2 |
| **合计** | 22 种 parent | **1,181** |

最常见值为 0=286、1=281，随后是 2=58、8=53、4=46、3=45。这些计数包含源码行为
常量与 Hex-Rays 物化的对象/指针偏移；门禁保存二者，但本地源码对照只接受行为证据，
不硬凑 ARM64 字节布局。

## fresh Android 证据与本地对照

本轮 fresh decompile：

- FindNameIndex_guess@0x59641C：packed count 默认 0、tag 0x0D..0x10、lowcase 10..13、
  0xFFFFFFFF 变宽掩码与 unsigned state bound 完整保留。
- CreateVariant_guess@0x59673C：classifier tag 集、bool 0/1、integer
  左移 16/32/48、raw mask、String/Resource tag 和 dispatch 初始 refcount 全部可见。
- EnumMembers@0x596F50：category 0..7、TJS_ENUM_NO_VALUE=0x100000、callback 参数数
  2/3、四只 Variant 默认值和 packed 循环常量保持同一生命周期。
- PropGet@0x597854：-1002、-1006、TJS_MEMBERMUSTEXIST=0x400、-1001 与 count decoder
  默认 0 一致。
- Adopt@0x598708：unsigned size < 0x40、PSB magic、owner refcount 0 -> 1、header index
  2..9 和 strict refresh 边界一致。
- root PropSet@0x59B378：member/setter/objthis/param 的 -1001/-1007/-1008/-1 返回、
  numparams=1 与 signed-result normalization 一致。

代表性本地映射：

| Android 数值族 | 本地实现 |
| --- | --- |
| classifier tag → category 0..7 | cpp/plugins/psbfile/PSBPackedInternal.h:37-100 |
| integer tag、符号扩展与左移 16/32/48 | PSBPackedInternal.h:107-151 |
| Real 的 0.0/1.0 与 tag 0x1D..0x1F | PSBPackedInternal.h:158-187 |
| PropGet 错误码、0x400 与 packed count default | cpp/plugins/psbfile/main.cpp:121-206 |
| EnumMembers category、默认 flag、2/3 callback arity | main.cpp:371-442 |
| CreateVariant 默认值、tag 分支和 scalar helper 分层 | main.cpp:557-687 |
| MDF/PSB magic 与 0x0B/0x40 gates | cpp/plugins/psbfile/PSBRawFile.cpp:17-47,516-539 |
| trie/dictionary 的 0/1 边界和 lower/upper 更新 | PSBRawFile.cpp:52-105 |

对象分配尺寸、字段偏移、vtable slot byte displacement、TPIDR canary offset 等仍作为
Android 编译产物留在证据清单中；依项目字节布局规则，它们不进入 portable C++ 源码。

## 机器门禁

verify_elf_surface.py 新增 NMC1 compact manifest：

- 49,640 raw bytes，SHA-256
  5c0185f96c797fbea416052a51bcfee837550e85256c1844c67d7b5f7886b9d1；
- 每行固定 owner-local ordinal、parent/type/mnemonic id、synthetic 标志、EA、ancestor
  anchor、exact word 与 canonical value；
- verifier 独立检查 owner FDE、0-based ordinal、anchor 对齐/范围、exact instruction
  word、normal CFG reachability、concrete EA 与 synthetic ancestor 关系，以及
  parent/type/mnemonic 全量计数；
- trailing digest 固定完整 132,066-byte textual ctree 序列。

当前固定输出：

    numeric_ctree_surface=true owners=95 rows=1181 ea_backed=1133 synthetic=48 ea_sites=1023 anchors=1055 normal=1055 landing=0 single=956 shared=99 max=4 parents=22 types=22 mnemonics=39 values=54 zero=286 one=281 semantic_bytes=132066 semantic_sha256=true instruction_words=true paths_complete=true

完整 ELF 门禁通过，114-entry 判定仍为
ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0。本轮新增的是数值表达式与 ABI immediate
的证据分层，不改变 15 个 stripped/O3 精确 token 上限。
