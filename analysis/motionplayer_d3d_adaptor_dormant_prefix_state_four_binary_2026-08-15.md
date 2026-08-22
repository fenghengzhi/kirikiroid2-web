# MotionPlayer D3DAdaptor 前缀 dormant int32 四参考恢复（2026-08-15）

## 结论

`D3DAdaptor` 的四个尺寸/中心整数与五个布尔字节之间还存在一个独立 32 位成员：

```text
width, height, centerX, centerY
int32 dormantState_guess = 0
visible, canvasCaptureEnabled, clearEnabled, resizable, alphaOpAdd
```

它不是 alignment padding。四份构造函数都显式写 0，但完整 adaptor/Player 插件 consumer
surface 中没有构造后的 read/write，析构也不处理。当前可证明的源级结构是一个
constructor-only scalar；原始字段名与历史用途仍未知，故保留 `_guess`，不猜成位置、颜色、
帧号或第二个尺寸字段。

## 四端构造写入

| 目标 | ctor | 写点 | 指令形状 |
|---|---:|---:|---|
| Android arm64 | `0x6AAEF0` | `0x6AAF0C` | `STR X8,[self,+0x10]`；低 word 为 0，高 word 同时发布四个布尔默认字节 |
| Android armv7 | `0x57D0AC` | `0x57D0C6` | `STRD R0,R1,[self,+0x10]`；第一 word 为 0，第二 word 含布尔默认组 |
| iOS arm64 | `0x100103FA8` | `0x100103FC8` | 独立 `STR WZR,[self,+0x10]` |
| iOS armv7 | `0x10128C` | `0x1012B4` | 独立 `STR zero,[self,+0x10]` |

布尔默认组四端一致：`visible=0`、`canvasCaptureEnabled=0`、`clearEnabled=1`、
`resizable=0`，随后另写 `alphaOpAdd=0`。Android 的宽 store 不能把 `+0x10` 误判成
布尔 padding，因为 iOS 两端为它生成了独立的 32 位 store。

## consumer 审计范围

四份 recovery IDB 先统一应用 `D3DAdaptor_layout_guess *` 到以下完整 native surface：

- `setPos`、`setSize`、`setClearColor`、`setResizable`、`removeAllTextures`；
- 五个真实 nullsub：`removeAllBg`、`removeAllCaption`、`registerBg`、
  `registerCaption`、`unloadUnusedTextures`；
- `visible`、`alphaOpAdd`、`canvasCaptureEnabled`、`clearEnabled` 的 getter/setter；
- `captureCanvas`、ctor、dtor、target release、`clearTargetTexture`、
  `renderFromPlayer`；
- `Player_renderToD3DAdaptor`、共享 raw texture renderer、source-texture callback 与
  target/reference callback，以及相应 NCB factory/typed wrappers。

定型后重新反编译，只有 ctor 的 `self->dormantState_guess` 命中。Android arm64 伪代码中
另外出现的 `self[1].dormantState_guess` 是不完整前缀类型跨到后置 GNU map header 的显示
伪影；对应真实地址是对象 `+0x48/+0x58` 的树节点/root 清理，不是前缀 `+0x10`。
Android armv7 的同类 `self[1]` 显示也落在 `+0x34` 之后的 map header。

## 原始指令复扫与误命中排除

除 typed-field 检索外，本轮逐条复核了 exact `+0x10` 的直接 load/store、从相邻 word
开始覆盖它的 pair 访问，以及 `ADD/ADD.W +0x10` 地址形成：

- ctor 之外，capture 中的 `+0x10` 属于 Layer texture 的 width、候选 texture 比较或
  texture vtable 槽；
- dtor/target release/clear 中的 `+0x10` 属于 texture/manager vtable 槽；
- source-texture callback 中的 `+0x10` 属于 Player source descriptor、Layer、树节点或
  member-hint global；
- render callback 的其余命中全是 stack `std::function`/callable 布局；
- `Player_renderToD3DAdaptor` 只 prepare/project 后调用 `renderFromPlayer`，不直接读取
  adaptor 前缀；target callback 只读取 target texture（64 位 `+0x30`、32 位 `+0x24`）。

因此该槽的完整当前生命周期是：

```text
allocation
  -> ctor explicit store 0
  -> no post-construction plugin access
  -> dtor skips scalar
  -> object storage deallocation
```

没有 getter、setter、copy/move、reset 或 exception-cleanup action。构造后即使内存被外部
破坏，当前插件行为也不会观察这个值。

## 源码与 IDB 落点

- `D3DAdaptor.h`：把会误导为 padding/offset placeholder 的
  `_reserved_0x10_guess` 改为 `_dormantState_guess`，保留 `=0` 与未知名后缀；
- 原 D3DAdaptor 四参考文档的对象布局表补回遗漏的 `+16` 行；
- 四份 recovery IDB 的 prefix type 成员同步为 `dormantState_guess`，19/19/18/18 个
  core function signature 使用 typed self，ctor 写点和 dtor 入口补注释/书签后保存。

本结论只说明当前四份参考插件范围中的生命周期，不根据无 consumer 反推原始字段名，也
不声称链接外未知代码在其他版本从未使用过该槽。
