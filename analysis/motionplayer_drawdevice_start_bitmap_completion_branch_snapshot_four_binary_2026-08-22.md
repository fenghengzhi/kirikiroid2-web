# MotionPlayer `StartBitmapCompletion` 分支快照、区域容器与重入边界四二进制审计（V275）

日期：2026-08-22  
范围：`DrawDeviceObjectBase::StartBitmapCompletion(iTVPLayerManager *)`，以及它直接观察的
draw-buffer bitmap、render manager、completion update-region 与纹理字段。  
参考：Android arm64-v8a、Android armeabi-v7a、iOS arm64、iOS armv7 四份
`reference/binaries/` recovery database。  

## 1. 本轮结论与被纠正的旧结论

四份参考二进制完全一致地证明：`StartBitmapCompletion` 不读取根对象的
`CurrentTarget`。它从传入 manager 的 draw buffer 开始，并在该 bitmap 自身的 render
target/reference 之间调用 `FillARGB`。

当前源码此前还有一个更隐蔽的顺序偏差：它在 software/GPU 判定之前统一取得 target 和
reference。参考实现把两者放在分支内部，而且两条分支的顺序不同：

```text
software:
  snapshot update-region Count/Head
  reference = bitmap raw texture
  target    = bitmap->GetTextureForRender(false, nullptr)

GPU:
  width  = bitmap->GetWidth()
  height = bitmap->GetHeight()
  target    = bitmap->GetTextureForRender(false, nullptr)
  reference = bitmap raw texture
```

这不是无意义的编译器调度。`GetTextureForRender` 是虚调用，可以重入并替换 bitmap 的 raw
texture；所以 software 固定观察转换前的 reference，GPU 固定观察转换后的 reference。
把取得动作提前到 predicate 之前，还会改变 predicate、region 获取或 target 转换抛异常时已
发生的副作用。

本轮已同步修正两份旧分析中的过时叙述：

- `motionplayer_drawdevice_multiple_inheritance_vtables_completion_lifecycle_four_binary_2026-08-15.md`
  原先误写为 `CurrentTarget->GetTextureForRender`；
- `motionplayer_drawdevice_manager_legacy_texture_lock_tail_four_binary_2026-08-15.md`
  原先误写为 completion 直接向根 `CurrentTarget` 操作。

另外两份既有报告中“completion 不读取 `CurrentTarget`”的结论仍然正确。

## 2. 四端入口与 ABI

| 目标 | 入口 | 函数大小 | 本轮 recovery 名称 |
|---|---:|---:|---|
| Android arm64 | `0x531E7C` | `0x278` | `DrawDeviceObjectBase__StartBitmapCompletion_guess` |
| Android armv7 | `0x495D10` | `0x198` | `DrawDeviceObjectBase__StartBitmapCompletion_guess` |
| iOS arm64 | `0x100234690` | `0x220` | `DrawDeviceObjectBase__StartBitmapCompletion_guess` |
| iOS armv7 | `0x2333D8` | `0x256` | `DrawDeviceObjectBase__StartBitmapCompletion_guess` |

四库统一应用保守 prototype：

```cpp
void __fastcall DrawDeviceObjectBase__StartBitmapCompletion_guess(
    void *self, void *manager);
```

`self` 在本函数中不参与任何业务状态读取。manager 没有 null guard；传 null 会在第一次虚调用
前后按平台 helper 实现崩溃。

## 3. 前缀数据流与 guarded statics

四端公共前缀严格如下：

1. `manager->GetDrawBuffer()`；仅检查返回 bitmap 是否为 null，null 时立即返回。
2. 从非空 bitmap 取得 render manager；没有 render-manager null guard。
3. 第一份函数局部 guarded static 初始化 `FillARGB` render method：
   `renderManager->GetRenderMethod("FillARGB", nullptr)`。
4. 第二份独立 guarded static 通过 method 的 `EnumParameterID("color")` 缓存 color ID。
5. 每次非空 bitmap 调用都执行 `method->SetParameterColor4B(colorId, 0)`；固定清零，不读取根
   `ClearColor`。
6. 此后才调用 `TVPIsSoftwareRenderManager()` 并进入两条分支。

64 位 vtable byte offsets 是 render manager `GetRenderMethod +0x38`、method
`EnumParameterID +0x10`、`SetParameterColor4B +0x38`、render manager
`OperateRect +0xA0`；32 位分别为 `+0x1C/+0x08/+0x1C/+0x50`。bitmap 的
`GetTextureForRender` 是 64 位 `+0x10`、32 位 `+0x08` 槽。

只有两个 guarded-static initializer 拥有编译器生成的 guard-abort 清理。A64 的两个
`__cxa_guard_abort` landing 分别由 method lookup 与 color-ID lookup 的异常进入。普通
`SetParameterColor4B`、predicate、bitmap virtual、尺寸 helper 和 `OperateRect` 的异常都直接
逃逸；函数没有 target/reference AddRef、Release、RAII 或 rollback。

## 4. GPU 分支的精确顺序

四端 GPU 分支都先构造 full bitmap rect，再接触纹理：

1. `bitmap->GetWidth()`；
2. `bitmap->GetHeight()`；
3. 构造 `{0, 0, width, height}`；
4. 从已快照的 render manager 取得 `OperateRect` 槽和已缓存 method；
5. `bitmap->GetTextureForRender(false, nullptr)`，结果作为 target；
6. 读取 bitmap raw texture 字段，结果作为 reference；
7. 用空 source-texture array 调用一次 `OperateRect`。

关键指令序列：

| 目标 | width/height 起点 | target virtual | reference raw load | submit |
|---|---:|---:|---:|---:|
| A64 | `0x531F98` | `0x531FD4` | `0x531FD8` | `0x531FF4` |
| A32 | `0x495E48` | `0x495E76` | `0x495E7A` | `0x495E8C` |
| I64 | `0x10023479C` | `0x1002347D8` | `0x1002347E0` | `0x1002347F8` |
| I32 | `0x2335C6` | `0x2335F8` | `0x2335FC` | `0x233610` |

宽高在 target virtual 之前读取，因此该虚调用即使重入并改变 bitmap 尺寸，本次 rect 仍使用
转换前尺寸；reference 则是转换后的 live raw texture。target/reference 都不判 null。

## 5. software 分支的 update-region 快照

software 分支先直接读取具体 `tTVPLayerManager` 内嵌 `tTVPComplexRect` 的 Count 与 Head，
等价于内联 `GetUpdateRegionForCompletion().GetIterator()`：Count 非零时 iterator Head 为容器
Head，否则为 null。

| ABI | region Head | region Count | node rect | node Next | bitmap raw texture | target Width/Height |
|---|---:|---:|---:|---:|---:|---:|
| LP64 | manager `+0xB0` | manager `+0xC0` | node `+0x00..0x0F` | node `+0x18` | bitmap `+0x58` | target `+0x0C/+0x10` |
| ILP32 | manager `+0x5C` | manager `+0x64` | node `+0x00..0x0F` | node `+0x14` | bitmap `+0x40` | target `+0x08/+0x0C` |

快照完成后才按如下顺序取得纹理：

1. 直接读取 bitmap raw texture 字段为 reference；
2. 调用 `bitmap->GetTextureForRender(false, nullptr)` 为 target；
3. 检查 iterator Head 是否为空；空时返回。

因此空 update region 也不会省略 reference 快照和 target 转换。只有转换完成后才因 Head=null
退出。若 target virtual 重入并替换 bitmap raw texture，本次 software submit 仍保留转换前
reference。

关键位置：

| 目标 | Count/Head snapshot | reference raw load | target virtual 完成 |
|---|---:|---:|---:|
| A64 | `0x531F88..0x531F90` | `0x532004` | `0x532018` |
| A32 | `0x495DCC..0x495DDA` | `0x495DE0` | `0x495DEA` |
| I64 | `0x10023478C..0x100234794` | `0x100234804` | `0x100234820` |
| I32 | `0x23353C..0x23354A` | `0x233550` | `0x233560` |

## 6. intrusive circular-list 遍历与边界行为

iterator 保存一个固定 Head 与一个初始 null Current。第一次 `Step()` 令 Current=Head；后续
步骤读取 `Current->Next`，若 Next==Head 则终止，否则推进到 Next。二进制中的紧凑循环与当前
`tTVPComplexRect::tIterator` 源码完全一致，不是 STL iterator。

每个成功 step：

1. 把当前 node 前 16 bytes 复制为 `tTVPRect`；
2. 从同一个固定 target 指针重新读取 direct Width；
3. 以 unsigned 比较 `Width < (unsigned)rect.right`；
4. 仅当 width 通过时，再重新读取 direct Height，并比较
   `Height < (unsigned)rect.bottom`；
5. 任一比较失败立即 `break` 整个区域循环；
6. 否则用固定 render manager、method、target、reference 和当前 rect 调用
   `OperateRect`，再推进 Next。

没有 left/top 负数检查，没有 `rect.right < rect.left` 或 bottom/top 顺序检查，也没有把越界
rect clip 到 target。负的 right/bottom 转为 unsigned 后通常成为大正数并触发整个循环 break。
恰好等于 Width/Height 可以提交。

target pointer 本身只由一次转换取得并固定，但 Width/Height 在每个 rect 前重新读取；所以前
一次 `OperateRect` 若重入并修改同一 target 的 direct 尺寸，下一 rect 会观察新尺寸。若它释放
target，则下一次 direct load 是原版的 UAF 边界。render manager、method、target、reference
都不在每次迭代重采样。

region Head 同样是转换前快照。虚拟 target 转换或 `OperateRect` 若重入并清空/重建 update
region，旧 Head/Current/Next 仍被继续使用；参考实现没有 generation counter、锁或重启逻辑。

## 7. 对象生命周期和失败面

本函数对 bitmap、render manager、method、target、reference、region node 全部按 borrowed
pointer 使用：

- 不 AddRef bitmap/target/reference；
- 不 Release 任何纹理；
- 不拥有 region node；
- 不检查 method、target、reference 或 node-next 是否为 null；
- 不 catch 普通业务异常；
- 仅 static-local guard initializer 失败时调用 `__cxa_guard_abort`，让后续调用可以重试该
  static 初始化。

第一次 method lookup 若正常返回 null，guard 仍会发布 null；随后第二 static 或每次
`SetParameterColor4B` 会按当前 guard 状态在 null 上崩溃。若 color-ID initializer 成功，之后
method 不会重新通过当前 render manager lookup；它是进程级函数局部 cache，而本次
`renderManager` 仍用于 `OperateRect`。

## 8. 源码落点

`cpp/plugins/DrawDeviceD3D.cpp` 的实现已改为：

- predicate 前不再取得 target/reference；
- software：iterator 构造 → reference → target → loop；
- GPU：width/height rect → target → reference → submit。

收尾源码扫描结果：本函数块 `CurrentTarget=0`、software predicate `=1`、
`GetTextureForRender` `=2`、`OperateRect` `=2`；iterator 在 software reference 之前，software
reference 在 target virtual 之前。当前编译源码和测试中八个本轮参考地址匹配数为 0。

没有为这一顺序增加伪造的公开 seam。现有测试目标无法替换具体 `tTVPLayerManager` 的私有
draw-buffer pointer；本轮以四份本机指令、源码语句顺序和两份 Wasm object 定向反汇编三重
确认，避免为了测试方便扩张产品 ABI。

## 9. Wasm 构建与定向反汇编

- ordinary 与 `KRKR2_WASMTIME_HEADLESS=1` 两种完整
  `motionplayer-dll.cpp` syntax-only 均 exit 0；只有仓库既有 `_tss` deprecated warning；
- Web、Wasmtime main、Wasmtime guest 均成功重编/链接；之后三目标均
  `ninja: no work to do`；
- 两个 CTest 目录均 exit 0，且准确报告 `No tests were found!!!`；
- 三份 Wasm 均 `WebAssembly.validate=true` 并成功构造 Module；imports/exports 未变；
- Wasmtime main/guest object 的符号均为
  `DrawDeviceObjectBase::StartBitmapCompletion(iTVPLayerManager*)`。

Wasmtime object 定向反汇编显示：

- software：先调用 region getter/iterator constructor，再调用 raw `GetTexture` accessor，
  最后执行 bitmap vtable `+0x08` target virtual；
- GPU：先调用 width、height helper并构造 rect，再执行 bitmap vtable `+0x08`，之后才调用
  raw `GetTexture` accessor；
- 两条路径都位于 software predicate 的不同分支中。

最终产物：

| 产物 | bytes | imports/exports | SHA-256 |
|---|---:|---:|---|
| Web `index.wasm` | 85655133 | `539 / 69` | `98CF2BCB5DCB94D07B545F73DAA64C45AF4C70C1E25AA95A22B41476E40DC479` |
| Wasmtime `index.wasm` | 85002274 | `538 / 69` | `65B1F0FC0B0730D2A6634C2C2A2F0823B7488D744B7D1428F74B70C45426C65D` |
| Wasmtime guest | 151508371 | `445 / 87` | `3C7C465D5D1BD625242A71177D91B97F81E6E16593E8246DB5147EF6A5D97662` |

相对 V274，总大小分别为 `+39 / +39 / +141` bytes。当前关键 section payload：

| 产物 | FUNCTION | GLOBAL | CODE | DATA | `.debug_str` |
|---|---:|---:|---:|---:|---:|
| Web | `0x1BD30` | `0xD5C2` | `0x1A40FFF` | `0x5A3E40` | — |
| Wasmtime main | `0x1BA4F` | `0xD5EA` | `0x19E8FAD` | `0x5A1090` | — |
| guest | `0x16190` | `0xB1C3` | `0x13D7E10` | `0x4D1630` | `0x1530816` |

## 10. recovery IDB 写回与验证

四库各写入 7 条注释、1 个 bookmark、1 个保守函数名、1 个 prototype，共计：

- comments：28；
- bookmarks：4；
- renames：4；
- function types：4；
- 写入后强制 decompile readback：4；
- 最终 canonical auto-analysis-ready fresh readback：4。

本机 9.4 standalone `idat.exe` 是薄 IDALib launcher，实际启动没有继承 MCP 的许可环境并报
`license.txt` unavailable；一次试探性 `ida.exe -A` 产生的单独 GUI 进程已按 PID 立即停止，
没有保存数据库。最终改用 MCP 所使用的同一 IDALib 引擎执行
`run_auto_analysis=true -> save -> close`，四库 health 均为
`auto_analysis_ready=true`，随后 fresh decompile 均回读成功。最终 IDA/IDALib worker/session
审计为零。

iOS armv7 继续采用不同路径 candidate：先从 V274 canonical 六组件复制
`candidate-v275`，只在 candidate 写入并强制 readback；确认后才逐组件发布到 canonical。
fresh open 会删除空的 loose `.id2`，该 92-byte 组件从未修改，随后从 canonical 恢复到
candidate 并验证 hash 相同；其余实际变更组件在发布时逐项 hash identical。canonical 最后
单独完成 auto-analysis/save 和 fresh readback。

最终 canonical IDB：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android arm64 | 368547291 | `BA542D8D037E0C82ABCD6AB1511902BD40C58869288597F8684BCD53EE5FFDF8` |
| Android armv7 | 346739012 | `E960A708E01D8CB68C9415DA38E27068681D85F7DECB58870CBD935913560C8A` |
| iOS arm64 | 336228210 | `6EE958930E15D91F854AED6300D3D8ED724C1C8BB23A75A40D316A588A1E42EE` |
| iOS armv7 | 377098090 | `DB289635FE0F48712D66F681A324301577A6A42D5575864FB45B59196BEB85BD` |

pre-V275 backup 与 iOS candidate 位于：

`out/idb-recovery/v275-start-bitmap-completion/`

pre-backup hash：

| 目标 | bytes | SHA-256 |
|---|---:|---|
| Android arm64 | 368547695 | `FA7E0ECBA06E0DEF708F70B7D44CCD365902EC490CA08B83AD0DED80B3990063` |
| Android armv7 | 346744412 | `C9A9FFF807AE720C84706242E035004ADC55EA387CC26A059F8C6577C8916461` |
| iOS arm64 | 336228302 | `09C1761AD4B3E37D601BC86C4D941B3228E61C9CEFDA465522FCA5744D8BEA0B` |
| iOS armv7 | 377057222 | `6B564B884AEC76E3AD65FE0DCA5A57140A553343FB17778BC8C97294EEA15ECC` |

candidate 在 canonical auto-save 前为 377098090 bytes / 
`975C29185D85C4B41520251FC9882A758425B8E965CE4556074C6E49ED603161`；最终 canonical 因独立
auto-save/packing metadata 拥有不同 hash，但语义 fresh readback 完全一致。

## 11. 本轮未扩张范围

本轮闭合的是 completion 入口本身的 source order、region iterator ABI、reentry/exception
边界。`FillARGB` method 在具体 software/OpenGL render manager 内部如何解释空 source array、
如何处理非法 target/reference 和越界 rect，仍属于下游 render-manager 独立纵切面；本文不从
portable 实现反推四份参考的 backend 内部行为。
