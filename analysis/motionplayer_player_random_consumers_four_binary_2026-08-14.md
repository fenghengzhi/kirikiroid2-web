# MotionPlayer Player random 消费链四参考二进制恢复（2026-08-14）

## 范围与调用图

本纵切面从四端 `Player_random_guess` 的全部直接 xref 出发，复核随机数的消费者、条件
取样和粒子生成阶段顺序。旧 `libkrkr2.so` 注释不作为裁决依据。

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| 条件区间短副本（调用 random） | `0x6B7B60` | `0x5850C8` | 被裁剪 | 被裁剪 |
| 条件区间内联-random 副本 | `0x6B3240` | `0x582400` | 被裁剪 | 被裁剪 |
| `Player_random_guess` | `0x6B7B98` | `0x585100` | `0x10010DE8C` | `0x10B774` |
| type-4 粒子系统 | `0x6BC4BC` | `0x588A48` | `0x100111D08` | `0x10F51C` |

所有实际引用都收敛到 type-4 粒子系统：四端各有 16 个粒子函数内部调用点。Android
两端还各有一条来自紧邻条件区间短函数的调用指令，但该函数本身全库零 xref，不能算运行
时消费者。2026-08-16 fresh old-address 复核又闭合了两个早先漏记的第二副本：它们同样零
xref，但把整条 Player random wrapper 内联，因而不产生对 out-of-line random 的 call
xref。除此之外没有插值器、wind、timeline 或渲染消费者调用 `Player::random`。

## 16 个静态取样点

四端共同的源级分组如下：

1. frequency 模式首次激活的发射间隔；
2. frequency 模式 timer 循环内每次补充的发射间隔；
3. count 模式发射数；
4. particle motion list 元素选择；
5. box 的 X；
6. box 的 Y；
7. box 3D 分支的 Z；
8–10. sphere 3D 分支的两个角与体积半径；
11–12. disk 2D 分支的角与面积半径；
13. 速度区间；
14. 方向散布；
15. 初始粒子角度；
16. 初始缩放。

这些是静态调用点而不是单粒子的固定调用次数。box/sphere/disk 互斥，3D 条件控制额外
维度，多个区间是否退化也会改变共享 ResourceManager RNG 的运行时消费量。

## 条件消费与 IEEE-754 边界

frequency、速度、角度和缩放等区间插值都先用原生 double `!=` 比较端点，只在不等时
调用 `Player_random_guess`。因此：

- 相等的普通数和同号无穷不消费 RNG；
- `-0.0 == +0.0`，不消费 RNG，并保留 minimum 的 `-0.0`；
- NaN 与自身仍为不等，会消费一次 RNG，算术结果仍为 NaN。

count trigger 是明确的例外：只要 node flags 整字节非零，就先无条件取样，再计算
`int(min + (max-min)*r)`。即使 `min == max == 0`，也会消费一个随机数；flags 为零时则
完全不取样，emitCount 保持零。

2026-08-16 进一步把该 `int(...)` 的指令边界锁定为四端共同的 signed-int32
向零/饱和 profile：NaN -> 0，正负超界分别到 `INT32_MAX/INT32_MIN`。详见
`motionplayer_particle_count_trigger_conversion_four_binary_2026-08-16.md`。

同日复核第 4 个消费点证明，motion-list source index 在
`random() * signed sourceCount` 后也使用同一 `FCVTZS` / `VCVT.S32.F64` profile；
它不夹取到 list 范围。NaN 得零、正负溢出得到 `INT32_MAX/INT32_MIN`，完整证据见
`motionplayer_particle_source_index_conversion_four_binary_2026-08-16.md`。

角度散布使用 `range != -range`：`range` 为正零或负零时不取样，其他普通非零值及 NaN
进入取样路径。

## 单次生成的消费顺序

成功进入粒子创建时，四端均保持以下阶段顺序：

```text
motion list 选择
  -> 位置体积分布（box / sphere / disk）
  -> speed 区间
  -> angle spread
  -> particle angle 区间
  -> zoom 区间
```

其中 sphere 3D 连续三次调用的含义为：第一次形成 theta，第二次形成 phi，第三次形成
`cbrt(r) * 16` 半径。disk 2D 先取角，再取 `sqrt(r) * 16` 半径。box 先取 X、再取 Y，
仅 3D 时再取 Z。不同编译器对局部变量和 sin/cos helper 的安排不同，但四端的调用先后
和各返回值用途一致。

## Android 保留的区间成员候选

Android 两端在零参数 wrapper 前均保留同形 56-byte 短函数，ABI 为：

```cpp
double candidate(Player *self, double minimum, double maximum) {
    double value = minimum;
    if(minimum != maximum)
        value = minimum + (maximum - minimum) * self->random();
    return value;
}
```

它没有任何 caller，也不在 92 项 Motion.Player NCB 表中。两份 Android 还各有一份第二
body（A64 `0x1BC` bytes、A32 `0xAC` bytes），区间算术相同但把完整 Player random
owner/dispatch/AsReal 流程内联；第二 body 也没有 caller。iOS 两端没有任一 range body，
符合无引用成员被链接器裁剪的形状。由于四份 stripped 二进制没有给出源符号，源级恢复名
保守定为 `Player::randomInRange_guess`，第二 IDB 实体只按形状命名为
`Player_randomInRange_inlinedRandomCopy_guess`；不能断言原始拼写，也不把任何一份注册为
脚本方法。粒子主函数在四端均直接保留自己的条件插值，故恢复这个成员不应反向改写粒子
函数调用结构。

## 源码、测试与 IDB

- `Player::randomInRange_guess` 按 Android 保留的源结构放在零参 wrapper 之前；
- 新增 test-only type-4 pass 入口，不改变生产调用链；
- 回归探针验证 equal finite、equal infinity、`-0/+0` 不消费，NaN 消费；
- 第二个顺序探针验证 frequency equal interval 不消费，而 count flags 非零、zero interval
  仍消费一次；flags 清零又抑制消费；
- 完整 Web Debug 编译/链接成功，聚合 motionplayer 单元测试翻译单元用真实
  Emscripten 参数通过 `-fsyntax-only`，唯一诊断为仓库既有 `_tss` warning；
- Android 两份 IDB 已命名短 `Player_randomInRange_guess` 与内联-random 副本；四份粒子主函数补充
  16 点拓扑、条件取样和阶段顺序注释；四份 recovery IDB 均已保存。

2026-08-16 V165 还修复了 Android arm64 recovery IDB 中 `Player_random_guess` 只有首条
指令被定义、Hex-Rays 只显示 `JUMPOUT` 的旧库缺口，并证明 Player 与 ResourceManager
两层复用同一 process-wide `random` member-hint word。完整证据见
`motionplayer_shared_random_hint_owner_lifecycle_four_binary_2026-08-16.md`。

## 未外推的部分

- 本纵切面证明调用次数和消费顺序，不假定 Math.RandomGenerator 的具体 PRNG 算法；
- 未用某个 fixture 的随机数值硬编码序列，因为构造时种子和脚本对 generator 的替换都
  属于 ResourceManager/脚本环境状态；
- exact native name 不可恢复的 Android 区间函数继续使用 `_guess`，不以邻接位置伪造
  确定名字。
