# Follow-up：静态对象、vtable 与初始化顺序门禁

日期：`2026-08-03`。

## 范围与权威

- 唯一行为与结构权威仍是 Android ARM64
  `reference/libkrkr2/libkrkr2.so`，SHA-256 为
  `ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`。
- 本轮不使用 Android ARMv7、iOS ARMv7、废弃私有仓库或 Git LFS 对象，也不寻找同版本
  源码。所有结论来自当前目标的 fresh IDA 反编译、原始 qword 和 ELF program header。
- 114 个 FDE 只覆盖 emitted 函数边界；本轮补审计此前未被机械门禁完整覆盖的静态对象
  拓扑、Itanium vtable prefix、多个继承地址点和 `.init_array` 顺序。

## Fresh Android 证据

本轮重新反编译/分析了：

- `PSBFile_ncbClassInfo_static_init@0x42CEF8`
- `psbfile_static_init@0x42CF28`
- `PSBFile_preRegister_guess@0x59849C`
- `PSBValueDispatch_ctor_guess@0x597AD4`
- `PSBFile_ncbInstanceAdaptor_CreateEmpty_guess@0x59ABD8`
- `PSBFile_ncbRegistNativeClass_RegistItem_guess@0x59AEEC`
- `PSBFile_ncbFactory_FuncCall_guess@0x59B14C`
- `PSBFile_rootProperty_PropGet_guess@0x59B28C`
- `PSBFile_loadMethod_FuncCall_guess@0x59B570`

目标结构摘要（不超过 10 行）：

```text
1. .init_array: 0x42CEF8 先清 class-info，0x42CF28 再构造两只 AutoRegister。
2. 0x42CF28: class-register={vptr,module,next,className}；callback={vptr,module,next,init,term}。
3. 0x59849C: guard 懒建 0x28-byte media；ref=1，file=Void，container=empty；每次均注册同一指针。
4. dispatch primary: prefix {top=0,rtti=0} + 32 个 iTJSDispatch2 槽。
5. dispatch secondary: prefix {top=-8,rtti=0} + 3 个 iTJSNativeInstance 槽。
6. media: prefix {top=0,rtti=0} + 11 个 iTVPStorageMedia 槽。
7. class AutoRegister 是 2 槽表；PSBFile instance adaptor 是 5 槽表。
8. factory/root/load typed wrapper 各是 prefix {0,0} + 完整 34 槽表。
9. 三只 wrapper 只在各自 factory/property/method、析构和 GetFlags 槽发生目标特化。
```

## 目标静态 qword 面

`verify_elf_surface.py` 现从 ELF64 `PT_LOAD` 映射直接读取下列范围，不依赖 IDA 名称或
反编译排版：

| 表面 | 起始 VMA | qword 数 | 固定内容 |
| --- | ---: | ---: | --- |
| `.init_array` 顺序 | `0x19EA088` | 2 | `0x42CEF8, 0x42CF28` |
| pre-register callback vtable | `0x19FD8D8` | 4 | `{top=0,rtti=0}` + 2 槽 |
| dispatch primary vtable | `0x1A0B3C8` | 34 | `{0,0}` + 32 槽 |
| dispatch secondary vtable | `0x1A0B4D8` | 5 | `{-8,0}` + 3 槽 |
| media vtable | `0x1A0B500` | 13 | `{0,0}` + 11 槽 |
| class AutoRegister vtable | `0x1A0B568` | 4 | `{0,0}` + 2 槽 |
| instance adaptor vtable | `0x1A0B588` | 7 | `{0,0}` + 5 槽 |
| factory wrapper vtable | `0x1A0B5C0` | 36 | `{0,0}` + 34 槽 |
| root Property wrapper vtable | `0x1A0B6E0` | 36 | `{0,0}` + 34 槽 |
| load Method wrapper vtable | `0x1A0B800` | 36 | `{0,0}` + 34 槽 |

合计 `10` 个静态表面、`177` 个 qword。门禁会报告第一个变化的 qword、VMA、期望值和
实际值，因此下列变化都不能再被“114 个函数地址仍在”掩盖：

- `PSBValueDispatch` 两个基类交换、secondary offset 改变或 RTTI 项从零变为非零；
- virtual override 顺序、重复 thunk 或 nullsub 槽位改变；
- `PSBMedia` 接口顺序改变；
- NCB adaptor/factory/property/method specialization 被换成另一种 wrapper；
- class-info 与 AutoRegister 的初始化顺序改变。

## 本地源码逐项映射

1. `PSBDispatch.h` 的 `PSBValueDispatch : iTJSDispatch2,
   iTJSNativeInstance` 与目标主地址点、偏移 8 的次地址点一致；32/3 个 override 的顺序
   与全部目标槽一致。
2. `PSBValueDispatch_ctor_guess@0x597AD4` 同时写两个 vptr，随后按
   `refCount -> PSBFile-compatible holder -> node -> valid` 初始化；本地字段和构造顺序一致。
3. `PSBMedia.h` 继承 `iTVPStorageMedia`，提供析构、AddRef/Release 和九个媒体接口；目标
   11 槽（含两个析构槽）顺序完全一致。`PSBMediaRegistry.cpp` 的函数内静态指针也复刻
   guard-once、对象永久存活、每次 callback 重新注册的边界。
4. `NCB_REGISTER_CLASS(PSBFile)` 展开为四指针 class-register 对象，
   `NCB_PRE_REGIST_CALLBACK(initPsbFile)` 展开为五指针 callback 对象；目标
   `0x42CF28` 的写入次序和链表头更新与之对应。
5. `ncbInstanceAdaptor<PSBFile>` 的 `{vptr, instance, sticky}` 结构由
   `0x59ABD8` 的 0x18-byte allocation 和五槽表共同固定；本地使用同一模板，没有自建
   简化 adaptor。
6. `Factory`、`Property(root)`、`Method(load)` 保留 ncbind typed wrapper；三个目标 34 槽
   表的 common slots 与特化 slots 均对应当前宏/模板路径，没有改写成手工 TJS dispatch。

这里的“对应”是可移植 C++ 源码结构对应，不把 ARM64 对象字节尺寸硬编码到 wasm32。

## 结论与 IDB 改善

- 本轮没有发现新的生产 `cpp/` GAP，因此没有为了制造改动而修改源码；114 项统计仍是
  `ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
- 15 项 `EVIDENCE_LIMITED` 仍只表示 stripped/O3 删除的精确名字或 token 上限；本轮新增
  门禁固定的是可由目标直接证明的类拓扑，不尝试据此移除 `_guess`。
- 已在 IDB 的 `.init_array`、dispatch 两个 vtable prefix、media、AutoRegister、adaptor
  及三只 wrapper prefix 共 9 个地址写入证据注释并保存 IDB。

## 验证

```text
PASS
binary_sha256=ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38
manifest=114 static_init_fdes=2 main_fdes=112
static_object_surface=true surfaces=10 qwords=177
init_array_order=true vtable_prefixes=true wrapper_tables=true
eh_surface=true lsda_fdes=39 unwind_only_fdes=75
lsda_call_sites=true functions=39 call_sites=232 no_landing=77 cleanup=80 catch_all=75
raw_lsda_topology=true functions=10 call_sites=51 no_landing=18 cleanup=16 catch_all=17
```

完整命令：

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump /Users/bytedance/Developer/emsdk/upstream/bin/llvm-dwarfdump
```

同一当前工作树还通过：

- Mac Debug `psbfile-dll`：`598 assertions in 11 test cases`；
- Mac Debug `motionplayer-dll`：`1386 assertions in 21 test cases`；
- Mac Debug `motionplayer-ttstr-hash-test`：`109 assertions in 24 test cases`；
- Web Release `psbfile`：构建目标最新，`ninja: no work to do`；
- `verify_audit.py`：114/114、`99/15/0` 与源码快照门禁全部 `PASS`；
- Python AST parse 与 `git diff --check`：`PASS`。
