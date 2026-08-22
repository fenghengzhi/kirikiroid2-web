# motionplayer `Player::getBounds` / `isValid` 共享 hint 与 Dictionary 生命周期（四参考二进制）

日期：2026-08-16

## 1. 结论

四个 `reference/binaries/` 的 `Player::getBounds` 具有同一份源级结构：

1. 先创建 fresh Dictionary，并让 `ncbPropAccessor` 直接接管 factory reference；
2. 固定先判 `maxY >= minY`，再判 `maxX >= minX`；
3. 无序时只发布 `isValid=false`，不发布任何几何字段，也不调用 binary64 classifier；
4. 有序时固定按 `left/top/right/bottom/width/height` 发布六个 Real；
5. 随后固定按 `minX/maxX/minY/maxY` 调用位级 classifier；四次结果全为 0 才发布
   `isValid=true`，任一次非 0 则发布 false；
6. 七个属性写入全部使用 `TJS_MEMBERENSURE`，返回状态全部忽略；
7. 返回值先在 accessor 仍存活时构造 `{dispatch, dispatch}` Object closure，随后 accessor
   析构并释放它接管的 factory reference。

`isValid` 是紧邻 `opacity` 之后的独立 32 位进程级 member-hint 槽，唯一 consumer 是
`Player::getBounds` 的三条结果分支。六个几何字段却不是 bounds 私有槽：它们全部复用插件
更早建立、被大量路径共享的 `left/top/right/bottom/width/height` 全局 cache word。旧移植在
`PlayerCore.cpp` 中另建七个 bounds-only 静态 hint，会让同名 TJS 成员在错误的第二套 mutable
cache word 中运行；本轮已删除这些重复槽。

本文绝对地址只作为四份参考二进制的分析坐标；编译源码只保留语义名和四端共同结论。

## 2. `isValid` 槽、literal、函数与 classifier 坐标

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `isValid` hint | `0x1AB5494` | `0x1111930` | `0x101B6995C` | `0x187D600` |
| UTF-16LE `isValid` | `0x14C9986` | `0x5924C8` | `0x10195C77A` | `0x174EADE` |
| `Player::getBounds` | `0x6C9E64` | `0x59226C` | `0x10011CBD4` | `0x11B53C` |
| binary64 classifier | `0xA0C7A0` | `0x75F618` | `0x1002583C4` | `0x259750` |

`isValid` 的精确 UTF-16LE pattern 为：

```text
69 00 73 00 56 00 61 00 6C 00 69 00 64 00 00 00
```

部分目标存在相同原始字节的无关命中；上表 literal 是按 `Player::getBounds` xref 过滤后的
真实成员名。global 的全部 code/data xref 去重后只落入本函数；由于 AArch64/Thumb 装载全局
地址的指令展开不同，原始 xref 数可能是每分支一个或两个，但语义 consumer 始终只有三条
`isValid` publication。

左边界是 V160 已恢复的 `opacity`，右边界是 `Player::initNonEmoteMotion` 使用的 `parameter`
hint；该右边界已由 V162 继续闭合为进程级全局槽：

| 边界 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| previous `opacity` | `0x1AB5490` | `0x111192C` | `0x101B69958` | `0x187D5FC` |
| next `parameter` global hint | `0x1AB5498` | `0x1111934` | `0x101B69960` | `0x187D604` |
| next consumer | `0x6B0A3C` | `0x580C28` | `0x100108258` | `0x1058F8` |

四端因此都有严格的 `opacity -> isValid -> parameter`、4-byte stride 布局；不能把
`isValid` 或 `parameter` 留在 `PlayerCore.cpp` 的独立静态变量区。`parameter` 的 consumer、
literal、右邻槽和 init 分支证据见
`analysis/motionplayer_init_parameter_hint_global_boundary_four_binary_2026-08-16.md`。

## 3. 六个几何字段复用既有共享槽

`getBounds` 六次 Real publication 使用下列既有 process-wide hint：

| 槽 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `width` | `0x1AB520C` | `0x1111740` | `0x101B696D4` | `0x187D404` |
| `height` | `0x1AB5210` | `0x1111744` | `0x101B696D8` | `0x187D408` |
| `left` | `0x1AB5224` | `0x1111758` | `0x101B696EC` | `0x187D41C` |
| `top` | `0x1AB5228` | `0x111175C` | `0x101B696F0` | `0x187D420` |
| `right` | `0x1AB522C` | `0x1111760` | `0x101B696F4` | `0x187D424` |
| `bottom` | `0x1AB5230` | `0x1111764` | `0x101B696F8` | `0x187D428` |

完整四端 xref 审计表明 `width/height` 至少跨下列规范 consumer 复用；某些 AArch64 helper
被内联、某些 Thumb tail chunk 没有 function owner，因此原始函数计数不同，但语义集合一致：

- `MotionNode::findSource`；
- `MotionLayer::clearWholeLayer`；
- BezierPatch bounds 计算路径；
- `ResourceManager::findSource`；
- `Player::updateLayers` vertex computation；
- `Player::resolveRenderSource`；
- render-command builder 与 `renderToCanvas`；
- accurate SLA renderer；
- `Player::getBounds`；
- internal render-layer materialization 与 accurate-SLA post-draw update；
- `Player::calcViewParam` 与 `Player::getCommandList`。

`left/top/right/bottom` 另外由 `ObjSource::getClip`、BezierPatch reverse calculation 等路径
共享；`left/top` 还由 SLA assign 使用。由此可排除“同名但函数私有”的解释：机器码地址身份
证明 `getBounds` 必须复用 `motion::detail` 中现有的六个 generic hint。

## 4. 控制流与发布顺序

四端可统一还原为：

```text
result = ncbPropAccessor(TJSCreateDictionaryObject(), addref=false)

if maxY < minY or maxX < minX:
    ignore result.SetValue("isValid", false, MEMBERENSURE, shared isValid hint)
else:
    ignore result.SetValue("left",   minX,          MEMBERENSURE, shared left hint)
    ignore result.SetValue("top",    minY,          MEMBERENSURE, shared top hint)
    ignore result.SetValue("right",  maxX,          MEMBERENSURE, shared right hint)
    ignore result.SetValue("bottom", maxY,          MEMBERENSURE, shared bottom hint)
    ignore result.SetValue("width",  maxX - minX,   MEMBERENSURE, shared width hint)
    ignore result.SetValue("height", maxY - minY,   MEMBERENSURE, shared height hint)

    valid = classify(minX) == 0 &&
            classify(maxX) == 0 &&
            classify(minY) == 0 &&
            classify(maxY) == 0
    ignore result.SetValue("isValid", valid, MEMBERENSURE, shared isValid hint)

dispatch = result.GetDispatch()
return ObjectClosure(dispatch, dispatch)
// result accessor releases adopted factory reference after return object exists
```

两个次序细节需要保留：

- ordering gate 是 Y 后 X，不是旧源码的 X 后 Y；
- classifier 是 X-min、X-max、Y-min、Y-max，不跟 ordering gate 的轴次序相同。

比较本身没有脚本回调，但固定次序仍是四份代码生成共同反映的真实源结构。更重要的是，
invalid-order 分支根本不发布六个几何字段；ordered-but-invalid 分支则已经发布完整几何前缀，
之后才写 false。对负数、负零或 Infinity，这会形成七键 Dictionary 且 `isValid=false`，不是
回退成只有一键的无效形态。

## 5. binary64 classifier 的精确边界

四端 helper 都直接检查 binary64 exponent、fraction 与 sign bit，返回集合为：

| 输入类别 | 返回值 |
|---|---:|
| 非负、非 special（包括 `+0`、subnormal、普通 finite） | `0` |
| positive-sign NaN | `1` |
| `+Infinity` | `2` |
| 负 finite（包括 `-0` 和 negative subnormal） | `8` |
| negative-sign NaN | `9` |
| `-Infinity` | `10` |

`getBounds` 只接受 0。源码中的 `std::isfinite(value) && !std::signbit(value)` 正好表达
“classifier 结果为 0”，包括容易遗漏的 `-0` 拒绝边界。NaN 本身会使前面的 ordering
comparison 为 false，所以通常落入一键 invalid-order 形态；正负 Infinity 或有序负值可以
通过 ordering gate，先发布几何，再由 classifier 把 `isValid` 置 false。

## 6. Dictionary 与引用生命周期

反编译不是“裸 dispatch + 最后手工 Release”的结构，而是 allocating factory 与 accessor
所有权组合：

1. `TJSCreateDictionaryObject()` 的初始引用直接传给 `ncbPropAccessor(..., false)`；
2. `false` 表示 accessor 不再额外 AddRef，而是接管这份已有 factory reference；
3. 各 `SetValue` 在同一 accessor/Dictionary receiver 上运行，返回 bool 不参与任何分支；
4. 结果构造时，对同一个 dispatch 建立 owner/object-this 两指针 Object closure；
5. closure 构造完成后 accessor 析构，释放它接管的初始引用；返回 Variant 保有自己的引用；
6. 异常展开时 accessor 负责释放尚未发布的 Dictionary，不留下旧裸指针实现的手工清理窗口。

每次 property access 都重新调用 Dictionary factory，因此返回对象互不相同。EmotePlayer 的
`bounds` property 只是转发到其内嵌 Player，仍获得同样的 fresh Dictionary。

## 7. 源码与回归落地

- `MotionDispatch.h` / `RuntimeSupport.cpp`
  - 在真实 `visible -> setPos -> opacity` 序列之后补 `isValidMemberHint_guess`；
  - 保持它与后继 `parameter` 全局 hint 的语义边界（该边界由 V162 继续闭合）；
- `PlayerCore.cpp`
  - 删除 bounds-only `left/top/right/bottom/width/height/isValid` 七个重复静态槽；
  - 六个几何字段改为复用既有 `motion::detail` generic hint；
  - `isValid` 改为使用 V161 新闭合的共享槽；
  - ordering gate 改回 Y 后 X；
  - 使用 `ncbPropAccessor(TJSCreateDictionaryObject(), false)` 直接接管 factory reference；
  - 在 accessor 存活期构造并返回 `{dispatch,dispatch}` closure；
- `motionplayer-dll.cpp`
  - 把 `isValid` 纳入 V160 后继槽地址互异检查；
  - 在现有 bounds 大型回归中锁定七个 publication hint 彼此独立；
  - 继续覆盖 fresh Object、一键 invalid shape、六 Real + Boolean shape、负坐标、`-0`、Infinity、
    NaN 输入传播、EmotePlayer forwarding 与 calcBounds 相关边界。

## 8. IDB 回写

四份 recovery IDB 均完成并原位保存：

- `isValid` 建为独立 4-byte `unsigned int`，命名
  `g_motion_isValidMemberHint_guess`；
- 给槽、三条 publication、`Player::getBounds`、classifier 和 parameter-list 右边界补注释；
- 添加 `V161 complete Player getBounds isValid member-hint/lifecycle` bookmark；
- fresh xref 审计发现旧库把 `width` 起点错误描述成 16-byte item、四个边界槽描述成 1-byte
  item，因此重新建立：
  - `width/height/originX/originY` 四个独立 4-byte item；
  - `left/top/right/bottom` 四个独立 4-byte item；
  - `originX/originY` 虽不是本函数 consumer，但在 undefine 16-byte 错误 item 后一并精确保留；
- 六个 geometry slot 改用 `g_motion_<name>MemberHint_guess`，强调其共享身份；
- force-recompile `Player::getBounds` 后，四端 fresh pseudocode 都各出现一次六个 geometry 名和
  三次 `g_motion_isValidMemberHint_guess`；
- fresh globals 回读确认上述槽全部 `size=4`；
- 四份数据库全部保存成功。

## 9. 验证

2026-08-16 最终完成：

- ordinary `motionplayer-dll.cpp -fsyntax-only`：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax-only：通过；
- Web Debug 首轮 header 重建 `35/35`、共享几何修正增量 `3/3`、最终工作树重建
  `35/35`：均成功链接；
- Wasmtime Headless Debug 首轮 header 重建 `68/68`、共享几何修正增量 `4/4`、最终工作树
  重建 `68/68`：均成功链接；
- Node `WebAssembly.Module` parse：两份最终 wasm 均通过；
- `llvm-objdump -h`：两份最终 wasm section table 均通过；
- Web wasm：`85,648,168` bytes，539 imports / 69 exports；
- Headless wasm：`84,995,309` bytes，538 imports / 69 exports；
- 相比 V160，两份 wasm 都精确减少 733 bytes，import/export 数不变；
- 两个 CTest build tree 均可运行，但当前仍报告 `No tests were found`；新增测试代码由 ordinary/
  headless 两种 test-TU syntax 编译覆盖，不能声称已执行 Catch2 runtime；
- warning 仍只有既有 `_tss`、`nodiscard`、pthread memory-growth、JSPI experimental 与
  JS-library warning，没有 V161 新 error。
