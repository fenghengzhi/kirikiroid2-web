# MotionPlayer controller 头文件 ABI 注释迁移（四参考，2026-08-16）

## 结论

本轮没有改变任何字段、类型、声明顺序或运行时代码，而是把 compiled source 中以
Android ARM64 为主的物理 offset/sizeof 注释迁回 `analysis/`。这一迁移直接处理了从旧
`libkrkr2.so` 反编译语境切换到当前四个 `reference/binaries/` 后的注释失真风险：单端
offset 可以帮助定位 IDB，却不能充当四端共同的 C++ 成员身份。

四端已经闭合的共同源结构是：

- Eye/Blink：主 12B keyframe deque、次 8B pair deque、mesh resolver、
  `state/value/target/dir/span/accum/inv/pow`、blink metadata/state tail；
- Eyebrow：相同的两条 track 与 resolver，随后是
  `state/value/target/dir/accum/span/pow/inv` 和 `beginFrame`；
- VarController：20B keyframe deque、`count/state`、三个拥有所有权的 float 数组、
  `powCount/phase/invDuration`；
- Engine：十个 typed deque 按源码顺序声明，Eye/Eyebrow/Mouth entry 都以单指针 owner
  开头，后随一项或两项 `ttstr` key；逆序成员析构先释放 key，再释放 controller。

Eye 与 Eyebrow 的 curve 字段顺序差异仍显式保留。它是四端共同的源结构差异，不是
Android ARM64 偶然出现的 `+0x138/+0x13C` 数字差异。两类 controller 也仍是独立、无
vptr、无继承关系的 owning object。

## 物理布局归档位置

完整四端表没有被删除，仍由下列专题保存：

- Eye/Blink 与 Eyebrow 的四端 object size、两条 deque、resolver、curve 和 tail offset：
  `motionplayer_eye_eyebrow_constructor_initialization_four_binary_2026-08-15.md`；
- Eye/Eyebrow entry owner、builder raw-emplace、异常边界与析构：
  `motionplayer_eye_entry_owner_emplace_four_binary_2026-08-13.md`、
  `motionplayer_eye_eyebrow_enqueue_lifecycle_four_binary_2026-08-11.md`；
- VarController 的 `0x80/0x48/0x60/0x38` 四端 object size 与所有成员 offset：
  `motionplayer_var_controller_lifecycle_four_binary_2026-08-11.md`；
- Engine 的 `1496/788/1064/568` 四端 object size、十个 deque 和后续容器区域：
  `motionplayer_emote_engine_ctor_full_four_binary_2026-08-14.md`。

因此 compiled source 现在只描述可移植的字段角色、顺序、所有权、读写门控和生命周期；
需要回到某一参考指令或 native layout 时，由上述四参考分析表和 recovery IDB 承担定位。

## 有意保留的尺寸契约

`EmoteVarKeyValue20B` 仍保留 `sizeof == 20` 的 `static_assert`，并保留五个连续 float word
的 member 内偏移语义。这不是某个 STL ABI 的 container offset：四端都以同一 20B stride
读写 keyframe，且第四个 channel 与 duration 复用 word 3。类似地，Engine entry 上已有的
pointer-width 公式 `static_assert` 属于源类型形状检查，并非把 Android A64 物理地址硬编码
进 Web 对象。

本轮不需要修改 recovery IDB：所依据的 constructor、builder、step、reset、range
destructor 和 Engine constructor 证据已经在上述专题中完成四端命名、注释和保存；本轮仅
迁移这些已闭合证据在 compiled source 与 `analysis/` 之间的表达层级。
