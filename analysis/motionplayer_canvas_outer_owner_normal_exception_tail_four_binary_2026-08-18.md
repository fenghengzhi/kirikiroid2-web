# ordinary Canvas per-item 外层 owner 正常/异常共同尾（V245，2026-08-18）

## 1. 结论

四个参考二进制共同证明，ordinary Canvas submitter 在每个已经通过 admission/clip/prior-draw
门控的 item 上构造以下外层 owner：

```text
descriptor raw Object accessor
color raw Object accessor
resolved source Variant(Object + ObjThis)
source raw Object accessor
```

这四个 owner 不仅跨越 direct/buffered 图像提交，还跨越该 item 的 debug-frame dispatch。所有正常
item 出口——包括 direct 分支源码级 `continue`、两种 style 都是 Void、unsupported mesh type 以及
buffered frame 返回——随后都进入同一个逆序清理尾：

```text
release source accessor raw Object
destroy resolved source Variant       // Object, then ObjThis
release color accessor raw Object
release descriptor accessor raw Object
advance item iterator
```

因此当前源码里的 direct `continue` 是同一 C++ scope 的源码表达；它不会绕过这四个局部对象的
析构，也不代表参考实现存在一条 owner 集合不同的机器码快捷路径。

回调抛出 C++ 异常时，已经构造的 direct/buffered/frame 分支临时量先按其局部作用域析构，然后仍以
`source accessor -> source Variant -> color accessor -> descriptor accessor` 顺序展开。Android arm64
显式保留了 cold cleanup chain；iOS armv7 用独立 SjLj landing dispatcher 表达同一链；Android armv7
与 iOS arm64 没有在主函数正文中渲染出同形 cold block，这是编译器/异常 ABI 表示差异，不是插件
生命周期差异。

## 2. 四文件函数映射

| 参考文件 | complete Canvas submitter | 状态 |
| --- | ---: | --- |
| `Kirikiroid2_1.3.9_Android_arm64-v8a.so` | `Player_renderToCanvas_guess@0x6C4820` | fresh decompile/disasm 已定位 |
| `Kirikiroid2_1.3.9_Android_armabi-v7a.so` | `Player_renderToCanvas_guess@0x58E2CC` | fresh decompile/disasm 已定位 |
| `Kirikiroid2_1.3.9_iOS_arm64` | `Player_renderToCanvas_guess@0x1001186E0` | fresh decompile/disasm 已定位 |
| `Kirikiroid2_1.3.9_iOS_armv7` | `Player_renderToCanvas_guess@0x11653C` | fresh decompile/disasm 已定位；SjLj dispatcher 单列于下文 |

四个地址仅在各自文件内有效。函数名带 `_guess`，因为 stripped 参考文件没有恢复出这个 private
源码符号的精确名字；语义来自四端共同调用链。

## 3. 共同源码形状

最可能生成四份参考产物的共享 C++ 结构是：

```cpp
for(each item) {
    if(rejected before owner construction) continue;

    ncbPropAccessor descriptor{tTJSVariant(sourceDescriptor)};
    publish descriptor fields;
    ncbPropAccessor color{tTJSVariant(sourceColors)};
    publish four colors;
    tTJSVariant source = resolveSource(...);
    ncbPropAccessor sourceAccessor{tTJSVariant(source)};

    if(useDirect) {
        submit direct primitive;
        drawDebugFrame(...);
        continue; // ordinary C++ destruction of all four outer owners
    }

    {
        construct buffered ResourceManager/bufLayer/buffer owners;
        optionally execute buffered image phase;
    } // nested owners die before frame
    drawDebugFrame(...);
} // source accessor -> source Variant -> color -> descriptor
```

若 direct mesh type 不受支持，源码级 `continue` 同样先展开这些外层 owner；若 outline 与 meshline
均为 Void，则只跳过 frame dispatch，不跳过 outer cleanup。只有更早的 admission/clip/prior-draw
门控发生在四个 owner 构造前，因此它们不需要清理该集合。

## 4. 正常共同尾逐端映射

| phase | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | --- | --- | --- | --- |
| common tail entry | `0x6C5DB0` | `0x58F59C` | `0x10011991C` | `0x117F1E` |
| source raw Release | `0x6C5DB4..0x6C5DD4` | `0x58F59C..0x58F5AE` | `0x10011992C..0x10011993C` | `0x117F24..0x117F34` |
| source Variant dtor | `0x6C5DD8..0x6C5DDC` | `0x58F5B0..0x58F5B4` | `0x100119940..0x100119944` | `0x117F36..0x117F3A` |
| color raw Release | `0x6C5DE0..0x6C5DF0` | `0x58F5B8..0x58F5CA` | `0x10011994C..0x10011995C` | `0x117F3E..0x117F54` |
| descriptor raw Release | `0x6C5DF4..0x6C5E0C` | `0x58F5CC..0x58F5DE` | `0x100119970..0x100119980` | `0x117F56..0x117F6C` |
| item-loop rejoin | `0x6C5E1C -> 0x6C639C` | `0x58F5E8 -> 0x58FA56` | `0x100119EF8` | `0x117F6E..0x117F78 -> 0x116BC6` |

Android arm64 的 common-entry code xrefs 为 `0x6C59CC`、`0x6C59E4`、`0x6C5B14`，另有
debug-frame 后的自然 fallthrough `0x6C5DAC`。Android armv7 对应 `0x58F286`（两种 style 均
Void）、`0x58F29C`（unsupported mesh type）、`0x58F398` 和 frame fallthrough。iOS arm64 对应
`0x100119580`、`0x100119598`、`0x1001196B0` 和 frame fallthrough。它们共同排除了“direct
continue 跳过外层析构”的解释。

iOS armv7 在每次可能抛出的 raw `Release` 前把 SjLj `fctx.call_site` 分别写为 `0x65`、`0x66`、
`0x67`；这些调用自身若在清理过程中再次抛异常，进入 terminate 路径。它们不改变正常 owner
次序，也不能被误读为三套业务 landing pad。

## 5. 异常共同尾

### Android arm64

Android arm64 显式 cold cleanup chain 为：

```text
0x6C6C20  release source accessor raw Object
0x6C6C3C  destroy resolved source Variant
           destroy any still-live branch locals
0x6C6D1C  release color accessor raw Object
0x6C6D2C  release descriptor accessor raw Object
           continue older enclosing unwind
```

不同抛出点可以跳入该链的不同前缀，但任何已经完整构造的外层 owner 都按正常尾的逆序展开。
普通负 HRESULT 不进入这条链；只有真正的 C++ 异常参与异常展开。

### iOS armv7 SjLj

`Player_renderToCanvas_guess` 在 `0x11659A` 把独立函数 `0x118026` 安装为 SjLj landing
dispatcher。本轮将其恢复为行为推断名 `Player_renderToCanvas_sjlj_cleanup_guess`；`_guess` 明确表示
参考文件没有保留精确 private 源码符号。

debug-frame `FuncCall` 前写入 `call_site = 0x5E`；personality 传给 dispatcher 的 switch selector
落在 case 93 (`0x118784`)。该路径为：

```text
0x118784              capture active exception for frame call
0x118790..0x118812    destroy frame argument/local Variants
0x11884E..0x11886C    release source accessor raw Object
0x11886E..0x118872    destroy resolved source Variant
0x1188AE..0x1188CC    release color accessor raw Object
0x118906..0x118924    release descriptor accessor raw Object
                       continue older enclosing unwind and resume
```

jump-table 尾部 `0x1189BE..0x1189C4` 汇合的是 cleanup `Release`/destructor 自身再次抛异常的
terminate handling，不是 frame callback 的通常异常清理，也不是不同 owner 顺序。

### Android armv7 / iOS arm64

这两端的主函数中没有单独渲染出与 Android arm64 cold blocks 或 iOS armv7 SjLj switch 同形的
代码区；其正常公共尾、所有源级 scope 出口和析构次序仍逐条一致。报告保留该机器码表示差异，
不把另一端的地址或异常 ABI 套用过来。

## 6. 本地实现对照与修正

`cpp/plugins/motionplayer/PlayerRenderExecute.cpp` 的运行结构已经匹配共同伪代码：

1. `descriptor`、`color`、`ResolvedSourceObject::object`、`sourceAccessor` 按四端顺序构造；
2. buffered 三层 owner 位于更内层 block，先于 debug frame 析构；
3. direct 分支在 frame 后执行 `continue`，由 C++ 正常 scope cleanup 释放四个 outer owner；
4. buffered 分支从 frame fallthrough 到 item scope 末端，释放同一集合；
5. 未添加 catch、null guard、HRESULT gate 或手工 cleanup。

本轮没有改变生产运行语义，只把过时注释“owner 仅跨越 direct/buffered copy”纠正为“owner 还跨越
debug-frame dispatch”，并明确 direct `continue` 仍执行共同逆序析构。

## 7. 测试强化

现成 Canvas submitter probe 增加 distinct source `Object`/`ObjThis` 在第一条 `drawLine` callback 的
Release 计数快照。buffered right-inverted 与 direct affine 两次执行都验证：

- buffered ResourceManager/bufLayer/buffer owners 在第一条 frame callback 前已经全部清理，callback
  后不再产生其 Release；
- source accessor raw owner 与 persistent source Variant 在 callback 时仍存活；submitter 返回前恰好
  再产生 2 次 source Object Release 和 1 次 distinct source ObjThis Release；
- direct 第二次执行没有重新读取 `bufLayer`，但仍提交一次 `operateAffine` 和四条 `drawLine`；
- direct `continue` 与 buffered fallthrough 的 outer source owner tail 完全相同。

descriptor/color 是 submitter 内部 persistent TJS objects；它们的精确相对位置由四端机器码共同尾
覆盖，测试不为此捏造新的外部 fixture。

## 8. IDB 写回与 iOS armv7 安全保存

本轮四库共写回 34 条 comments、16 个 bookmarks；iOS armv7 另恢复 1 个 `_guess` semantic
function name：

- Android arm64：10 comments、4 bookmarks；
- Android armv7：6 comments、4 bookmarks；
- iOS arm64：6 comments、4 bookmarks；
- iOS armv7：12 comments、4 bookmarks，并将 `sub_118026` 命名为
  `Player_renderToCanvas_sjlj_cleanup_guess`。

iOS armv7 遵循 different-path compressed save：写入
`out/idb-recovery/v245-ios-armv7/Kirikiroid2_1.3.9_iOS_armv7.v245.i64`，经独立
`C:\IDA\idat.exe` 重开退出码 0 后，以 `save=false` 关闭 live session；旧 canonical 与仍存在的
`.id0/.id1/.nam` 逐文件移动到 `pre-v245-canonical/`，再安装 packed copy。MCP 重开确认 submitter
名称、SjLj dispatcher 名称和正常/异常 comments 均存在，最终 `save=false` 关闭。没有递归删除。

- pre-V245 canonical：376,109,264 bytes，SHA-256
  `7D52D78D0E0BCCC352880DA3D8F362249C02BE60D86FAC3477E2AD63C29EA2EF`；
- final V245 canonical：376,199,376 bytes，SHA-256
  `CFC9D47698956EE0B3698BD8CABB30D2EBAE684FE26215606FCEFE71E2A17254`。

最终 IDA session count 为 0。

## 9. 验证与产物

- complete motionplayer Catch2 TU ordinary/headless syntax：通过；
- `cmake --build out/web/debug`：通过；
- `cmake --build out/wasmtime/debug`：通过；
- `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`：通过并完成 exnref 转换；
- scoped `git diff --check`：无 whitespace error，仅既有 LF/CRLF 提示；
- Windows x64 Catch2 执行树仍要求重建当前缺失的整套 native third-party ports，本轮没有把该外部
  依赖重建当成源码失败；测试 TU 已由两种 Emscripten 配置完整编译。

Web 与 Wasmtime 主 wasm 与 V244 逐字节相同，符合“生产代码仅修注释”的预期：

| product | size | FUNCTION | CODE | DATA | name | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Web `index.wasm` | 85,655,252 | `0x1BD31` | `0x1A41057` | `0x5A3E40` | `0x3185F7B` | `891B9C2C3604064B320308F97BD1FAC763A6E1EB34CEB1F3DC26E38ADFF9C110` |
| Wasmtime `index.wasm` | 85,002,393 | `0x1BA50` | `0x19E9005` | `0x5A1090` | `0x3141E11` | `3CE70D2FE4FBBA56A2E42B33A5FEFD2BD12581D64CE62F1D5BD11AB4659D3D26` |

`krkr2_wasmtime_guest.wasm` 为 151,478,490 bytes，SHA-256
`588E345568BB79298568D17B20E32E76EF630EC5BDA5BEFADFFCCA7460908299`。guest 对重新编译源文件的
行表/调试 metadata 敏感；本轮 main wasm 的 exact hash 与 CODE section 不变是生产语义无扰动的
直接守护。

## 10. 下一边界

V246 继续 ordinary Canvas item tail 外一层的 top-level receiver/target/clip owners：最后一个 item
完成后 `setClip()` reset 的参数构造、返回值忽略、receiver/raw owner 清理，以及 reset callback 抛异常
时已经销毁与仍存活的 outer function-scope owner 集合。重点区分 per-item cleanup、post-loop clip reset
和 wrapper post-draw 三个生命周期层级。
