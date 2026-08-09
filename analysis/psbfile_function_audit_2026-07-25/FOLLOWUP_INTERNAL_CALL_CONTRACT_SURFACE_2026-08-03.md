# Follow-up：44-site MANIFEST 内 direct-call contract 闭环

日期：2026-08-03

权威目标：Android ARM64 `reference/libkrkr2/libkrkr2.so`

SHA-256：`ded611b9018cfca425e97d5f8aaaa5dff809c4bacefb66ba77806372ddb52b38`

## 结论

- 114 个 FDE 内连接 MANIFEST 函数的 direct transfer 精确为 **44 site / 39 unique
  edge**：42 个 `BL` 与 2 个 cross-FDE tail `B`。
- 44 site 已从“caller→callee 集合”推进为逐调用点 contract：**21 类参数角色 + 8 类
  regular-result 消费方式**。
- 两处 hidden-sret、一次 non-trivial Variant by-value ABI、两处 `uint32 → size_t`
  zero-extension，以及 ncbind 的两只空 tag-reference 实参拥有独立 producer-word 门禁。
- `CopyFirstArgument_guess@0x59B708` 的旧 IDB prototype 遗漏了 caller 显式传入的 `X1/X2`。
  本轮已补成两个中性 opaque-tag const-ref 参数；fresh decompile 保留它们且函数体不读取，
  IDB 已保存。精确模板 type spelling 仍被 stripped，不伪造确定名字。
- 逐项对照本地 `main.cpp`、`PSBRawFile.cpp`、`PSBMedia.cpp`、`ncbind.hpp` 与
  `ncb_invoke.hpp` 后，**没有新增 `cpp/` GAP**；本轮不修改 `cpp/`。

## 逐调用点角色

| 参数角色 | site | AArch64/source-facing ABI | regular result |
|---|---|---|---|
| Variant materialize | `5971B4,5973A4,597848,5979B4` | `X0=self, X1=result, X2=node` | 返回同一 result pointer，但 caller 不消费 |
| DecodeName | `597350,5975CC(tail),598FCC,599C30` | `X0=string out, X1=owner, W2=nameIndex` | `void` |
| packed name lookup | `59797C,598C94,598D94` | `X0=names, X1=key, X2=outIndex` | `W0 bool` 立即分支 |
| packed dictionary lookup | `597994,598CAC,598DAC` | `X0=dictionary, W1=nameIndex, X2=outOffset` | `W0 bool` 立即分支 |
| NCB RegistItem | `597FC4,598038,5980B8(tail)` | `X0=registrar state, X1=name, X2=item` | `void` |
| factory Load | `598154` | `X0=file, X1=non-trivial by-value Variant address` | bool 明确丢弃 |
| file Adopt | `5982FC,59860C` | `X0=file, X1=data, X2=size_t, X3=OwnerFilter` | `W0 bool` 分支 |
| file LoadStorage | `598360,599EE8` | `X0=file, X1=ttstr name, X2=OwnerFilter` | `W0 bool` 分支 |
| raw try lookup | `59967C` | `X0=node, X1=key, X2=raw-node out` | `W0 bool` 分支 |
| media EnsureContainer | `5998EC,599964,599A2C` | `X0=media, X1=name` | `W0 bool` 分支 |
| media GetResourceData | `599900,599978` | `X0=media, X1=name, X2=size out` | pointer 测试/保存 |
| media Resolve | `599A44,59A0E0` | `X0=media, X1=name, X2=raw-node out` | `W0 bool` 分支 |
| ttstr IndexOf | `599E38,59A56C,59A5A0` | `X0=ttstr, W1='/', W2=start` | `W0` index 消费 |
| adaptor Create | `599F18` | `X0=file, W1=sticky, W2=throwOnFail` | dispatch pointer 使用 |
| raw Contains | `59A680` | `X0=node, X1=key` | `W0 bool` 分支 |
| strict raw lookup | `59A694` | `X8=raw-node result, X0=node, X1=key` | hidden-sret |
| NCB RegistBegin | `59A914` | `X0=registrar state` | `void` |
| NCB registerMembers | `59A91C,59A9A0` | `X0=registration body state` | `void` |
| NCB RegistEnd | `59A924,59A958` | `X0=registrar state`；第二处为 cleanup path | `void` |
| typed root Invoke | `59B334,59B418` | `X0=params functor, X1=member pointer, X2=native` | bool 映射为 TJS status |
| load first argument | `59B634` | `X8=Variant result, X0=functor, X1/X2=empty tag refs` | hidden-sret |

这里的角色来自每个 callsite 的实际寄存器 producer 与 callee 数据流，不是从本地函数名
反推。39-edge 集合仍保留；同一 edge 的多个 site 不再被集合去重掩盖，例如三处
`RegistItem`、两处 `RegistEnd` 与四处 `CreateVariant`。

## 八类返回消费

| result class | 数量 | 约束 |
|---|---:|---|
| ignored pointer | 4 | `CreateVariant` 返回原 result pointer；caller 继续从原 local/参数读，不依赖 X0 |
| void | 12 | DecodeName 与 NCB registration 调用不伪造返回值 |
| ignored bool | 1 | factory 调用 `Load` 后直接析构 by-value Variant；与本地显式 `(void)` 对应 |
| bool branch | 17 | lookup/adopt/storage/media/resolve/contains 的 `W0` 立即控制成功/失败边 |
| pointer use | 3 | 两个 resource pointer 与一个 adaptor dispatch pointer 被测试或保存 |
| integer use | 3 | 三个 slash `IndexOf` 返回值进入 `-1`/substring 控制流 |
| hidden-sret | 2 | strict raw-node result 与 load argument Variant 都由 `X8` 指向 caller storage |
| bool-to-error | 2 | root getter/setter typed invoke 的 bool 经 `MVN/SBFX` 映射为 TJS status |

这一区分纠正两个常见误读：`CreateVariant` 的 pointer return 存在不代表调用者使用它；
`RegistBegin/registerMembers/RegistEnd/RegistItem` 的旧寄存器残值也不构成源码返回值。

## 七个特殊 producer 证据

`verify_elf_surface.py` 逐字固定以下 producer，而不是只固定 call target：

| call | producer | word | 证据 |
|---|---|---|---|
| `598154 → Load` | `59814C` | `910003E1` | `X1=SP`，前一条已 copy-construct Variant；证明 non-trivial by-value 间接 ABI |
| `5982FC → Adopt` | `5982D8` | `2A1503E2` | 写 `W2`，octet 的 32-bit length 按 AArch64 规则零扩展为 size_t |
| `59860C → Adopt` | `5985FC` | `92407EC2` | `AND X2,X22,#0xffffffff`，stream size 显式收窄/零扩展 |
| `59A694 → strict lookup` | `59A68C` | `910003E8` | `X8=SP`，raw-node hidden-sret output |
| `59B634 → first argument` | `59B624` | `9100A3E8` | `X8` 指向 caller Variant result storage |
| 同上 | `59B62C` | `910083E1` | `X1` 指向第一只空 tag temporary |
| 同上 | `59B630` | `910063E2` | `X2` 指向第二只空 tag temporary |

### Variant by-value

`Factory@0x5980F4` 先在 caller stack copy-construct `tTJSVariant`，再以 `X1` 传给
`PSBFile::Load@0x598268`，调用结束后立即析构该 copy，且不消费 bool。对应本地：

```cpp
(void)file->Load(*params[0]);
```

以及 `bool PSBFile::Load(tTJSVariant value)`。把参数改成 `const tTJSVariant&` 会删除这条
copy/dtor 生命周期，因此当前 by-value 签名是必要结构，不是表面等价选择。

### 两种 size producer

两处 `Adopt` 的 callee ABI 都是 64-bit unsigned size：octet 路径通过写 `W2` 隐式零扩展，
storage 路径通过 `AND X2,#0xffffffff` 显式零扩展。两者均来自 32-bit PSB/MDF length，
与本地 `uint32_t` source → `std::size_t Adopt` 数据流一致；没有 signed-extension 或原生
64-bit storage length 直接穿透。

### 两个 hidden-sret

- `Resolve@0x59A694` 把 caller stack 地址放入 `X8`，strict lookup 将 retained
  `PSBRawNode` 写入该槽；随后 current 的旧 owner 被释放、新 owner 被接管，temporary 再
  析构。对应 `current = current.GetDictionaryValueStrict(key.Buf)`。
- `load FuncCall@0x59B634` 把 Variant output 放入 `X8`，helper 完成多级
  copy/CopyRef/dtor 链；随后该 output 以 `X1` 传给 member-pointer `PSBFile::Load`。

两处都不能改写为普通 `X0` return 或裸字段 memcpy。

### ncbind 两只空 tag 参数与 IDB 修正

caller 在 `0x59B62C/0x59B630` 明确形成两个不同 stack 地址并放入 `X1/X2`；callee
`0x59B708` 不读取它们。它们与本地模板调用：

```cpp
io(tNumTag<1>(), tTypeTag<tTJSVariant>())
```

及 `paramsFunctor::operator()(const tNumTag<N>&, const tTypeTag<T>&)` 的两个 const-ref
临时对象完全同形。目标没有保留精确模板 type name，所以 IDB 使用中性
`PSB_empty_call_tag_arm64`，只声明“存在两个 opaque empty-tag reference ABI 参数”。这次
修正补回源码结构，但不把本地名字反灌成二进制权威名字。

## 两条 direct tail

1. `decodeName wrapper@0x5975CC`：把 caller 的 result 从 `X1` 移到 `X0`，从 dispatch
   读取 owner 到 `X1`，保留 `W2 nameIndex`，随后无帧 tail 到 `DecodeName@0x597B1C`。
2. `registerMembers@0x5980B8`：在恢复 callee-saved register/stack 后，以
   `X0=registrar state, X1="load", X2=item` tail 到 `RegistItem@0x59AEEC`。

这两条与本地 thin member wrapper 和 NCB 最后一项注册自然产生的 tail 形状一致；目标
没有额外 return adapter 或隐藏 cleanup。

## 源码对照结论

- packed/dispatch：三参数 helper、out-index/out-offset 与 result/node 分离均保留；没有把
  packed lookup 合并成 map 或把 `CreateVariant` 改成 return-by-value。
- raw file/node：by-value Variant、`OwnerFilter`、64-bit size ABI、bool output 与 strict
  hidden-sret 各自保留；try/strict 方法边界没有合并。
- media：`EnsureContainer → LoadStorage → CreateAdaptor` 与
  `Resolve → Contains → StrictGet` 顺序、短路和 delayed output commit 一致。
- ncbind：RegistBegin/body/RegistEnd、typed root member-pointer 与 load 参数提取仍走模板
  分层；没有手写功能等价 wrapper。

因此 44-site contract 为 `ALIGNED`。本轮唯一纠正是 IDB prototype 可读性，不是生产代码
修改。

## 可复现门禁

```bash
python3 analysis/psbfile_function_audit_2026-07-25/verify_elf_surface.py \
  --llvm-dwarfdump \
  /Users/bytedance/Developer/emsdk/upstream/bin/llvm-dwarfdump
```

新增输出：

```text
internal_call_contract_surface=true sites=44 edges=39 roles=21 result_classes=8 direct_tails=2 hidden_sret=2 special_producers=7 parameter_roles=true result_consumption=true
```

后续全量输入侧复核没有只停在 44 个 MANIFEST 内调用：全部 317 个 normal direct
transfer 的 446 个寄存器参数现均拥有完整 predecessor producer 门禁，并继续确认本报告的
两处 hidden-sret、by-value Variant、size zero-extension 与两只 tag temporary；详见
[FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_NORMAL_DIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。

互补的 46 个非 switch indirect transfer 也已完成输入侧闭合：117 个 `X0..X7` 参数形成
120 条 producer 关系，40 个 normal-entry 与 6 个 landing-only callsite 分开验证，且
call-operand type 排除了 factory callback 的 stale `X4` 假第五参。详见
[FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md](FOLLOWUP_INDIRECT_ARGUMENT_PRODUCER_SURFACE_2026-08-03.md)。
