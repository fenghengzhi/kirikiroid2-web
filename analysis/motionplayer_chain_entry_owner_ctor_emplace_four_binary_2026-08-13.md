# MotionPlayer chain entry owner / constructor / emplace：四参考二进制闭环（2026-08-13）

## 1. 范围与结论

本文闭合 `EmoteEngine` deque #2/#3 的共同 element specialization。两次调用分别把
`hairControl` 与 `partsControl` 交给同一个 builder，传入 type tag 1/2；两个 deque 在
正常析构和 constructor unwind 中也都调用同一个 specialization destructor。联合恢复的
源代码形态是：

```cpp
struct Entry {
    std::unique_ptr<ChainSpring> spring;
    unsigned char initFlag; // raw-pointer constructor 故意不初始化
    ttstr shapeLabel;
    ttstr keyA;
    ttstr keyB;
    ttstr keyC;
    float anchorX;
    float anchorY;

    explicit Entry(ChainSpring *raw)
        : spring(raw), anchorX(0), anchorY(0) {}
};

ChainSpring *spring = new ChainSpring(elem);
populate op/ofs/bendR/bendS/p/pv/bp through raw spring;
chainDeque.emplace_back(spring); // 此处才把 raw pointer 交给 entry owner
assign shapeLabel/keyA/keyB/keyC;
register three HM6 entries;
```

所以本地历史上的 `EmoteBustChainSpring_ctor(self, elem)` free helper 和 entry 外层显式
`delete` 循环都不是一比一源结构：前者是参数 constructor，后者应由 entry 内的单指针
`unique_ptr` 逆成员析构完成。

## 2. 四端映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| shared chain builder | `0x668DB0` | `0x556B84` | `0x1001A87C0` | `0x1A7DCC` |
| real argument constructor | `0x6662D8` | `0x5554F0` | `0x1001A6104` | `0x1A5710` |
| raw emplace | inline `0x669428` | inline `0x556F1A` | `0x1001A912C` | `0x1A8790` |
| boundary raw emplace | inline `0x669454` | `0x56743E` | included above | included above |
| constructor-failure delete | `0x669A08` | caller EH 未拆为独立 landing | `0x1001A906C` | SjLj case 10 `0x1A854E` |
| entry/range clear | `0x68029C` → `0x680350` → `0x680458` | `0x562B80` → `0x562BC8` → `0x562C4C` | `0x1001B7298` | `0x1B6E2A` |
| full deque destructor | `0x681CA8` | `0x563FDC` | `0x1001B8ABC` | `0x1B80FC` |

四端 builder 的两处 `applyMetadata` caller 分别提供 type tag 1/2；full destructor helper
也各被 Engine 的 #3/#2 member phase 调用两次。这比仅观察布局更强，证明两组 container
复用同一 entry 类型与 builder，而不是两个碰巧同尺寸的独立实现。

## 3. element 自然布局、block capacity 与未写 init byte

| 平台 | element | owner | init byte | 四个 `ttstr` | anchors | block / capacity |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Android arm64 | 56B | `+0` | `+8` | `+12/+20/+28/+36` | `+44/+48` | 504B / 9 |
| Android armv7 | 32B | `+0` | `+4` | `+8/+12/+16/+20` | `+24/+28` | 512B / 16 |
| iOS arm64 | 56B | `+0` | `+8` | `+12/+20/+28/+36` | `+44/+48` | 4088B / 73 |
| iOS armv7 | 32B | `+0` | `+4` | `+8/+12/+16/+20` | `+24/+28` | 4096B / 128 |

四端 raw emplace 都只复制 source raw pointer，不清空 source slot：

- Android arm64 fast 与 boundary 路径写 owner `+0`，清零 `+12..+51`，不写 `+8..+11`；
- Android armv7 fast 路径 `STR owner; memclr(+8, 0x18)`，boundary helper 相同，不写
  `+4..+7`；
- iOS arm64 helper 写 owner 以及 `+12/+20/+28/+36/+44` 的零值，不写 `+8..+11`；
- iOS armv7 helper 写 owner，清零 `+8..+31`，不写 `+4..+7`。

共同源码最合理且能保持这一边界的表达是接收 raw pointer 的 entry constructor：owner
和 anchors 出现在 initializer list，四个 `ttstr` 默认构造，而 `initFlag` 被省略。普通
`emplace_back()` 再赋 owner 会 value-initialize entry，错误地把这个 byte 稳定清零。

## 4. 逆成员析构证明 owner

四端 clear/range helper 的每 entry 顺序一致：

```text
destroy keyC
destroy keyB
destroy keyA
destroy shapeLabel
owned = entry.spring
entry.spring = null
delete owned
```

Android arm64 是 `+36 -> +28 -> +20 -> +12 -> +0`；32 位两端是
`+20 -> +16 -> +12 -> +8 -> +0`；iOS arm64 是
`+36 -> +28 -> +20 -> +12 -> +0`。这正是声明顺序
`{unique_ptr, initFlag, shapeLabel, keyA, keyB, keyC, anchorX, anchorY}` 的逆成员析构，
而不是 Engine 外层持有一组非拥有裸指针。

## 5. constructor/new-expression 与异常矩阵

四端 builder 都先分配 ABI-sized spring（64 位 176B、32 位 168B），紧接着调用接收
`elem` 的 spring constructor。iOS armv7 尤其清楚：allocation call-site 10，constructor
call-site 11；SjLj case 10 对 pending allocation 调 `operator delete`。Android arm64 与
iOS arm64 也有对应 landing delete；Android armv7 虽没有被 IDA 拆出同样清晰的独立
landing，allocation 紧邻 constructor 的共同 new-expression 数据流一致。

constructor 返回后，builder 才从 `param` 读取并覆盖 `op/ofs/bendR/bendS/p/pv/bp`；
随后 raw emplace。精确异常状态是：

| 抛出位置 | spring allocation | deque 状态 |
| --- | --- | --- |
| spring constructor 内 | `new` 表达式回收 | 不追加 |
| constructor 后的 property/array 转换 | raw pointer 泄漏 | 不追加 |
| deque grow / block allocation | raw pointer 泄漏 | 不追加 owner |
| raw emplace 成功后的 label/HM6 | entry owner 保留 spring | 部分初始化 entry 保留 |

因此本地必须写 `new ChainSpring(elem)`，但不能在 property 阶段用临时
`unique_ptr` 做“安全改进”；后者会消除参考实现真实存在的泄漏窗口。

## 6. 共同 builder 数据流与本地对照

四端共同顺序为：

1. 把 `chainControl` 包装为 accessor，读取 count；
2. 逐 index 取 element，`enabled != true` 则跳过；
3. 取 `param`；`new ChainSpring(element)`；
4. 覆盖 `op`、`ofs`、`bendR`、`bendS`；
5. 取 `bp`、`p`、`pv` accessor，按 segment 0/1 依次覆盖六个 vec3；
6. `chainDeque.emplace_back(rawSpring)`；
7. 顺序写 `baseLayer`、`var_lr`、`var_lrm`、`var_ud`；
8. 以同一个 metadata loop index 注册 keyA/keyB/keyC，type 为调用者传入的 1/2。

本地本轮逐行对照：

- `EmoteBustChainSpring` 增加 `explicit ... (const tTJSVariant&)`，原 free helper 改成
  member constructor；保留 default constructor 供纯数学测试手工构造；
- `EmoteBustChain1Node56B::spring` 改为 `unique_ptr`，raw-pointer constructor 不初始化
  `initFlag`，只接管 spring 并把 anchors 置零；deque #3 继续复用这一 entry alias；
- builder 保留 raw local 与所有 property 顺序，仅把构造改成 `new ...(elem)`，把追加改成
  `emplace_back(spring)`；没有引入临时 owner；
- step 通过 `.get()` 借用；reset 与正常析构删除 #2/#3 外层 delete loop；
- 清除本纵切面 compiled C++ 中的旧单目标绝对地址注释；地址只保留在本文。

## 7. IDB 改进

四个 recovery IDB 新增 56B/32B entry layout type，并为 builder、raw emplace、clear/range、
entry/full destructor、constructor-failure cleanup 写入统一语义名/注释。未知原始名字继续用
`_guess`。四库已 force-recompile 并原位保存。

验证结果：

- 完整 `cmake --build --preset "Web Debug Build"` 通过，最终链接 `index.html`/wasm；
- 复用 Web Debug `EmoteEngine.cpp` 的真实 Emscripten defines/includes/ABI 参数，加入既有
  `out/syntax-check` Catch2/test config，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 `-fsyntax-only`：通过，唯一诊断是
  仓库既有 `_tss` literal-operator warning；
- 新增测试覆盖 raw-pointer entry constructor 的 owner 接管、anchor 零值及 move 后 source
  owner 清空；`initFlag` 是原版故意未初始化边界，因此测试不读取它；当前没有可运行的有效
  原生 Catch2 目标，没有把翻译单元编译误报成 runtime 执行；
- `git diff --check` 通过（仅报告工作树既有 LF→CRLF 提示）。
