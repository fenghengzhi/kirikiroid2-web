# Player `independentLayerInherit` 四参考二进制边界

日期：2026-08-13

## 结论

`independentLayerInherit` 的 NCB 形式是读写 Boolean 属性，但公开 setter
具有反直觉且四端一致的非闭环行为：

1. 将 typed Boolean 与 Player 保留字节比较；
2. 相等时立即返回；
3. 不相等时遍历完整 native `std::deque<MotionNode>`，把每个 node 的 delta
   dirty 字节置 `1`；
4. 返回前不写 Player 的 `independentLayerInherit` 字节。

因此公开 `set(true)` 后，`get()` 可以继续返回旧的 `false`；再次
`set(true)` 仍会再次标脏全部节点。这不是 port 漏写：四个当前参考二进制
都具有相同控制流。

真正的非构造写入发生在 type-3 child 初始化：读取
`motionIndependentLayerInherit`，若与新 child 的默认/现值不同，则只标脏
child root，然后写入 child Player 字节。构造函数本身把该字节清零。

## 注册、accessor 与字段布局

| 目标 | 注册 | getter | public setter | 字段 | ctor 清零 |
|---|---|---|---|---:|---|
| Android arm64 | `0x6D4B98` | `0x6D6B48` | `0x6C9DB4` | `+1097` | `0x6CC488` |
| Android armv7 | `0x598218` | `0x598FC2` | `0x5921E8` | `+749` | `0x593830` |
| iOS arm64 | `0x1001249CC` | `0x100125680` | `0x10011CB18` | `+985` | `0x10011EE88` |
| iOS armv7 | `0x123CC4` | `0x1248A6` | `0x11B498` | `+685` | `0x11D894` |

getter 均为单 byte load。Android arm64 setter 在比较前显式 `& 1`；其余
三端接收到 NCB typed adapter 已转换的 Boolean 值。

## public setter 的 deque 遍历

四端节点尺寸和 dirty 偏移随 ABI 变化，但遍历拓扑一致：

| 目标 | `sizeof(MotionNode)` | node delta dirty | setter 标脏点 |
|---|---:|---:|---|
| Android arm64 | `2632` (`0xA48`) | `+1584` (`0x630`) | `0x6C9DFC` |
| Android armv7 | `2272` (`0x8E0`) | `+1344` (`0x540`) | `0x592206` |
| iOS arm64 | `2648` (`0xA58`) | `+1600` (`0x640`) | `0x10011CB90` |
| iOS armv7 | `2228` (`0x8B4`) | `+1312` (`0x520`) | `0x11B4FA` |

arm64 setter 的外壳在值不等时 tail-branch 到 deque 遍历 helper；32 位版
编译器分别选择了内联或相邻 helper。空 deque 会自然完成，不会写 Player
字节。

## type-3 内部提交路径

| 目标 | 比较/读取区域 | child root dirty | child Player store |
|---|---|---|---|
| Android arm64 | `0x6B19D8..0x6B19E4` | `0x6B19F0` | `0x6B19F4` |
| Android armv7 | `0x5812F4..0x5812FA` | `0x581302` | `0x581306` |
| iOS arm64 | `0x100108BC4..0x100108BCC` | `0x100108BF4` | `0x100108BF8` |
| iOS armv7 | `0x106384..0x10638A` | `0x1063B4` | `0x1063BA` |

缺失 `motionIndependentLayerInherit` 属性时使用 `false`。这个内部路径不能
改成调用 public setter，否则 child 字节永远无法提交，后续
`updateLayers`/anchor consumer 将观察到错误状态。

## consumers

字段完整访问扫描在四端均只得到八类访问：type-3 compare/store、
`updateLayers` 的多处分支读取、anchor-feedback 读取、public setter 比较、
ctor clear、NCB getter。代表性 consumer：

- Android arm64：`0x6B8BE8`, `0x6B8CF0`, `0x6B8D94`, `0x6BDD20`；
- Android armv7：`0x585B88`, `0x585D50`, `0x589E60`；
- iOS arm64：`0x10010EAC4`, `0x10010ECA8`, `0x100113148`；
- iOS armv7：`0x10C564`, `0x10C73E`, `0x110C1A`。

false 时部分 transform/opacity/anchor 路径继续继承 root；true 时选择
independent-layer 分支。

## 本轮落地

- 保留公开 setter 的“标脏但不提交”行为；
- 清理 `Player.h`、`PlayerCore.cpp`、`main.cpp` 中旧 `libkrkr2.so` 绝对地址
  与过时 getter 名；
- 增加相等 no-op、不等全标脏、重复标脏、type-3 内部提交和提交后公开
  setter 仍不改值的回归测试；
- 四个 IDB 共命名 8 个 getter/setter，标注注册、ctor、type-3 writer 与
  consumer，并保存数据库。

## 验证

- `cmake --build out/web/debug -- -j 6`：全量重编与链接通过；随后复跑为
  `ninja: no work to do.`；
- `cmake --build out/wasmtime/debug -- -j 6`：全量重编与链接通过；随后复跑为
  `ninja: no work to do.`；
- 以 Web Debug 的实际编译参数对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 Emscripten
  `-fsyntax-only`：通过，仅有仓库既有的 `_tss` deprecated warning；
- `git diff --check`：通过，仅报告工作树既有的 LF/CRLF 转换 warning。
