# MotionPlayer 未展开 `STUB_WARN` 占位宏清理（2026-08-16）

## 结论

删除三处文件中的四个 `STUB_WARN` definition 和一个只为该宏服务的 `#undef`：

- `EmoteEngine.cpp`；
- `EmotePlayer.cpp` 的 D3DEmotePlayer/EmotePlayer 两套 spelling；
- `PlayerInternal.h`。

全 `cpp/plugins/motionplayer` 与 motionplayer tests 的 token 检索证明它们没有一次展开，
所以这次清理不改变预处理后的 C++、符号表、控制流、日志或异常边界。`EmoteEngine.cpp`
中只为这组死宏存在的 `spdlog` include/`LOGGER` definition 也一并删除；
`EmotePlayer.cpp` 的死 `LOGGER` definition 同样删除。

`PlayerInternal.h` 的 `LOGGER` definition 不能随宏一起删除：若干包含该 header 的 Player
translation unit 仍使用它执行当前 Web 诊断。这里只删除零展开的 `STUB_WARN`。

## 原版 TODO 边界不受影响

这次清理没有把真正的 TODO 行为误删。四个参考共同存在、当前仍按原版抛出 `eTJSError`
的 D3DEmotePlayer TODO 接口保持原状，例如：

- `assignState`；
- 五个 variable/frame query；
- `getOuterForce`。

它们是可达函数体和注册表成员；`STUB_WARN` 则只是早期占位实现遗留、从未展开的
preprocessor 文本。两者边界不能混同。
