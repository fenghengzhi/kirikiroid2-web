# motionplayer HM3 timeline/track 单指针 owner 生命周期四参考复原（2026-08-13）

## 结论

本轮对 `reference/binaries/` 的 Android arm64、Android armv7、iOS arm64、
iOS armv7 四份当前参考产物重新取证，纠正了本地 HM3 timeline state 与 Track
仍用普通 raw pointer、手写 `delete`、手写 move special members 表达 ownership 的
过渡实现。

四端共同支持的源结构是：

```cpp
struct EmoteTimelineTrack {
    ttstr label;
    bool instantVariable;
    std::vector<EmoteTimelineFrame24B> frameList;
    std::unique_ptr<EmoteVarController> controller;
    float output;
};

struct EmoteTimelineData {
    std::deque<EmoteTimelineTrack> variableList;
};

struct EmoteHM3Value {
    std::unique_ptr<EmoteTimelineData> timelineData;
    std::unique_ptr<EmoteVarController> blendController;
    // flags, rawElement, timeline scalars and frameCursors follow
};
```

这里的 `unique_ptr` 是“单根 pointer、默认 deleter、move-only owner”的源级表达，
不会增加字段大小。证据不只是 normal destructor 中出现 `delete`：四端的替换顺序、
owner slot 清零、通用 reset helper、mapped/Track 逆声明次序析构和默认节点构造共同闭合。

## 四端函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| initialize timeline state | `0x66D03C` | `0x5590E8` | `0x1001ABD5C` | `0x1AB4B0` |
| initialize track controllers | `0x66DC20` | `0x559848` | `0x1001AC5DC` | `0x1ABDA4` |
| HM3 `operator[]` | `0x685060` | `0x5669AC` | `0x1001A6938` | `0x1A6074` |
| HM3 map dtor | Engine dtor 内联 | `0x564C38` | `0x1001B888C` | `0x1B7FC8` |
| HM3 node-chain destroy | Engine dtor 内联 | `0x5638D8` | `0x1001B7D2C` | `0x1B7562` |
| mapped/key destroy | `0x681220` | `0x563906` | node-chain 内联 | node-chain 内联 |
| TimelineData dtor | `0x681298` | `0x56395C` | `0x1001C44FC` | `0x1C1C2C` |
| Track range/clear | `0x6813C4` | `0x563A0C` | `0x1001C4544` | `0x1C1C54` |
| TimelineData owner reset | caller 内联 | `0x559814` | caller 内联 | caller 内联 |
| controller owner reset | caller 内联 | `0x55982E` | caller 内联 | caller 内联 |

这些地址只用于四参考证据定位，未写入编译源码注释或 helper 名。

## HM3 mapped value 与 hash node ABI

源字段相同，但 32 位 double 对齐规则和两套标准库 hash node 前缀不同：

| ABI | node 分配 | mapped 起点/大小 | node 前缀/尾部 |
|---|---:|---:|---|
| Android arm64 | `0x88` | `+0x10 / 112B` | next `+0`, key `+8`, cached hash `+0x80` |
| Android armv7 | `0x70` | `+0x10 / 88B` | next `+0`, pair 对齐槽 `+4`, key `+8`, value 对齐槽 `+0xC`, cached hash `+0x68`, tail pad |
| iOS arm64 | `0x88` | `+0x18 / 112B` | next `+0`, cached hash `+8`, key `+0x10` |
| iOS armv7 | `0x60` | `+0x0C / 84B` | next `+0`, cached hash `+4`, key `+8` |

两份 64 位 mapped value 均为 112B。两份 32 位字段偏移相同，但 Android ARM EABI
让 value 自然大小向 8 字节对齐到 88B，iOS armv7 则为 84B。共同字段布局为：

| 字段 | 64 位 mapped 偏移 | 32 位 mapped 偏移 |
|---|---:|---:|
| timelineData owner | `+0` | `+0` |
| blendController owner | `+8` | `+4` |
| flags | `+0x10` | `+8` |
| rawElement | `+0x14` | `+0x0C` |
| loopBegin | `+0x28` | `+0x18` |
| loopEnd | `+0x30` | `+0x20` |
| lastTime | `+0x38` | `+0x28` |
| currentTime | `+0x40` | `+0x30` |
| blendWeight | `+0x48` | `+0x38` |
| autoStop | `+0x50` | `+0x40` |
| frameCursors vector | `+0x58` | `+0x48` |

四端 `operator[]` miss 路径把前两个 owner slot 清零，默认构造 raw Variant/标量与
空 vector，并单独写 `blendWeight = 1.0f`。hit 路径原样返回既有 mapped value。

## owner replacement 数据流

`initializeTimelineState` 的四端共同顺序是：

```text
new TimelineData allocation
construct its deque header
oldTimeline = state.timelineData owner
install new TimelineData into owner slot
if oldTimeline: TimelineData dtor; operator delete

read loopBegin/loopEnd/lastTime and initialize scalar state

new EmoteVarController(1) allocation + constructor
oldBlend = state.blendController owner
install new controller into owner slot
if oldBlend: controller dtor; operator delete
```

Android armv7 把两次操作分别抽成 `0x559814` 和 `0x55982E`：helper 都是
`old=*slot; *slot=replacement; if(old) { dtor(old); delete old; }`。其他三端将同一逻辑
内联。这正是单指针 owner 的 `reset`/replacement 形状；尤其 replacement 在旧对象
销毁前已经完成 new-expression 和 constructor。

`initializeTimelineControllers` 也给出第二条独立证据：flags&2、非空且非 instant 的
Track 若已有 controller 就 reset target；若 owner 为空，则构造
`EmoteVarController(1)` 并写入同一 owner slot。Android armv7 再次调用 `0x55982E`，
虽然该分支的旧值按控制流必为空，通用 owner assignment 仍保留删除旧值的代码。

## Track/TimelineData 内部容器

Track 的自然大小和 offset 四端一致分成两组：

| ABI | Track 大小 | controller owner | frame vector | label |
|---|---:|---:|---:|---:|
| 两份 64 位 | 56B | `+40` | `+16` | `+0` |
| 两份 32 位 | 28B | `+20` | `+8` | `+0` |

Track range/clear 对每个元素严格执行：

```text
destroy controller owner
destroy frame vector
destroy label
```

这就是声明顺序 `label, bool, frameList, controller, output` 的逆序（POD 的 bool/output
没有析构）。若 controller 是普通 borrowed pointer，成员析构不会生成 slot 清零、
controller destructor 和 delete；若只是 Engine 外层手写回收，也不会稳定嵌入四种
STL deque 的 element range destruction specialization。

TimelineData 对象本身就是一个自然 ABI 的 `deque<Track>` header：

| ABI | TimelineData 大小 | block 字节 | Track/block |
|---|---:|---:|---:|
| Android arm64 | `0x50` | 504 | 9 |
| Android armv7 | `0x28` | 504 | 18 |
| iOS arm64 | `0x30` | 4088 | 73 |
| iOS armv7 | `0x18` | 4088 | 146 |

## mapped value 逆析构与对象生命周期

四端 HM3 节点都按同一源级次序销毁：

```text
frameCursors vector
rawElement Variant
blendController owner
timelineData owner
key ttstr
hash node allocation
```

具体 node-relative owner slot 为：Android arm64 `+24/+16`、Android armv7
`+20/+16`、iOS arm64 `+32/+24`、iOS armv7 `+16/+12`（前者为 blend，后者为
timeline）。这证明 mapped value 的声明顺序是 timeline owner 在前、blend owner 在后，
并且二者都由 mapped value 自身负责，而不是 Engine 另有外层 delete pass。

timeline owner 销毁继续进入 TimelineData deque dtor，再逐 Track 释放 controller owner；
因此完整所有权树是：

```text
unordered_map node
  -> EmoteHM3Value
       -> unique owner: TimelineData
            -> deque<Track>
                 -> each Track unique owner: EmoteVarController
       -> unique owner: blend EmoteVarController
       -> rawElement Variant owner
       -> frameCursors vector backing
```

## 异常与边界行为

- TimelineData/controller 的 allocation 或 constructor 抛异常时，C++ new-expression
  回收本次 allocation；旧 owner 尚未被替换，保持存活。
- replacement constructor 成功后立即进入 owner reset，没有 property lookup 或 deque
  grow 之类的 raw-pointer 暴露窗口。
- owner slot 先写 replacement，再销毁旧对象；旧析构抛异常虽不符合通常 C++ 析构契约，
  但从机器码顺序看 replacement 已经归 owner slot 所有。
- Track controller 只在 owner 为空的分支分配；构造成功后立即安装。已有 controller
  不替换，只 reset target。
- HM3 `operator[]` hit 不重置 owner 或运行时标量；重复 timeline label 仍复用既有状态。
- map clear/Engine dtor 释放全部 nested owner；源码不需要也不应再保留外层 delete loop。

## 源码、测试与 IDB 落地

- `EmoteTimelineTrack::controller` 改为 `std::unique_ptr<EmoteVarController>`；
- `EmoteHM3Value::timelineData` 与 `blendController` 改为相应 `unique_ptr`；
- 删除两个类型的手写 destructor、copy-delete 与 move ctor/move assignment；隐式
  move-only 语义现在由成员自然产生；
- builder 使用 owner `reset`，给 raw-pointer helper 的调用点显式 `.get()`；
- 测试中的直接 owner 构造改为 `reset(new ...)`，并新增 Track/HM3 两层 move-transfer
  用例，检查 moved-from owner 为空、目标 owner 保留原对象身份；
- 四个 recovery IDB 新增 Track、mapped value、TimelineData header 与 hash node ABI type，
  统一命名 owner reset、HM3 node clear、TimelineData dtor 和 Track clear/range helper，
  写入 source-level 注释与原型并强制刷新反编译。

## 验证

- 完整 `cmake --build --preset "Web Debug Build"`：通过，最终 `index.html`/Wasm 链接成功；
- 从 Web `compile_commands.json` 复用 EmoteEngine 的完整 Emscripten 参数，对完整
  Catch2 `motionplayer-dll.cpp` 翻译单元执行 `-fsyntax-only`：通过；只有项目既有
  `_tss` literal-operator 弃用警告；
- `git diff --check`：通过；只有工作区既有 LF/CRLF 提示；
- 四个 recovery IDB 均已写入类型/命名/注释、刷新相关反编译并保存。

当前 CMake 配置仍没有直接可运行该 Catch2 文件的 motionplayer test executable；
因此语法编译通过不能表述成 Catch2 runtime 通过。
