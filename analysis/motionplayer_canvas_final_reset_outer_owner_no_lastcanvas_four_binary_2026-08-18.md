# ordinary Canvas 最终 clip reset、函数级 owner 与无 lastCanvas 字段（V246，2026-08-18）

## 1. 结论

四个参考二进制共同证明，ordinary Canvas submitter 在进入 item loop 前依次取得两个函数级 raw
Object owner：

```text
Layer class accessor Object
target Layer accessor Object
```

target accessor 不是直接借用按值参数中的 dispatch。它先从 by-value target 构造一个 call-local
`tTJSVariant` CopyRef，再由 `AsObject()` 额外保留 Object，随后在任何 `width`/`height` property
callback 前析构 temporary closure；因此跨越整个 item loop 的只是 target raw Object retain。

无论 main list 一开始为空，还是任意数量 item 全部完成，控制流最终都汇入同一个无参数
`Layer.setClip` 调用：

```text
flags    = 0
member   = "setClip"
hint     = shared setClip member hint
result   = nullptr
argc     = 0
argv     = nullptr
ObjThis  = target raw Object
```

普通负 HRESULT 被忽略。正常返回后严格先 Release target raw Object，再 Release Layer-class raw
Object，然后返回。若最终 `setClip` 真正抛出 C++ 异常，则以相同 target -> Layer 顺序展开；此时
所有 per-item descriptor/color/source、buffered ResourceManager/bufLayer/buffer 和 debug-frame 临时量
都已经离开作用域。

最重要的结构修正是：四端 Canvas 函数都没有把 target 写回 Player，Player 构造/析构也证明
draw-affine 六标量前面的 24-byte native 区域是无析构 POD，而不是 `tTJSVariant`。因此本地曾存在的
`Player::_lastCanvas` 不只是“本函数不应写”的 dormant owner；这个成员本身就不存在于参考类布局。

## 2. 四文件函数映射

| 参考文件 | complete Canvas submitter | Player constructor | Player destructor |
| --- | ---: | ---: | ---: |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `Player_renderToCanvas_guess@0x6C4820` | `Player_ctor_guess@0x6CC110` | `Player_dtor_guess@0x6CCEBC` |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `Player_renderToCanvas_guess@0x58E2CC` | `Player_ctor_guess@0x5935C4` | `Player_dtor_guess@0x593C24` |
| `Kirikiroid2_1.3.9_iOS_arm64` | `Player_renderToCanvas_guess@0x1001186E0` | `Player_ctor_guess@0x10011EC04` | `Player_dtor_guess@0x10011F2A0` |
| `Kirikiroid2_1.3.9_iOS_armv7` | `Player_renderToCanvas_guess@0x11653C` | `Player_ctor_guess@0x11D488` | `Player_dtor_guess@0x11DCC4` |

地址只在对应文件内有效。private stripped 符号均保留 `_guess`；iOS armv7 本轮还把 Canvas 函数
签名收紧为四个 pointer argument、`void` 返回，避免反编译器继续传播旧 Boolean/publication 解释。

## 3. 共同源码形状

最可能生成四份参考产物的共享 C++ 结构是：

```cpp
void Player::renderToCanvas_guess(tTJSVariant target, ...) {
    ncbPropAccessor layerClass{getLayerClass()};
    ncbPropAccessor targetLayer{tTJSVariant(target)};

    const int width = targetLayer.GetValue<int>(TJS_W("width"));
    const int height = targetLayer.GetValue<int>(TJS_W("height"));
    // prepare projection and walk every admitted item
    for(each item) {
        // all per-item and nested owners end inside this loop body
    }

    layerClass.Object->FuncCall(
        0, TJS_W("setClip"), &setClipHint,
        nullptr, 0, nullptr, targetLayer.Object);
    // ignored ordinary HRESULT
} // target raw Release, then Layer-class raw Release
```

这里的伪代码强调 owner/dispatch 拓扑，不宣称 `ncbPropAccessor` 是 stripped 源码中的精确变量名。
参考实现没有 final Layer `Update(false)`、没有成功 Boolean、没有 target publication，也没有
`_lastCanvas` member destruction。

## 4. 最终 reset 正常路径逐端映射

| phase | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| final-tail entry | `0x6C63AC` | `0x58FA62` | `0x100119F08` | `0x117F7C` |
| no-arg `setClip` call | `0x6C63DC` | `0x58FA82` | `0x100119F38` | call-site 7 at `0x117F9E`; call `0x117FAE` |
| target raw Release | `0x6C63E0..0x6C63F0` | `0x58FA84..0x58FA8C` | `0x100119F3C..0x100119F4C` | `0x117FB0..0x117FC8` |
| Layer-class raw Release | `0x6C63F4..0x6C6414` | `0x58FA92..0x58FAAA` | `0x100119F50..0x100119F70` | `0x117FCA..0x117FEA` |

四端的 empty-list branch 和 loop-exhaustion branch 都指向这一 final-tail entry。最终调用不构造参数
Variant 数组；其 `argc/argv` 是精确的 `0/nullptr`，receiver 是 raw target Object，不是 closure
ObjThis。`setClip` 返回值没有保存或测试。

## 5. 最终 reset 异常路径

Android arm64 在正文外显式保留 final call 的 landing：`0x6C645C` 捕获后跳到共同 outer cleanup，
依次经过 target `0x6C6D4C`、Layer class `0x6C6D5C`，再继续 resume。它与正常返回的 owner 逆序完全
一致。

iOS armv7 的 SjLj `call_site = 7` 经 selector case 6 (`0x11813E`) 到 `0x11819C` 捕获异常，随后：

```text
0x118946..0x118964  release target raw Object
0x118990..0x1189A2  release Layer-class raw Object
                     resume the active exception
```

Android armv7 与 iOS arm64 通过各自 EH/unwind metadata 表达同一源级析构区间，主函数正文没有渲染
出与另两端完全同形的 cold block。这里保留 ABI 差异，不复制不存在的 landing address。

## 6. Player 布局反证：`_lastCanvas` 整个字段不存在

为了区分“Canvas 不写某个 dormant Variant”与“Player 根本没有该 Variant”，本轮 fresh
decompile/disasm 了四端 Player constructor/destructor。紧邻 draw-affine 六标量之前的区域如下：

| target | pre-affine POD range | draw-affine begins | constructor evidence | destructor evidence |
| --- | --- | --- | --- | --- |
| Android arm64 | `Player+0x310..+0x327` | `+0x328` | `0x6CC4E4..0x6CC4E8` block zero | `0x6CD0B0` 从 `+0x360` nontrivial owner 跳到 `+0x308/+0x300` strings，跳过 POD |
| Android armv7 | `Player+0x200..+0x217` | `+0x218` | `0x5937FE` block zero | `0x593CE8` 跳过 `+0x200..+0x24F`，继续 `+0x1FC/+0x1F8` strings |
| iOS arm64 | `Player+0x2A0..+0x2B7` | `+0x2B8` | `0x10011ECF4` block zero | `0x10011F37C` 跳过 `+0x2A0..+0x2EF`，继续 `+0x298/+0x290` strings |
| iOS armv7 | `Player+0x1C0..+0x1D7` | `+0x1D8` | `0x11D84C` block zero | `0x11DDFA` 跳过 `+0x1C0..+0x20F`，继续 `+0x1BC/+0x1B8` strings |

若这里存在 `tTJSVariant`，四端都必须有相应 default construction/tag initialization，并在 destructor
中经过 Variant cleanup；实际四端只有 POD 零初始化且析构全部跨过该区。再结合四个 Canvas 函数零
field store，`_lastCanvas` 的 native-class-layout 解释被共同排除。

本轮只把该 24-byte 区域可靠地定性为 POD，尚未为其中每个字节捏造字段名；精确身份留给后续以
writer/reader xref 闭合的 Player layout 纵切面。

## 7. 本地实现修正

- `cpp/plugins/motionplayer/PlayerRenderTargets.cpp` 删除 Canvas wrapper 在 execute 后的
  `_lastCanvas = tTJSVariant(resolvedLayerObject, resolvedLayerObject)`；
- `cpp/plugins/motionplayer/Player.h` 删除整个 `_lastCanvas` Variant member，并把相邻 layout 注释改为
  “pre-affine 区是 POD，Player 不保留最近 Canvas target”；
- final no-argument `callLayerResetClip_guess(layerClassObject, renderLayerObject)` 保持在 top-level item
  loop 之后，HRESULT 继续被忽略；没有添加 catch、null guard、result Variant 或 fallback；
- 旧分析文档中仍把 `_lastCanvas` 当成可存在字段的表述，按 V246 证据改为“字段本身不存在”；描述
  旧 port 被删除行为的历史清单则保留，但明确它不是 native contract。

移除非平凡 Variant member 会改变本地 `Player` 构造/析构和嵌入对象的代码生成，因此本轮没有仅以
Canvas 单文件语法通过作为完成条件，而是重编了 Web、Wasmtime 和独立 guest 的完整依赖树。

## 8. 测试强化

现成 Canvas end-to-end probe 在 submit receiver 中识别 `setClip` 且 `argc == 0` 的 reset 调用，记录
当时 ResourceManager、buffer、source Object 和 distinct source ObjThis 的 Release 计数，并故意返回
`TJS_E_FAIL`。buffered inverted-viewport 与 direct affine 两轮都验证：

- 每轮恰有两次 no-argument reset：item 的 wrong-empty clip reset 与 loop 完成后的无条件 final reset；
- last reset 时 manager/buffer/source Object/source ObjThis 的快照都等于函数返回后的计数，证明所有
  per-item/nested owner 已在 final reset 前消失；
- direct 源码级 `continue` 仍先执行 per-item common cleanup，再进入 final reset；
- final reset 返回 `TJS_E_FAIL` 不改变控制流，也不阻止 owner 正常释放。

普通/headless 两套完整 motionplayer Catch2 TU 均通过 Emscripten syntax compile。Windows x64 Catch2
运行树仍要求重建当前缺失的整套 native third-party ports，本轮没有把该外部依赖缺口误报为源码
失败。

## 9. IDB 写回与 iOS armv7 安全保存

本轮四库共写回 48 条 comments、24 个 bookmarks：

- Android arm64：13 comments、6 bookmarks；
- Android armv7：10 comments、6 bookmarks；
- iOS arm64：10 comments、6 bookmarks；
- iOS armv7：15 comments、6 bookmarks，并恢复 `Player_ctor_guess`、`Player_dtor_guess` 两个
  semantic names；Canvas `void` 四-pointer signature 也已写回。

iOS armv7 继续使用 different-path packed save。Canvas 第一轮写入
`out/idb-recovery/v246-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.v246.i64`；加入 ctor/dtor layout
证据后的最终文件为同目录 `Kirikiroid2_1.3.9_iOS_armv7.v246-final.i64`。V245 canonical 与中间 V246
canonical 分别保留在 `pre-v246-canonical/`、`pre-v246-field-final/`，没有递归删除。

最终 packed copy 和 canonical 都是 376,330,448 bytes，SHA-256
`7EC963562DB69E2B4F23E74CA2E0F641341474C270D3B3E8611800D5E962E306`。两个 candidate 均经独立
`C:\IDA\idat.exe` reopen 退出码 0；canonical 又经 MCP reopen 读回 Canvas signature、四个 semantic
names、final reset 与 POD comments。最终 IDA session count 为 0。

## 10. 验证与产物

- complete motionplayer Catch2 TU ordinary/headless syntax：通过，仅有既有 `_tss` warning；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`：通过并完成 exnref 转换；
- compiled source `_lastCanvas/lastCanvas` 扫描：`cpp/` 与 `tests/` 零命中；
- scoped `git diff --check`：无 whitespace error，仅既有 LF/CRLF 提示；
- IDA session audit：0。

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Web `index.wasm` | 85,655,080 | `0x1BD31` | `0x1A40FAB` | `0x5A3E40` | `0x3185F7B` | `EE2A781753532571F91831FA75CB13AEEFD896B02B50C4A4A6FC65A283B220F6` |
| Wasmtime `index.wasm` | 85,002,221 | `0x1BA50` | `0x19E8F59` | `0x5A1090` | `0x3141E11` | `60E5041335BA27E4AEBA9C23F4C1546FB6F775AAFCEB5094902518D55DE7F94B` |

相对 V245，Web/Wasmtime 主 wasm 各精确缩小 172 bytes，且缩减全部落在 CODE；FUNCTION、DATA、
name/import/export 均不变。这是移除伪 Variant 成员构造/析构路径的预期代码生成结果，不是资源或
接口漂移。

`krkr2_wasmtime_guest.wasm` 为 151,477,998 bytes，SHA-256
`C33A6A68DF680D3D47E6493C1305633FC2B560170CE5E0C61C13BE5CC7E486C7`。guest 相对 V245 缩小
492 bytes；额外差异来自包含测试 TU 的 debug/line metadata，主模块的精确 CODE 缩减更适合作为
生产语义守护。

## 11. V247 后续补证

V247 已完成这里列出的下一边界：draw-affine 前 24-byte POD 是三个持久 camera velocity double；
它们位于 pending stealth string pair 后、draw-affine 前。camera damping 实际紧邻更早的 frame delta，
non-identity flag 则是 damping 后四 byte control group 的第四项，并不位于 affine/particle rect 后。
完整四端 offset、writer/reader、particle child commit 与最终 IDB/build 基线见
`analysis/motionplayer_player_camera_velocity_affine_layout_four_binary_2026-08-18.md`。
