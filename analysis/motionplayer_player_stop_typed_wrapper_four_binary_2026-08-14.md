# MotionPlayer `Player::stop` typed wrapper 四端恢复（2026-08-14）

## 结论

四份参考二进制的 `Motion.Player.stop` 都是普通 typed NCB method，而不是 raw callback。
源码级 native body 为：

```cpp
void Player::stop() {
    allplaying = false;
}
```

它不接收参数、不返回 Boolean，也不触发 motion teardown。当前端口原有 `stopCompat`
虽然同样清 playing，却把脚本结果写成 `true`，并绕过了 typed wrapper 对 result、argc 与
native-instance failure 的共同处理，因此脚本 ABI 不等价。本轮已删除 raw wrapper 并恢复
`NCB_METHOD(stop)`。

## 四端映射

| 阶段 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| Player member registration site | `0x6D5DDC` | `0x5986C0` | `0x1001250CC` | `0x124320` |
| typed zero-arg void registration helper | `0x6D6D1C` | `0x5B33D8` | `0x1001498F8` | `0x14A8AC` |
| generated typed `FuncCall` | `0x6F6E48` | `0x5B34B0` | `0x100149A48` | `0x14AA94` |
| native `Player::stop` | `0x6D6E10` | `0x599118` | `0x100125938` | `0x124B2E` |

registration helper 把 `{method pointer, member-adjustment}` 填进 zero-argument void method 的
专用 Function 对象。Android arm64 的 member pointer 为直接函数地址加零 adjustment；其他
目标的构造形态与之相同。它不是保存 `tjs_error (*)(result,argc,param,objthis)` 的 raw
callback 对象。

## native body 与字段

四端完整函数只有一条字节清零和 return：

| 目标 | playing 字段 |
| --- | ---: |
| Android arm64 | `Player+1099` |
| Android armv7 | `Player+751` |
| iOS arm64 | `Player+987` |
| iOS armv7 | `Player+687` |

这些偏移与 `playImpl`、chara live writer 和 `allplaying` getter 所用字段一致。body 不访问：

- primary/stealth motion label；
- motion content 与 find-motion context Variant；
- live/pending chara；
- frame cursor、loop time、sync flags；
- node/timeline/particle 容器；
- current dispatch。

因此 `stop` 是一个播放 gate 写入，不是 clear/unload/reset。重复调用天然幂等。
它只清本 Player 的 local byte；type-3 descendant 仍在播放时，递归属性 `allplaying` 仍可
返回 true。两个读属性的精确差异见
`motionplayer_player_playing_aggregate_four_binary_2026-08-14.md`。

## generated typed wrapper

四端 `FuncCall` 的共同顺序为：

```text
if membername != null: return TJS_E_MEMBERNOTFOUND
if objthis == null:    return TJS_E_NATIVECLASSCRASH
if result != null:     result.Clear()
if argc < 0:           return TJS_E_BADPARAMCOUNT
resolve Player native instance from objthis
if resolution failed:  return resolver error
if native is null:      return failure
call Player::stop()     // void; no result conversion/store
return TJS_S_OK
```

方法的最小参数数为零，所以任何非负 `argc` 都被接受；多余参数完全不读取。负 `argc` 是
通常不可由脚本产生、但 dispatch 级可观察的边界：result 已经清成 Void，而 native body
尚未调用。void method 正常返回时 result 同样保持 Void。

这与旧 raw wrapper 有三个可见差异：

1. 旧 wrapper 正常返回把 result 写成 Boolean true；原版 typed wrapper 清成 Void；
2. 旧 wrapper 自己忽略所有 `argc`，连负数也成功；原版负数返回 bad-param-count；
3. typed wrapper 使用统一 native resolver/error 路径，不返回旧 wrapper 手写的
   invalid-object 分支结果。

## 本地修正与回归

- `Player` 新增 `void stop()`，body 只写 `_allplaying=false`；
- 删除 `Player::stopCompat` 声明与实现；
- Player registrar 从 `NCB_METHOD_RAW_CALLBACK(stop,...)` 改为 `NCB_METHOD(stop)`；
- 源码注释不再保留旧单目标地址，只记录四端共同语义。

回归用 Player adaptor 直接调用脚本成员 `stop`，验证：

- `argc=1` 的 surplus 参数被忽略，playing 清零，旧 integer result 变成 Void；
- `argc=-1` 返回 `TJS_E_BADPARAMCOUNT`，result 已变成 Void，playing 保留 true，证明 native
  body 没有执行。

## IDB 改进

四份 recovery IDB 均已：

- 将 native body 命名为 `Player_stop_guess` 并设为 `void(void *self)`；
- 在 native body 标注唯一 playing-byte 写入及全部保留状态；
- 在 registration site 标注 typed void wrapper 的 result/argc/native-resolver 边界；
- 保存到各自 recovery IDB。

## 验证

- `Web Debug Build` 完整重编并成功链接 `index.html` / Wasm；
- 聚合 `motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten defines/includes/ABI 参数执行
  `-fsyntax-only` 成功；唯一诊断是仓库既有 `_tss` literal-operator 弃用 warning；
- `git diff --check` 通过；仅有工作树既有 LF/CRLF 提示。
