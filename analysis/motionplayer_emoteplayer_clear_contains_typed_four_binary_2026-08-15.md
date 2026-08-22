# Motion.EmotePlayer clear/contains typed NCB 成员四参考二进制复核

## 结论

对 `EmotePlayer_ncb_registerMembers_guess` 的 member 8/10 槽和实际成员体做四端 fresh
decompile/xref 后，旧端口和旧文档把二者标成 raw callback 的结论被推翻：

- `clear` 是 typed `void EmotePlayer::clear(Variant, Variant)`；
- `contains` 是 typed `bool EmotePlayer::contains(ttstr, double, double)`；
- registrar 保存的是 C++ member pointer，并为两种参数/返回签名选择 typed
  `ncbNativeClassMethod` 创建器；函数体不接收 argc/argv/result/objthis。

因此本地已删除 `clearCompat`/`containsCompat`，注册恢复为 `NCB_METHOD(clear)` 和
`NCB_METHOD(contains)`。这恢复了 typed adapter 的 receiver、arity、native unwrap、
参数 owner 和返回值发布顺序。

## 四端映射

| 目标 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| registrar | `0x67CEA8` | `0x5612E8` | `0x1001B5130` | `0x1B4DE0` |
| `EmotePlayer_clear_guess` | `0x67EE44` / `0xA8` | `0x561DA8` / `0x50` | `0x1001B5D04` / `0x5C` | `0x1B595C` / `0x96` |
| `EmotePlayer_containsByRawLabel_guess` | `0x67EEEC` / `0x48` | `0x497BFE` / `0x28` | `0x1001B5E84` / `0x48` | `0x1B5B74` / `0x36` |

Android arm64 registrar 在 `0x67D1B0/0x67D1C0` 形成 `clear` member pointer，在
`0x67D290/0x67D2A0` 形成 `contains` member pointer；二者都构造 0x40-byte typed
Function method object。iOS arm64 对应序列把 `clear` 地址交给 `0x1001C719C`
creator，把 `contains` 地址交给 `0x1001C7A10` creator；紧邻的 typed `play`、
`getVariable`、`serialize` 也各按自己的 signature 使用不同 creator。raw callback
member 14–19 则进入另一组 registration helper，结构不同。

两份 32 位 registrar 使用 Thumb/PIC 间接形成相同 member pointer。虽然 IDA 没有把
所有 PC-relative materialization 恢复成入口 data xref，四端实际成员体的 C++ 参数
形状、相邻注册顺序和 typed wrapper 语义一致。

## `clear` 数据流与 Variant 生命周期

四份成员体共同执行：

```text
target = owning copy(method argument 0)
fill   = owning copy(method argument 1)
Player_drawToLayerRecursive(engine.player, target, fill)
destroy fill
destroy target
return void
```

成员体没有 argc 分支、argv null guard、motion-content test 或 result 写入。它无条件调用
Player；真正的 `hasMotionContent()` gate 位于 `Player::drawToLayerRecursive_guess` 开头。因此即使
无 motion，typed adapter 和 EmotePlayer member 仍已完成两个 Variant 参数的转换/owner
建立，随后 Player 才成功 no-op。

脚本名 `clear` 不表示清理 Engine/Player 生命周期。命中有 motion 的路径后，Player
仍执行既有的 D3DAdaptor clear、SeparateLayerAdaptor 解包、Layer/callable fill 和
type-3 child 递归。完整 Player body 见
`analysis/motionplayer_draw_to_layer_four_binary_2026-08-11.md`。

旧 `clearCompat` 的偏差包括：

- 手工查 `objthis` 并返回端口私有 `TJS_E_INVALIDOBJECT`；
- 零参/一参也可能成功，并把缺失 fill 合成为 Void；
- 在 EmotePlayer 层提前测试 motion，跳过 typed 参数 owner 边界；
- 手工清 result，而不是由 typed void-return convertor发布。

## `contains` 数据流与短路

四份成员体共同执行：

```cpp
node = Player_findNodeByRawLabel(player, label, true);
if(!node) {
    return false;
}
return GeometryShape_contains(node.currentGeometry, x, y);
```

标签使用 raw、递归 lookup；不存在时短路返回 false，不读取 geometry。命中后直接测试
节点内嵌的当前 GeometryShape，没有 visibility、空标签、更新或 owner fallback gate。
这个同一 helper 也被 D3DEmotePlayer façade调用；部分平台内联，部分平台保留 code
xref。

旧 `containsCompat` 手工检查 result、argv slot 和 native receiver，并把短 argc/null
slot 都变成 `TJS_E_INVALIDPARAM`。参考 typed adapter 不包含这些 compat 分支。

## Typed adapter 边界

本项目 NCBind 的 typed `ncbNativeClassMethod::FuncCall` 与四端产物一致：

1. `membername != nullptr` 委托基类；
2. `objthis == nullptr` 立即 `TJS_E_NATIVECLASSCRASH`；
3. `doInvoke` 检查 `numparams < required`，不足返回 `TJS_E_BADPARAMCOUNT`；
4. 再解 native instance，错误类型返回 `TJS_E_NATIVECLASSCRASH`；
5. 按声明顺序转换恰好 required 个参数；surplus 不读取；
6. 调 C++ member，并由 return convertor发布 Void 或 Boolean。

所以 `clear.required == 2`，`contains.required == 3`。null receiver 比 argc 更早；非 null
wrong receiver 则先经过 argc gate，再进行 native unwrap。这与 member 14–19 的
native-instance raw callback family 不同。

## 源码、测试与 IDB 改动

- `EmotePlayer.h/.cpp`：恢复 typed `clear(Variant, Variant)`，保留既有 typed
  `contains(ttstr,double,double)`；删除两个 compat callback。
- `main.cpp`：member 8/10 从 `NCB_METHOD_RAW_CALLBACK` 改回 `NCB_METHOD`。
- 2026-08-16 fresh Player registrar/worker 复核确认下游递归 body 本身是 Player
  `clear` 的 direct typed target；本地不再把它命名为 `drawToLayerCompat`，而使用
  未知源码名的 `drawToLayerRecursive_guess`。
- 单元回归覆盖 clear/contains 的 required arity、wrong receiver 顺序、surplus 忽略、
  typed void/Boolean result，以及无 motion clear 的 Player-inner successful no-op。
- 四个 recovery IDB 把 member 8 统一命名为 `EmotePlayer_clear_guess`，member 8/10
  应用 typed prototype、强制刷新反编译，并写入函数注释和 bookmark。

## 验证

- 完整 motionplayer Catch2 翻译单元 Emscripten syntax-only：通过；只有项目既有
  `_tss` literal-operator 弃用告警。
- `cmake --build --preset "Web Debug Build"`：10-step 增量编译与 `index.html` 完整
  链接通过；只有项目既有编译器/Emscripten 告警。
- 本纵切面文件的 `git diff --check`：通过；只有工作区既有 LF/CRLF 提醒。
- 四个 recovery IDB 均已在 rename/type/comment/bookmark 后原位保存。
