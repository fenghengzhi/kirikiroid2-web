# Follow-up：typed member / 对象字段访问表面闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

> 后续纠正：本报告的 `483 = 416 EA-backed + 67 synthetic` 是当时 IDB 类型状态下的
> 精确旧基线，不是最终完整表面。后续 `cot_ptr/cot_idx` 全量复扫又以 producer/consumer
> 正证据提升 5 条 read-only member 行；当前为
> `488 = 421 EA-backed + 67 synthetic`、
> `R=317/W=147/RW=10/address=14`、390 个唯一 EA 站点。详见
> [FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md](FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md)。

## 结论

- 本轮对 114 个 MANIFEST 函数的 fresh Hex-Rays ctree 枚举全部最外层
  `cot_memptr/cot_memref`，得到 **483 条旧基线 typed-member 语义行**：
  `R=312 / W=147 / RW=10 / address=14`。
- 其中 **416 条**携带具体指令地址，折叠为 **385 个唯一机器站点 / 62 个 owner FDE**；
  另 **67 条**是 Hex-Rays 为合并初始化、成对 load/store 或折叠临时量产生的 synthetic
  expression，没有可单独指定的 EA。
- 385 个站点现已接入 `verify_elf_surface.py`：每个地址必须属于 MANIFEST FDE，目标 word
  按 `<owner:u64, site:u64, word:u32>` 序列化后的 SHA-256 必须保持
  `22bb4590f6e8f08c09d1df6bf5ef4dfdde78f5ed2024335914a2e3425b491a97`。
- 本轮的 fresh 裸指针复核发现 **7 个 IDB 类型传播缺口**，不是隐藏字段。给 4 个核心函数
  的 20 个局部补型/重命名，并给 3 个 cleanup/lookup 函数重新施加 4 个 user type 后，
  反编译直接显示完整 `owner → header → packed/resource` 字段链。相关注释与类型已保存
  IDB。
- 逐项对照当前 `PSBRawFile.h`、`PSBDispatch.h`、`PSBMedia.h`、`ncbind.hpp` 与
  `ncb_invoke.hpp`，字段集合、嵌套结构、引用计数、所有权转移与模板 field bundle 均一致；
  **没有新增 `cpp/` GAP**，本轮不修改 `cpp/`。

## 方法与边界

visitor 只记录最外层 member expression，避免把
`owner->headerStorage.entries` 同时重复记成外层 `entries` 和内层 `headerStorage` 使用；
但 inline 子对象本身取地址时仍记为 address-use。读写分类规则是：assignment 左值为
`W`，compound/inc/dec 为 `RW`，取地址为 `address`，其余为 `R`。

ctree 类型仅用于建立语义清单；机械门禁重新从 ELF 读取 385 个 AArch64 word，不要求
本地存在 IDA。没有 EA 的 67 个 synthetic expression 只进入字段/读写统计，不伪造机器
地址。本轮另行枚举的 `cot_ptr=480` 是当时类型状态下的初步单类快照，不是完整 raw
surface。后续同时枚举 `cot_ptr/cot_idx` 并再补四个局部类型后，当前 raw surface 为
`461+206=667` 行；逐行机器锚点与类别见后续裸内存报告。

ARM64 偏移只用于确认字段顺序和无遗漏。它们是 Android ABI 下的编译结果，**不进入**
wasm32 C++ 的 `_pad`、`#pragma pack` 或 `offsetof` 断言。

## 核心对象字段

| 目标记录 | typed rows | Android 字段表面 | 当前 source-facing 结构 |
|---|---:|---|---|
| `PSBRawOwner *` | 72 | `refCount@0`、`header@8`、inline `headerStorage@10`、`data@58`、signed `size@60` | `PSBRawOwner` 的 refcount/header view/inline header/raw allocation/signed size；自然 ABI，不硬凑偏移 |
| `PSBRawHeader` / `*` | 44 W + 25 R | signature/version/encrypt、encryptData、names、strings/stringsData、chunkOffsets/chunkLengths/chunkData、entries | `PSBRawHeader` 的 11 个字段，构造/Refresh/Adopt 写入与 consumer 读取均完整 |
| `PSBRawNode` 各形态 | 77 | 首 qword owner holder、次 qword node；copy/assignment/cleanup 都分别访问 | 源码保留 `PSBFile file_` 首子对象 + `node_`，不是把 O3 scalarization 反写成两个裸字段 |
| `PSBFile` / `const PSBFile` | 16 | 唯一 owner qword | `PSBFile` 只含 `owner_`，Rule-of-Three 通过该 holder 实现 |
| `PSBValueDispatch *` | 44 | 两个 base address point、refcount、owner/node、valid | 源码保留双继承 + `refCount_` + 嵌套 `PSBRawNode value_` + `valid_`；owner/node 是 O3 展开的 `value_` |
| `PSBMedia *` | 13 | vptr、refcount、Variant file、ttstr container | `iTVPStorageMedia` 基类 + `_ref` + `_file` + `_container`，没有额外缓存或隐藏 owner |

关键 access-mode 交叉关系：

- owner `refCount`：`R=15 / W=12 / RW=7`；覆盖 AddRef、Release、copy/assignment、
  temporary cleanup 与 terminal delete；
- owner `header`：`R=8 / W=4`，inline `headerStorage` 另有 4 次取地址；
- owner `data`：`R=16 / W=2`；只有 owner 构造/交接写入，所有 terminal release 都从同一
  字段释放；
- header 的 11 个字段各有 4 次写入；consumer 读取只涉及实际使用的
  `names/strings/stringsData/chunk*/entries`，不把未消费字段误判为不存在；
- dispatch `valid` 为 `R=6 / W=3`，与独立 invalidate 状态一致，不和 owner/null 合并；
- raw node 的 holder 与 node 在 output assignment 中保持
  `Release old → copy owner → AddRef → write node`，不是两 qword memcpy。

## 补回的 header 数据流

fresh 反编译后的共同语义不超过十行：

```text
header = holder.owner->header
string path: offsets = header->strings; data = header->stringsData
resource path: if !header->chunkData return null
offsets = packed(header->chunkOffsets); lengths = packed(header->chunkLengths)
lookup path: FindNameIndex(header->names, key, outIndex)
release path: if --owner->refCount == 0: dealloc(owner->data); delete owner
```

本轮具体补型：

- `CreateVariant@0x59673C`：`stringHeader/stringsData/stringOffsets/*Data` 与
  `resourceHeader/chunkData/chunkOffsetsData/chunkLengthsData`；
- `GetString@0x598B58`：header/string offsets/string data 四只局部；
- `GetResource@0x5996E4` 与 media inline clone `@0x59A0B4`：header/chunk data/
  offsets/lengths 四只局部；
- `GetDictionaryValue@0x598D58`、`ContainsDictionaryKey@0x5995D8`、
  `GetListAt@0x5999F4`：重新施加 `PSBRawNode*`/`PSBRawOwner*` user type，使 lookup 与
  terminal cleanup 不再显示 `_QWORD + 1/+11`。

对应 fresh 结果现在直接显示：

- `0x596834/0x598B98`：`owner->header->strings/stringsData`；
- `0x59686C/0x5996E8/0x59A0EC`：
  `owner->header->chunkData/chunkOffsets/chunkLengths`；
- `0x598D90`：`self->owner->header->names`；
- `0x598DC8..0x598DE0`、`0x599630..0x59964C`、`0x599A7C..0x599A98` 与
  `0x59A150..0x59A168`：`refCount` 归零后释放 `data` 再 delete owner。

## 容器与模板字段

| 家族 | 目标字段证据 | 本地对应 |
|---|---|---|
| `std::vector<std::string>` | begin/end/capacityEnd：26 rows；end 有唯一 `RW` increment | `GetDictionaryKeys()` 返回 `std::vector<std::string>`，reserve/emplace slow path 未换成别的容器 |
| COW `std::string` / `ttstr` | string data 16 rows；ttstr storage 31 rows | key/vector element 与 ttstr 临时量仍保留 COW 引用生命周期 |
| `OwnerFilter` | target storage、manager、invoker：17 rows | `PSBFile::OwnerFilter = std::function<void(PSBRawOwner&)>` 的 manager/invoker gate |
| class info | initialized/name/id/classObject 四字段 | `ncbClassInfo<T>::InfoT` 的原四字段顺序 |
| native registration | className/classObject/hasConstructor，另有 impl/isRegist wrapper | `ncbRegistNativeClass` 嵌套在 `ncbRegistClass`，Begin/Item/End 与 Unregist 路径不扁平化 |
| instance adaptor | vftable/nativeInstance/sticky | `ncbInstanceAdaptor<PSBFile>` 的 `_instance/_sticky` 与 `_deleteInstance()` |
| params functor | converter storage、numparams、result、param | `paramsFunctor<METHOD>` 的 `_aconv/_rconv` 后接 `_numparams/_result/_param`；load 仍以两只空 tag const-ref 提取首参数 |
| member pointer/wrapper | function-or-vtable offset、this-adjust/virtual flag；factory/root/load wrapper member | Itanium member pointer 与 `MethodCaller::Invoke` 模板链，不替换为普通函数指针 |

这里的 `converterStorage`、registration `State` 与 wrapper 名只是 IDB 中性 ABI 记录，
用于呈现目标字段束；不声称它们是原模板实例的精确 C++ identifier。源码仍以现存 ncbind
模板为权威恢复候选，目标内字段证据只验证其实例化形状。

## 源码对照结论

1. `PSBRawHeader` 十一个字段完整保留，header view 与 raw allocation/size 分开；没有把
   header 直接指向输入后跳过 inline view。
2. `PSBRawNode` 仍通过 `PSBFile` 首子对象复用 holder special members；目标的扁平
   owner/node access 是优化产物，不构成“源码应扁平化”的证据。
3. dispatch 双基类、独立 refcount、嵌套 raw node 与 valid byte 顺序一致；invalid 与
   owner-null 两种状态未合并。
4. media 的 Variant cache 与 container name 分开提交；没有新增 raw-owner 字段或让 stream
   持有 owner。
5. ncbind/MethodCaller 的 field bundle、空 tag、member pointer 与 adaptor 生命周期均由
   当前模板自然产生；没有手写功能等价 wrapper。

因此本轮检查到的 483-row typed-member/对象字段访问基线为 `ALIGNED`。后续新增的 5 条
promotion 与剩余 667 条 raw surface 也为 `ALIGNED`；本轮及后续复扫都只纠正 IDB 类型
传播与增加 ELF 证据门禁，不修改生产实现。

## 可复现门禁

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump \
  /Users/bytedance/Developer/emsdk/upstream/bin/llvm-dwarfdump
```

本轮新增的旧基线输出：

```text
typed_member_instruction_surface=true sites=385 owners=62 owner_fdes=true instruction_words=true
```

其中带真实 EA 的 108 个 `W/RW` 事件后来又逐项闭合到 selected store operand 与完整
value producer；详见
[FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_WRITE_VALUE_PRODUCER_SURFACE_2026-08-03.md)。
互补的 311 条带真实 EA 的 `R/RW/address` 语义事件后来也逐项闭合到物理 producer
lane、第一消费者或 residual anchor 来源；详见
[FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_READ_CONSUMER_SURFACE_2026-08-03.md)。
最后 67 条没有独立 ctree EA 的 optimizer-synthetic 语义行也已逐项闭合到 73 个真实
机器锚点；详见
[FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md](FOLLOWUP_TYPED_MEMBER_SYNTHETIC_REALIZATION_SURFACE_2026-08-03.md)。
后续新增的 5 条 typed-member promotion 与完整 667-row `cot_ptr/cot_idx` 表面见
[FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md](FOLLOWUP_RAW_MEMORY_DEREFERENCE_SURFACE_2026-08-03.md)。
