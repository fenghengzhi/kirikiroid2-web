# EmoteEngine 顶层 state pipeline 四端复原（2026-08-15）

## 结论

本纵切面重新审计四个 `reference/binaries/` 的
`EmoteEngine::serializeState` / `unserializeState`，闭合 controller 内部 state 与脚本
可见顶层 Dictionary 之间的数据流。

顶层 schema 固定为：

```text
timeline -> eye -> eyebrow -> mouth -> transition -> selector -> base -> outerforce
```

serialize 与 unserialize 对每个 key 复用同一个进程级 mutable member-hint 槽；`mouth`
进一步与 Mouth controller state 路径复用。当前源码此前只给 `mouth` 传 hint，其余七项
错误地传了 null，本次已恢复。

两个方向都不是事务：serialize 在创建输出 Dictionary 之前就执行零时间步进并写回变量
表；unserialize 每取出一个子树就立即 restore。任一步骤抛异常时，已发生的控制器状态、
变量表或前序子树变更均不回滚。

Primary 脚本入口也已重新闭合：四端 registrar 直接把这两个 Engine core 分别注册为
`serialize` 与 `unserialize`，成员指针 adjustment 为零，没有平行的 EmotePlayer
forwarder。无参 Variant 返回包装器的 result-clear、surplus 与双临时 owner 细节见
`analysis/motionplayer_state_method_typed_binding_owner_four_binary_2026-08-15.md`。

## 四端入口映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_serializeState_guess` | `0x673220` | `0x55BB70` | `0x1001AF774` | `0x1AEF30` |
| `EmoteEngine_unserializeState_guess` | `0x675424` | `0x55CF3C` | `0x1001B1130` | `0x1B0B80` |
| flags-forwarding named Variant getter | inline | `0x55218C` | `0x1000F1860` | `0xEDBF0` |

先前从某个 key 的 xref 得到的 `0x6755A4`、`0x55CFDC`、`0x1001B1218`、
`0x1B0CA0` 都只是 unserialize 函数内部地址，不是入口；本表用 `lookup_funcs` 重新解析
到真实函数起点，避免把旧注释中的中间地址继续传播。

## 顶层 key 的原始编码

普通 IDA string search 在 Android 上漏掉多数宽 key，Hex-Rays 又把多项显示成
`"t"/"e"/"s"/"b"/"o"`。按 UTF-8、UTF-16LE、UTF-32LE 三种编码做 raw byte
搜索后，state 使用的八项均唯一落在 UTF-16LE 地址；UTF-32LE 全部无命中。

| key | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `timeline` | `0x14D3B02` | `0xD84534` | `0x10195FF4C` | `0x17522B0` |
| `eye` | `0x14D3B14` | `0xD84546` | `0x10195FF5E` | `0x17522C2` |
| `eyebrow` | `0x14D3B1C` | `0xD8454E` | `0x10195FF66` | `0x17522CA` |
| `mouth` | `0x14D387A` | `0xD8435A` | `0x10195FC58` | `0x1751FBC` |
| `transition` | `0x14D3B2C` | `0xD8455E` | `0x10195FF76` | `0x17522DA` |
| `selector` | `0x14D3B42` | `0xD84574` | `0x10195FF8C` | `0x17522F0` |
| `base` | `0x14D3B54` | `0xD84586` | `0x10195FF9E` | `0x1752302` |
| `outerforce` | `0x14D3B5E` | `0xD84590` | `0x10195FFA8` | `0x175230C` |

## member-hint 地址身份

| key | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `timeline` | `0x1AB4FA4` | `0x111153C` | `0x101B6A054` | `0x187DA74` |
| `eye` | `0x1AB4FA8` | `0x1111540` | `0x101B6A058` | `0x187DA78` |
| `eyebrow` | `0x1AB4FAC` | `0x1111544` | `0x101B6A05C` | `0x187DA7C` |
| `mouth` | `0x1AB4EEC` | `0x1111484` | `0x101B69F9C` | `0x187D9BC` |
| `transition` | `0x1AB4FB0` | `0x1111548` | `0x101B6A060` | `0x187DA80` |
| `selector` | `0x1AB4FB4` | `0x111154C` | `0x101B6A064` | `0x187DA84` |
| `base` | `0x1AB4FB8` | `0x1111550` | `0x101B6A068` | `0x187DA88` |
| `outerforce` | `0x1AB4FBC` | `0x1111554` | `0x101B6A06C` | `0x187DA8C` |

四端 xref 集合一致：除 `mouth` 外，每个槽只由顶层 serialize 和 unserialize 使用；
`mouth` 还由 Mouth controller serializer/restorer 使用。因此源码把七个顶层专用槽留在
`EmoteEngine.cpp` 匿名命名空间，将 `mouth` 继续指向
`motion::detail::controllerMouthHint_guess`，没有创造第二个同名 cache。

## serialize：零时间预刷新与发布顺序

四端共同流程为：

```text
preProgress(true, 0.0)
for each Eye:       step(dt=0); variableValues[label] = output
for each Eyebrow:   step(dt=0); variableValues[label] = output
for each Mouth:     step(dt=0); variableValues[label] = mouth
                                variableValues[talkLabel] = talk
for each Selector:  step(dt=0); variableValues[label] = output
for each Transition:step(dt=0); variableValues[label] = output
stepRootControllers(dt=0)

dictionary = fresh TJS Dictionary
for child in fixed schema order:
    childValue = serializeChild()
    dictionary.PropSet(MEMBERENSURE, childKey, sharedHint, childValue)
return owning Object closure
```

这说明 snapshot 本身具有可观察副作用：它会推进/刷新 controller 输出，并通过共享
`LabelValueMap::operator[]` 路径写 `_variableValues`。如果后面的 child serializer、
Dictionary `PropSet` 或分配抛异常，这些刷新不会回滚。输出 Dictionary 的已发布前缀由
临时 Variant/dispatch 的异常析构回收，不会泄漏，但不会撤销 Engine 的预刷新。

八个子 serializer 都在前一个 child `PropSet` 完成并销毁临时 Variant 后才调用；没有
先构建八个子对象再一次性提交的 staging 容器。

## unserialize：Object closure 与逐子树提交

四端共同对象生命周期是：

```text
copy input Variant closure
force/require Object conversion
AsObject/AddRef outer dispatch
destroy copied Variant before first child getter

for child in fixed schema order:
    temporary = Void
    outer.PropGet(flags=0, childKey, sharedHint, &temporary, outer)
    ignore HRESULT
    childValue = copy(temporary)
    destroy temporary
    restoreChild(childValue)
    destroy childValue before next key

Release outer dispatch on normal or exceptional exit
```

非 Object 输入在转换处抛错；类型为 Object 但 dispatch 为空也没有友好 guard。getter 的
HRESULT 被忽略，因此普通缺失属性以 Void 继续进入对应 restore。A32/iOS 的共享 getter
helper 明确先将临时 Variant 初始化为 Void、转发 flags/name/hint、忽略 HRESULT、复制到
返回槽，再销毁临时；A64 把同样流程内联进 unserialize。

这带来不同的缺失行为：

- `timeline`：`restoreTimelineState_guess` 在检查 native Array 之前先执行
  `stopTimeline_guess("")`，所以缺失 `timeline` 仍会停止 timeline，然后因 Void 非
  Array 返回；
- `eye/eyebrow/mouth/transition/selector`：Void 无法取得 native Array，保持对应
  controller 集合当前状态；
- `base/outerforce`：外层 Object guard 对 Void 返回，不写相应 direct controller；
- 若 `base` 本身存在但内部 `rotate` 缺失，Angle restore 的严格 Object 转换仍会抛错，
  详见 Angle 生命周期分析页。

任一 getter、child restore、Object/数值转换或容器操作抛异常时，后续 key 不再处理；
前面已 restore 的子树保持新状态。外层 dispatch 仍由 unwind cleanup Release。

## 本地修正、IDB 回写与验证

- `EmoteEngine.cpp` 新增七个顶层 `_guess` hint 槽；八个 serialize `PropSet` 与八个
  unserialize `PropGet` 现在都接到四端地址身份，`mouth` 继续复用 controller 槽。
- 四份 recovery IDB 将七个先前被截断的顶层 UTF-16LE 字面量恢复为完整语义名；iOS
  两端的七个独立 hint 数据项也恢复为语义名，Android 端数组内部槽以注释记录。
- 四端 serialize/unserialize、八个 literal、八个 hint 与三端非内联 getter 均补证据
  注释；入口/helper 增加 bookmark，四份 IDB 已原位保存。
- 使用真实 response file 的 `motionplayer-dll.cpp -fsyntax-only` 通过，仅有既有 `_tss`
  warning。
- `cmake --build --preset "Web Debug Build"` 完成 3 个增量步骤，重新编译
  `EmoteEngine.cpp`、生成 `libmotionplayer.a` 并成功链接最终 `index.html`；仅有既有
  Emscripten/JSPI/`_tss` warnings。
- 定向 `git diff --check` 通过；换行转换提示不是 whitespace error。

本页闭合的是顶层 state orchestration，不代表所有 child serializer/restorer 的内部容器
都已完整恢复；controller 内部细节见
`analysis/motionplayer_controller_state_restore_family_four_binary_2026-08-15.md`，timeline
子树的五字段、Array/accessor 生命周期与异常前缀见
`analysis/motionplayer_timeline_state_snapshot_restore_four_binary_2026-08-15.md`。
