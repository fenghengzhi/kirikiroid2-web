# EmoteEngine deque #1 simple-spring entry owner、构造与接管边界（四参考二进制，2026-08-13）

## 1. 范围与联合结论

本文闭合 `EmoteEngine` 最早声明的 deque #1。虽然本地历史类型名是
`EmoteHairPartsNode48B`，真正填充该 deque 的 metadata key 是 `bustControl`；
`hairControl` 和 `partsControl` 分别填充后面的两组 chain-spring deque。deque #1
的节点由 `stepHairParts` 消费，保存一个 72 字节 simple-spring owner、一个 init byte、
三个 `ttstr` 和两个 anchor float。

四端联合恢复出的源码形态是：

```cpp
struct Entry {
    std::unique_ptr<Spring> spring;
    unsigned char initFlag;
    ttstr shapeLabel;
    ttstr keyX;
    ttstr keyY;
    float anchorX;
    float anchorY;
};

Spring *spring = new Spring(elem);
read op / p / pv / ofs into *spring;
deque.emplace_back(spring);       // raw-pointer hand-off
deque.back().shapeLabel = ...;
deque.back().keyX = ...;
deque.back().keyY = ...;
```

这里有两个必须分开的异常阶段：

1. `Spring(elem)` 构造抛出时，new-expression 自动删除刚分配的 72 字节 allocation；
2. 构造已完成但尚未成功 emplace 时，局部变量仍是裸指针。`op/p/pv/ofs` 读取或
   deque grow 抛出会泄漏 spring；成功 emplace 后才由 entry owner 接管。

因此，把 builder 写成 `make_unique` 再 move 会错误地修复原版 pre-emplace 泄漏；
继续保留“`new T;` + 普通 free ctor”又会丢失构造失败时的 new-expression 回滚。

## 2. 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `bustControl` builder | `0x6683F8` | `0x55659C` | `0x1001A7DDC` | `0x1A730C` |
| 72B spring constructor | `0x65F828` | `0x55176C` | `0x1001A18C4` | `0x1A099C` |
| raw emplace | inline `0x66874C` | `0x556B50` | `0x1001A84B0` | `0x1A7AA8` |
| boundary raw emplace | inline `0x668794` | `0x56706C` | included above | included above |
| builder EH cleanup | inline `0x668A80..0x668BFC` | no separate visible landing | `0x1001A821C` | `0x1A7766` |
| entry range/clear | `0x6800DC` | `0x562A70` | `0x1001B7164` | `0x1B6D4E` |
| full deque destructor | `0x681D6C` | `0x564058` | `0x1001B8B04` | `0x1B8124` |

所有未知原始标识符写回 IDB 时继续带 `_guess`。绝对地址只记录在本文，不进入
编译源码注释。

## 3. Entry 自然布局与 deque block

| ABI | entry 布局 | 大小 | 单 block 大小 | entry 数 |
| --- | --- | ---: | ---: | ---: |
| Android arm64 | owner `+0`，init `+8`，strings `+12/+20/+28`，anchors `+36/+40` | 48B | 480B | 10 |
| Android armv7 | owner `+0`，init `+4`，strings `+8/+12/+16`，anchors `+20/+24` | 28B | 504B | 18 |
| iOS arm64 | 同 arm64 字段偏移 | 48B | 4080B | 85 |
| iOS armv7 | 同 armv7 字段偏移 | 28B | 4088B | 146 |

64 位三个 `ttstr` 各占 8 字节，32 位各占 4 字节；64 位 entry 的 48B stride
包含自然尾 padding。Android 使用 libstdc++ 的较小 block，iOS 使用 libc++ 的
4096-byte 目标 block 策略。这些 ABI 差异不进入共享 C++ 的手工 padding。

entry range/clear 的四端机器次序一致：

```text
for entries in reverse:
    destroy keyY
    destroy keyX
    destroy shapeLabel
    owned = entry.spring
    entry.spring = null
    delete owned
```

这正是声明顺序 `{unique_ptr, initFlag, shapeLabel, keyX, keyY, anchorX, anchorY}`
的逆成员析构，而不是 Engine 在外层维护的一组非拥有裸指针。

## 4. 真实 constructor 与 new-expression 回滚

四端 constructor 都先写 `firstFlag=1`，从共享零向量复制三组 vec3，再按
`gravity/spring/friction/scale_x/scale_y` 的顺序读取 double 并窄化为 float。
它没有写 `biasY` 和 `prevDeltaX/Y`；builder 随后只覆盖 `biasY` 和三组 vec3，
`prevDeltaX/Y` 继续保持 allocation 中原有的未定义字节，直到 solver 首次分支写入。

三份有清晰调用者异常路径的参考进一步证明这是 C++ 参数构造函数，而不是 builder
调用的普通初始化 helper：

- Android arm64 在 `0x6685FC` 分配、`0x66860C` 调 constructor；constructor throw
  落到 `0x668B38..0x668B44`，对 allocation 调 `operator delete` 后继续 unwind；
- iOS arm64 在 `0x1001A7EF8` 分配、`0x1001A7F08` 调 constructor；对应 landing
  在 `0x1001A826C..0x1001A8278` 删除 allocation；
- iOS armv7 的 SjLj call-site 10 是 constructor call；EH switch 的 case 10
  在 `0x1A77D6..0x1A77E0` 删除 allocation；后续 call-site 不走该 case；
- Android armv7 同样是紧邻的 `operator new(0x48)` + `EmoteSpringState_ctor_guess`
  序列，但该产物没有保留可独立识别的调用者 landing body。它的正常数据流、对象
  大小和其余三端一致，不用缺失的展开形式反推不同源码。

本地据此把 free helper 恢复为 `EmoteSpringState(const tTJSVariant&)`，builder 使用
`new EmoteSpringState(elem)`。保留 defaulted default constructor 只服务纯数学测试和
portable 手工状态构造；metadata 原生路径不调用它。

## 5. 构造后裸指针窗口与 raw emplace

constructor 返回后，四端按相同顺序读取 `param.op`、`param.p`、`param.pv`、
`param.ofs`，然后把 raw spring 的地址传给 deque emplace。emplace 并不 move 一个
临时 entry，也不把 source raw slot 清零：

- fast path 直接把 raw pointer 复制到目标 `+0`，写 init byte `1`，把三个字符串
  和 anchors 清零；
- boundary path 先扩展 map/分配新 block，再执行相同目标写入；
- grow/allocate 抛出发生在目标 owner 构造前，因此 raw spring 仍无人拥有；
- 成功返回后，目标 entry 已拥有 spring，随后 label/HM6 写入抛出不会回滚 entry。

Android arm64 的 fast/boundary 路径内联；Android armv7 使用一个 fast helper，并
tail-call 独立的 504B boundary helper；两个 iOS helper 根据 libc++ start/size 计算
位置并在需要时 grow。四份 helper 都只读 source raw slot，没有清空动作。

这给出精确异常矩阵：

| 抛出位置 | allocation 状态 | deque 状态 |
| --- | --- | --- |
| spring constructor 内 | new-expression 删除 allocation | 无 entry |
| `op/p/pv/ofs` 读取 | 已构造 spring 泄漏 | 无 entry |
| deque map/block grow | 已构造 spring 泄漏 | 无新 entry |
| fast/boundary owner 写入完成后 | entry owner 接管 | entry 保留 |
| shape/key/HM6 写入 | entry owner 最终负责删除 | 部分初始化 entry 保留 |

## 6. 本地恢复

本轮修改：

1. `EmoteSpringState_ctor(self, dict)` 改为真正的参数 constructor；
2. `EmoteHairPartsNode48B::spring` 从 raw pointer 改为单指针 `unique_ptr`；
3. entry 增加 raw-pointer constructor，精确表示目标 owner 的接管点；
4. builder 保留 `EmoteSpringState *spring` raw local，并执行
   `_hairPartsNodes.emplace_back(spring)`，没有引入临时 `unique_ptr`；
5. step 调用通过 `.get()` 借用 spring；reset/正常析构删除 #1 的外层 delete loop，
   由 entry 逆成员析构完成相同释放；
6. 清除该 builder 路径编译源码中的旧单目标地址注释。

默认 entry constructor 仍供测试直接构造空节点；native builder 使用 raw-pointer
constructor，写 `initFlag=1` 且把 anchors 置零。`unique_ptr` 与 raw pointer 都是单指针
字段，因此不改变四端已证明的 entry 自然布局。

## 7. IDB 改进与验证

四份 recovery IDB 已写入：

- `EmoteHairPartsControlDeque_emplaceRaw_guess`；
- Android armv7 的 `EmoteHairPartsControlDeque_emplaceRawBoundary_guess`；
- `EmoteHairPartsControlEntryRange_destroy_guess` 或
  `EmoteHairPartsControlDeque_clear_guess`；
- iOS builder/constructor 的 `*_EHCleanup_guess`；
- IDA 专用 48B/28B entry layout；
- allocation、constructor、raw hand-off、grow leak、owner 逆析构的逐点注释。

四库均已 force-recompile 并原位保存。验证结果：

- 完整 `cmake --build --preset "Web Debug Build"` 通过，成功链接最终
  `index.html/index.wasm`；输出只有仓库既有 `_tss`、imagepacker attribute 和
  Emscripten JS/experimental warning；
- 复用 Web Debug 中 `EmoteEngine.cpp` 的真实 Emscripten defines/includes/ABI 参数，
  加入既有 `out/syntax-check` Catch2/test config，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过，唯一诊断是
  仓库既有 `_tss` literal-operator warning；
- 新增测试覆盖 raw-pointer entry constructor 的 owner 接管、move 后 source owner 清空、
  initFlag 与 anchor 零值；当前没有可运行的有效原生 Catch2 目标，因此没有把翻译单元
  编译误报成运行时执行；
- `git diff --check`：通过；只有工作树既有的 LF→CRLF 提示。

单一纵切面闭合不代表整个 motionplayer 恢复目标完成。
