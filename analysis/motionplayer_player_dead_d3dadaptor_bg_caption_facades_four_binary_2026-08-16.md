# Motion.Player 误拷贝 D3DAdaptor bg/caption façade 四端清理（2026-08-16）

## 结论

本地 `Motion.Player` 曾额外声明并实现：

```cpp
void removeAllBg();
void removeAllCaption();
void registerBg(tTJSVariant);
void registerCaption(tTJSVariant);
```

以及两个仅由这些方法使用的 `std::vector<tTJSVariant>`。四个当前参考二进制共同表明，
这些名字属于 `Motion.D3DAdaptor`，不属于 `Motion.Player`：

- 四端每个名字都只有一个 UTF-16LE literal；
- 每个 literal 的完整 xref 集都归 `D3DAdaptor_ncb_registerMembers_guess` 及 Android
  armv7 紧随 registrar 的 literal-pool data；
- 四端完整 92-member Player registrar 均不包含四名；
- recovery function-name 查询没有任何 Player 同名函数；
- 本地 Player 四个方法零 production caller，只有一个宽泛 draw-cache 单测调用过
  `registerBg/registerCaption`，且从未读取两个 vector；
- 真正的 D3DAdaptor 表面必须保留：remove 两项是 typed `void()` nullsub，register 两项
  虽然 native body 为空，generated NCB wrapper 仍分别执行五参数与三参数转换。

本轮因此只删除 Player 的复制 façade 与两个孤立 vector。D3DAdaptor 的四个注册项、typed
wrapper ABI 和 nullsub body 完全不变。

## Registrar 映射

| 目标 | Motion.Player registrar | Motion.D3DAdaptor registrar |
|---|---:|---:|
| Android arm64 | `0x6D3DA8` | `0x6AA274` |
| Android armv7 | `0x597EC8` | `0x57CC58` |
| iOS arm64 | `0x1001244F8` | `0x1001039A4` |
| iOS armv7 | `0x123848` | `0x100D94` |

四份 Player registrar fresh decompile 对四名均为零命中。Android arm64 当前 function comment
积累较长，反编译文本被工具限为 1024 chars；不过四名的唯一字符串和完整 xref 所有权仍
逐一排除了 Player registrar，不能用截断文本单独作阴性证明。

## UTF-16LE 唯一字符串与 xref

### Android arm64

| name | literal | 全部 xref |
|---|---:|---|
| `removeAllBg` | `0x14D5AFC` | `0x6AA4D8`, `0x6AA4E0` |
| `removeAllCaption` | `0x14D5B14` | `0x6AA4F4`, `0x6AA4FC` |
| `registerBg` | `0x14D5B36` | `0x6AA584` |
| `registerCaption` | `0x14D5B4C` | `0x6AA5F4` |

全部位于 `D3DAdaptor_ncb_registerMembers_guess`。

### Android armv7

| name | literal | 全部 xref |
|---|---:|---|
| `removeAllBg` | `0x57CDF0` | `0x57CCCA` |
| `removeAllCaption` | `0xD855F8` | `0x57CCD8`, `0x57CCE0`, literal-pool data `0x57CE08` |
| `registerBg` | `0xD8561A` | `0x57CCEA`, `0x57CCF2`, literal-pool data `0x57CE10` |
| `registerCaption` | `0x57CE1C` | `0x57CCFE` |

函数内 code xref 均位于 D3DAdaptor registrar；两个无函数归属的 data xref 位于同一
registrar 后继 literal pool，不构成 Player 调用或注册边。

### iOS arm64

| name | literal | 全部 xref |
|---|---:|---|
| `removeAllBg` | `0x10195BFEA` | `0x100103A64` |
| `removeAllCaption` | `0x10195C002` | `0x100103A84` |
| `registerBg` | `0x10195C024` | `0x100103AA4` |
| `registerCaption` | `0x10195C03A` | `0x100103AC4` |

全部位于 D3DAdaptor registrar。

### iOS armv7

| name | literal | 全部 xref |
|---|---:|---|
| `removeAllBg` | `0x174E34E` | `0x100E44`, `0x100E4A`, `0x100E56` |
| `removeAllCaption` | `0x174E366` | `0x100E62`, `0x100E68`, `0x100E74` |
| `registerBg` | `0x174E388` | `0x100E80`, `0x100E86`, `0x100E92` |
| `registerCaption` | `0x174E39E` | `0x100E9E`, `0x100EA4`, `0x100EB0` |

全部位于 D3DAdaptor registrar。

## 为什么不能删除 D3DAdaptor 对应项

D3DAdaptor 的四项虽然没有持久 bg/caption 容器，但 script wrapper 仍然可观察：

```text
removeAllBg/removeAllCaption:
    require argc >= 0
    ignore every surplus argument
    call void() nullsub

registerBg:
    require argc >= 5
    CopyRef argv[0]
    AsReal + float narrow argv[1..3]
    bool-convert argv[4]
    call five-argument nullsub

registerCaption:
    require argc >= 3
    CopyRef argv[0]
    AsReal + float narrow argv[1..2]
    call three-argument nullsub
```

因此“native body 为空”只意味着 D3DAdaptor 不保存 payload；并不允许删除 descriptor、
缩成零参数，或把这些名字迁移到 Player。完整 wrapper 证据见
`motionplayer_d3d_adaptor_ncb_surface_factory_four_binary_2026-08-14.md`。

## 本地偏差

旧 Player 实现把 payload 存入两个端口 vector：

```cpp
void Player::removeAllBg() { _backgrounds.clear(); }
void Player::removeAllCaption() { _captions.clear(); }
void Player::registerBg(tTJSVariant v) { _backgrounds.push_back(v); }
void Player::registerCaption(tTJSVariant v) { _captions.push_back(v); }
```

两个容器没有任何 renderer、getter、serializer 或销毁外的消费者；四方法也从未被 Player
registrar 绑定。它们会错误扩大 C++ source surface，并给 Player 增加两套 reference 中不
存在的 vector owner/lifecycle。

## 修正与验证

- 删除 Player 四个 declaration/body；
- 删除 `_backgrounds`、`_captions`；
- 删除 draw-cache smoke test 对两个死 Player method 的调用；
- 将真实 Player adaptor absence 回归扩展到四名，要求查询返回
  `TJS_E_MEMBERNOTFOUND` 且 result 保持不变；
- D3DAdaptor registrar、typed method declarations 和现有 wrapper 回归保持不变；
- 四份 recovery IDB 的 Player/D3DAdaptor registrar 已补 ownership 注释、强制反编译
  回读并原位保存；
- Player 四个旧方法和两个 vector 零匹配；仅 D3DAdaptor.h 保留四个 native method；
- motionplayer 测试 TU Emscripten syntax-only 通过；
- `Web Debug Build` 最终链接通过；
- 限定 `git diff --check` 无新增内容级 whitespace error，仅有既有 CRLF 提示。

