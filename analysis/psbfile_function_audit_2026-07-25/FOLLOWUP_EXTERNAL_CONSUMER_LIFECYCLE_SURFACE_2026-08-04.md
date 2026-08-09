# Follow-up：外部 consumer ELF 反向门禁与对象生命周期

日期：`2026-08-04`。本轮继续沿 psbfile 114-address MANIFEST 的 caller 方向复核，
把旧的 IDA xref 数量普查升级为直接读取权威 Android ARM64
`reference/libkrkr2/libkrkr2.so` 的可重复 ELF 门禁，并 fresh 反编译外部 consumer 中尚未
形成独立闭环的 `LoadedResourceRecord`、atlas record 与 `ObjSource` NCB adaptor 特殊成员。
这些函数不进入 114-address 统计分母；本轮不使用其他架构二进制或源码。

## 纠正旧 xref 口径

旧报告把 IDA 对两个本地 weak `std::vector<std::string>` 符号的 PLT alias 也算成了
“外部 caller”，所以得到 `305 sites / 17 targets / 27 callers / 73 pairs`。权威 ELF
逐字解码后的真实结果是：

| 项目 | 数量 |
| --- | ---: |
| IDA 主实现簇外 code-xref 原始数 | 305 |
| `.plt` symbol alias，非 direct caller | 2 |
| `.text` 中实际 `BL` consumer | 303 |
| 被调用的 MANIFEST 入口 | 15 |
| 外部 owner FDE | 25 |
| 去重 owner→target 对 | 71 |

两条被剔除的关系为：

- `0x40CD20`：`ADRP/LDR/ADD/BR X17`，其 `.rela.plt` GOT 槽绑定 weak dynamic symbol
  `_ZNSt6vectorISsSaISsEE7reserveEm`，本地定义值为 `0x599174`；
- `0x423250`：相同 PLT 形状，绑定
  `_ZNSt6vectorISsSaISsEE19_M_emplace_back_auxIJRSsEEEvDpOT_`，本地定义值为
  `0x59B7E8`。

这两只 stub 没有 `B/BL 0x599174/0x59B7E8`，只是 loader 经 GOT 解析同名本地 weak
definition 的符号关系，不能解释为源码从 PLT 反向调用定义。原
[FOLLOWUP_EXTERNAL_CONSUMER_XREF_2026-08-02.md](FOLLOWUP_EXTERNAL_CONSUMER_XREF_2026-08-02.md)
已按该正证据就地纠正。

## 303 条真实 consumer 全集

`verify_elf_surface.py` 现遍历完整 ELF `.text`，只接受 AArch64 immediate `B/BL`，把目标
命中 MANIFEST 且 owner FDE 不在 MANIFEST 的站点纳入反向 surface。303 条全部是 `BL`，
没有 direct tail；canonical row 固定
`(target, site, owner-FDE, exact-word, kind)`，共 `8,787` bytes，SHA-256 为
`238f46dbd5651312d5f854a854e3d5ef366822ba85e9d1d736450be8e6f57ec9`。

| MANIFEST target | 站点 | consumer 语义 |
| --- | ---: | --- |
| `0x597AD4` | 3 | `PSBFile` constructor |
| `0x598538/0x598A3C/0x598A64` | 3 | load/root/transfer |
| `0x598B3C` | 147 | raw owner terminal release |
| `0x598B58` | 8 | raw String |
| `0x598C58/0x598D58` | 78 | strict/try dictionary lookup |
| `0x598E64` | 2 | ordered dictionary keys |
| `0x5992E8/0x599438` | 29 | Double/Int |
| `0x599554/0x5995D8` | 18 | category/contains |
| `0x5996E4` | 9 | borrowed Resource |
| `0x59A284` | 6 | `ttstr` index helper |

25 个 owner 精确覆盖既有 Player/ResourceManager/ObjSource consumer、四个通用 string
consumer，以及本轮闭合的 record/adaptor 特殊成员；没有第二套 PSB owner、缓存或容器
从 MANIFEST 外绕入。

## Fresh Android 生命周期证据

本轮 fresh `decompile` 了 `0x698074`、`0x6DB3E8`、`0x6E3EFC`、`0x6E407C`、
`0x6EBCFC`、`0x6FE8C8`、`0x6FE8FC`、`0x6FE990`、`0x6FE9F0`，并重新反编译
`Motion_ObjSource_ncb_register@0x6FE610` 与
`ObjSource_ncb_registerMembers@0x69CCB8` 取得类名和 member 字面量正证据。
关键源码级伪代码（10 行）为：

```text
LoadedResourceRecord(): file=null; win.rehash(10); krkr.rehash(10)
if krkr construction throws: destroy(win); Release(file); rethrow
~LoadedResourceRecord(): destroy(krkr); destroy(win); Release(file)
then outer unordered-map node destruction releases its ttstr key
ObjSource native construct: new {rawOwner,node,texture=null}; attach by class ID
if attach fails or throws: Release(texture); Release(rawOwner); delete native
ObjSource native destroy: Release(texture); Release(rawOwner)
adaptor factory: new {ObjSourceAdaptor-vptr,native=null,sticky=false}
Invalidate/complete/deleting dtor: delete native iff native && !sticky
then clear state; complete dtor restores base vptr, deleting dtor deletes adaptor
```

具体证据如下：

- `Motion_LoadedResourceRecord_ctor_guess@0x6EBCFC` 先零初始化 `PSBFile`，再按声明顺序
  构造 Win/KRKR 两张 `std::unordered_map`，各自调用 prime rehash policy 的 `10`；第二张
  map 构造失败时，`0x6EBDF4` 先销毁 Win map，`0x6EBE24` 再 terminal-release file owner。
- `Motion_LoadedResourceRecordAndKey_destroy_guess@0x6DB3E8` 严格逆序销毁 KRKR map、
  Win map、PSBFile，最后释放 outer node 的 `ttstr` key。
- `sub_698074@0x698074` 逐个销毁 64-byte atlas record：先释放 record string，再释放
  raw owner；整个 vector storage 最后释放。它没有把 texture-rect 数据当成独立 owner。
- `0x6E3E28 → 0x6E3EFC` 是 NCB constructor wrapper 到 24-byte `ObjSource` native object
  的构造/挂载边；正常失败和 EH landing 分别在 `0x6E3FB0/0x6E403C` 释放 raw owner。
- `Motion_ObjSource_dtor_guess@0x6E407C` 先虚调用 Release texture qword[2]，再递减并按零
  terminal-delete raw owner qword[0]。ELF FDE 在 `0x6E40F0` 精确结束；`0x6E40F0` 是有
  独立栈帧和独立 FDE 的 property wrapper，原 IDA 函数范围把两者错误合并，本轮已拆开。
- `0x6FE774` 把 `0x6FE8C8` 存为 ObjSource adaptor factory；Itanium vtable
  `0x1A21F38` 的七个 qword 固定为 prefix、基类 slot、Invalidate、complete dtor 与
  deleting dtor。`0x6FE990 → 0x6E407C` 证明 complete adaptor destructor 调用 native
  destructor；Invalidate/deleting destructor 的内联 clone 保持相同 `native && !sticky`
  ownership gate。

## 本地逐行对照

| Android 行为 | 当前本地复刻 |
| --- | --- |
| record 字段为 file→Win map→KRKR map，析构逆序。 | `cpp/plugins/motionplayer/ResourceManager.h:54-66` 使用相同声明顺序；普通 C++ 逆声明顺序得到 KRKR→Win→file。 |
| 两张 nested map 各自 `rehash(10)`。 | `ResourceManager.cpp:108-113` 依次对两张 map 调用 `rehash(10)`。 |
| Win/KRKR mapped texture 独立 retain/release。 | `ResourceManager.h:20-51` 的两种 entry 均持有一只 texture；对应实现按 AddRef-before-Release 替换并在析构释放。 |
| ObjSource native payload 恰为 raw node pair + lazy texture。 | `SourceCache.h:93-115,151-152` 只有 `PSBRawNode _source` 与 `iTVPTexture2D *_texture`。 |
| native destructor 先 Release texture，再由 raw holder 析构 Release owner。 | `SourceCache.cpp:350-354` 显式 Release texture；随后成员逆序析构 `_source`，触发 raw owner Release。 |
| adaptor 只在 `native && !sticky` 时 delete，并总是清指针/flag。 | `cpp/core/plugin/ncbind.hpp:125-148` 的 destructor、Invalidate 与 `_deleteInstance()` 保持完全相同 gate 和清理次序。 |
| CreateAdaptor 通过 class object 创建 TJS object，再把 native pointer 写入 adaptor。 | `ncbind.hpp:202-225` 保留 class-object gate、CreateNew、GetAdaptor、pointer publish 与 sticky 设置。 |
| ObjSource 注册五个属性和 drawLayer。 | `cpp/plugins/motionplayer/main.cpp:33-42` 与 `0x69CCB8` 的 `originX/originY/width/height/clip/drawLayer` 字面注册顺序一致。 |

因此本轮没有发现新的 `cpp/` GAP；也没有为 ABI 字节偏移增加 padding 或平台布局断言。

## IDB 与独立门禁

本轮按 fresh 证据完成两项 IDB 质量修正：

1. 把错误合并的 `0x6E407C..0x6E4214` 拆成
   `0x6E407C..0x6E40F0` 与 `0x6E40F0..0x6E4214` 两个函数；
2. 对六个已确认的 ObjSource native/adaptor helper 使用带 `_guess` 的行为名，并在函数头
   记录注册点、vtable 和 FDE 依据；精确源码 spelling 未保留，所以没有去掉 `_guess`。

`verify_elf_surface.py` 新增两行固定输出：

```text
external_consumer_surface=true sites=303 targets=15 owners=25 owner_target_pairs=71 direct_calls=303 direct_tails=0 target_roles=15 owner_roles=22 owner_fdes=true sha256=true
external_consumer_lifecycle=true lifetime_fdes=12 raw_release_sites=9 helper_edges=2 adaptor_qwords=7 plt_aliases=2 plt_aliases_are_calls=false vtable_prefix=true
```

整套 ELF gate 与 `verify_audit.py` 均通过。本轮只修改 audit 文档/门禁与 IDB，不修改
`cpp/`，所以无需重建，源码快照不变，最终统计仍为
`ALIGNED:99 / EVIDENCE_LIMITED:15 / HAS_GAP:0`。
