# MotionPlayer 孤立 logo-trace session reset 四端复核（2026-08-16）

## 1. 结论

删除 `motion::detail::resetLogoChainTraceSession(const std::string &)` 的 declaration 和
definition。它不是参考插件接口或内部调用链的一部分，也没有成为本地 Web 诊断 sidecar 的
真实入口：全仓库除声明与定义外零引用，而 Web/Wasmtime 两份 `libmotionplayer.a` 都仍为它
发出独立强 `T` 符号。

保留且不改动实际参与诊断数据流的：

- `logoChainTraceLog`；
- `logoChainTraceCheck`；
- `logoChainTraceSummary`；
- `ensureLogoTraceSessionLocked`。

后者已经在首次按 path 取得 session 时初始化 `motionPath`/`motionName`，path 改变时也会
重置。因此删除零调用 reset 不改变已启用诊断的实际行为，更不影响默认 production 路径。

## 2. 本地引用与符号证据

删除前对 `cpp/`、`tests/` 的精确 token 检索只有：

```text
cpp/plugins/motionplayer/RuntimeSupport.h: declaration
cpp/plugins/motionplayer/RuntimeSupport.cpp: definition
```

没有 registrar、测试 hook、运行调用或函数指针取得。删除前 `llvm-nm -C` 在两份 archive
都报告：

```text
T motion::detail::resetLogoChainTraceSession(std::string const&)
```

这不是“死代码已被链接器自然消除”的情形；它真实扩大了恢复实现的 C++ 符号面。

## 3. 四参考新 motion 边界

fresh 复核四份 `Player_playImpl_guess` 完整反编译：

| 目标 | 地址 |
|---|---:|
| Android arm64 | `0x6AF664` |
| Android armv7 | `0x580158` |
| iOS arm64 | `0x100107540` |
| iOS armv7 | `0x104AE8` |

四端共同调用/状态顺序是：

1. play flag 与当前 label gate；
2. 仅当 `flags & 8` 时调用真正的 `Player_resetMotionState_guess`；
3. 为 chara/motion label 建立临时 `ttstr` owner，调用 `Player_loadMotion_guess`；
4. 成功时提交 stealth/primary label、取得 load-result 两个 Variant、读取 motion type，进入
   对应 initializer；
5. 失败时只执行 production `motion not found` 日志、清除 content/context 和 playing；
6. 最后析构 load-result owner。

完整 call set 没有第二个 reset/session helper，也没有 logo/trace/path-keyed map/sequence/summary
数据流。原版的 `Player_resetMotionState_guess` 是实际播放状态语义，不能据其名称推导出另一个
诊断 session reset。

早先的三编码四端检索已经证明整个 logo-chain/snapshot 查询设施是 Web-only sidecar，见
`analysis/motionplayer_logo_trace_query_native_absence_four_binary_2026-08-15.md`。本轮的新证据
进一步限定：即便保留 sidecar，也不应为一个从未接入的 reset 函数保留公开 C++ 符号。

## 4. 生命周期与边界行为

session 的实际生命周期由 `logoTraceSessions()` 的 function-static map 与
`ensureLogoTraceSessionLocked(path)` 决定：

- 第一次 log/check/summary 使用一个 path 时，`operator[]` 建立 value；
- stored path 不同则就地清空并重新设置 path/name；
- name 为空时补 basename；
- `summaryEmitted` 只在同一 map entry 的实际 summary 调用中改变。

孤立 reset 从未参与上述任何一步。删除它不会新增、延后或省略 map mutation；只让源码和
archive 符号面忠实反映真实可达的数据流。

## 5. IDB 记录

四份 recovery IDB 的 `Player_playImpl_guess` 入口补充本轮注释：生产 reset 仅由
`flags & 8` 驱动，完整 play/load call set 不存在 logo-trace session reset。绝对地址只保留
在本分析记录和 IDB 中，不写入编译源码注释。

## 6. 删除后验证

- ordinary motionplayer DLL syntax check：通过；
- `KRKR2_WASMTIME_HEADLESS=1` syntax check：通过；
- Web Debug `motionplayer` archive：32/32 通过；
- Wasmtime Headless Debug `motionplayer` archive：32/32 通过；
- Web Debug 最终 `index.html`/Wasm link：3/3 通过；
- `rg` 对 `cpp/`、`tests/` 的 reset token：0；
- Web/Wasmtime 两份 archive 的 reset symbol：均为 0；
- 两份 archive 的 `logoChainTraceLog`、`logoChainTraceCheck`、
  `logoChainTraceSummary` 强定义：各自仍分别为 1；
- `git diff --check`：通过（仅 Git 的现有 LF/CRLF 提示）。

四份 IDB 均在 force-recompile 后从 decompile 输出读回新注释并保存。
