# MotionPlayer EmoteVarController 生命周期四参考二进制复原（2026-08-11）

## 结论

本轮针对 `EmoteVarController.h` 中“析构函数尚未单独逆向，只做保守清理”的
旧注释，重新追踪四个当前参考二进制的 constructor、reset、正常析构、
ctor-unwind 与各 owner 清理路径。该注释已被四端证据直接推翻。

共同源级结构是一个无 vtable 的 owning class：

```cpp
class EmoteVarController {
    std::deque<Keyframe20B> queue;
    int count;
    int state;
    float *currentValue;
    float *startValue;
    float *targetValue;
    float powCount;      // ctor 不写
    float phase;         // ctor 不写
    float invDuration;   // ctor 不写
public:
    explicit EmoteVarController(int count);
    ~EmoteVarController();
};
```

constructor 依次构造 deque、写 `count/state`、分配并清零三块 `count*sizeof(float)`
数组；不写最后三个运行时标量。destructor body 依次释放
`currentValue/startValue/targetValue`，随后 C++ 自动析构先前声明的 deque，最后
caller 释放 controller 对象存储。

因此旧端口有三处源级偏差：

1. 把真实 class constructor/dtor 表达成手调 free helper；
2. constructor 额外把 `powCount/phase/invDuration` 清零；
3. 把 Android ARM64 尾部的对齐空洞误建模成 `int32_t pad` 字段。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `EmoteVarController_ctor` | `0x664410` | `0x554180` | `0x1001A4AD0` | `0x1A3FEC` |
| `EmoteVarController_step` | `0x663FD8` | `0x554014` | `0x1001A48C0` | `0x1A3E48` |
| `EmoteVarController_reset_guess` | `0x66451C` | `0x554208` | `0x1001A4B94` | `0x1A410C` |
| `EmoteVarController_dtor` | `0x680E88` | `0x563536` | `0x1001C46DC` | `0x1C1D62` |
| deque destructor tail | 在 dtor 内联 | `0x565428` | `0x1001B6FC0` | `0x1B6C40` |

上述 ctor/dtor/reset 语义名和 source-level prototype 已写回四份 IDB；关键
位置加入构造未初始化尾字段与析构次序注释，强制重新反编译后保存。

## ABI 布局

| 字段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| object size | `0x80` | `0x48` | `0x60` | `0x38` |
| deque | `+0`, 80B | `+0`, 40B | `+0`, 48B | `+0`, 24B |
| count/state | `+80/+84` | `+40/+44` | `+48/+52` | `+24/+28` |
| current array | `+88` | `+48` | `+56` | `+32` |
| start array | `+96` | `+52` | `+64` | `+36` |
| target array | `+104` | `+56` | `+72` | `+40` |
| powCount | `+112` | `+60` | `+80` | `+44` |
| phase | `+116` | `+64` | `+84` | `+48` |
| invDuration | `+120` | `+68` | `+88` | `+52` |
| tail alignment | `+124..127` | 无 | `+92..95` | 无 |

Android 两端使用 libstdc++ deque，iOS 两端使用 libc++ deque；pointer width、
deque header 与最终 alignment 共同产生这些差异。四端字段顺序和所有权相同，
不应在 portable Web class 中硬编码任一 native byte layout。

旧源码中的显式 `int32_t pad` 只对应两个 64 位 ABI 的隐式结构体尾对齐，并非
参考实现读写的成员。移除它以后，portable compiler 自己产生其 ABI 所需的对齐。

## constructor 数据流与未初始化边界

四端共同伪代码为：

```cpp
EmoteVarController::EmoteVarController(int count) {
    // queue 已由其成员构造函数建立为空 deque
    this->count = count;
    this->state = 0;
    currentValue = new float[count];
    startValue   = new float[count];
    targetValue  = new float[count];
    memset(currentValue, 0, 4 * count);
    memset(startValue,   0, 4 * count);
    memset(targetValue,  0, 4 * count);
    // powCount/phase/invDuration 不写
}
```

数组分配大小在四端都是 `4*count` 字节，不是 `count*4` 个 float；乘法溢出时
把 allocation size 变成最大无符号值并进入 operator-new failure 路径。三个数组
按 current、target、start 顺序分配和清零。

尾部三个标量保持 allocator 提供的旧字节。正常状态机不会无条件读取它们：

- `state==0 && queue.empty()` 只输出已清零 current 数组；
- 消费 keyframe 时先写 `invDuration/powCount/phase`，再进入 active step；
- reset 只有 `state!=0` 时读取 destination 数组，不读取未建立的 phase/power。

本地不对这些字段做防御性初始化，以保持参考构造边界。它们的未初始化值不得用
测试直接读取；验证应针对状态门控后的可观察行为。

## reset 边界

四端 reset 共同逻辑：

```cpp
if (!queue.empty()) {
    state = 0;
    copy queue.back().channels[0..count) -> currentValue;
    queue.clear();
} else if (state != 0) {
    state = 0;
    copy targetValue[0..count) -> currentValue;
}
```

Android libstdc++ 版本从 end cursor 回退到最后一个 20B 元素并释放多余 deque
block；iOS libc++ 版本用 size 与 block-map 算出最后元素，再调用 libc++ clear。
空 queue 且 idle 时不写任何尾字段。旧函数名把旧 Android 地址编码进 C++ 标识符，
现统一改为 `EmoteVarController_reset_guess`，地址只保存在本文。

## destructor 与 owner 调用链

四端 destructor body 均为：

```cpp
EmoteVarController::~EmoteVarController() {
    delete[] currentValue;
    delete[] startValue;
    delete[] targetValue;
} // 随后自动析构 queue
```

每个 `delete[]` 前都有 null check；C++ 的 `delete[] nullptr` 与其等价。参考实现
不把三个指针写回 null，因为对象马上进入 deque 析构并释放 storage。本地也不再
对即将死亡的指针做额外 null 写入。

fresh xrefs 证明同一 destructor 被以下 owner 复用：

- EmoteEngine 六个 direct VarController（angle 使用独立类型）；
- Transition deque 的 `unique_ptr` owning entry；builder 从已完成 new-expression 的 raw
  pointer 直接 emplace，element reverse destruction 在 label 后调用本 destructor；
- timeline variable track 的 owning controller；
- HM3 timeline state 的 blend controller；
- Engine constructor 的异常展开清理；
- 对应容器 element destructor/erase 路径。

Selector option 中的 controller 指针仍是 borrowed，不触发该 destructor。每个正常
owner 都是 destructor 后紧接 `operator delete`，符合一个普通 C++ `delete ptr`
表达式；旧端口的 `EmoteVarController_dtor(ptr); delete ptr;` 是反编译调用形态的
机械展开，不是最接近原源码的结构。

## 本地修改与验证

源代码修改：

- 用 `EmoteVarController(int)` 与 `~EmoteVarController()` 取代 ctor/dtor free
  helper；
- 所有 heap owner 改成单一 `delete`，stack 测试对象依赖自动析构；
- constructor 不再写 `powCount/phase/invDuration`；
- 删除假的 `pad` 源字段；
- 旧地址编码函数名改为 `EmoteVarController_reset_guess`；
- 清理本文件范围内把旧 `libkrkr2.so` 地址当作当前证据的注释。

2026-08-13 对 Transition deque #8 的独立四端追踪又确认该 owner 不是 Engine 外层 raw
delete loop，而是 element 内的单指针 `unique_ptr`；其 raw-emplace、grow-failure 泄漏和
post-emplace 异常边界另见
`analysis/motionplayer_transition_entry_owner_emplace_four_binary_2026-08-13.md`。

验证结果：Web Debug 与 Wasmtime Debug 增量构建均成功（两者均已收敛到
`ninja: no work to do.`）；使用 Web Debug 的真实 Emscripten 编译参数对
`tests/unit-tests/plugins/motionplayer-dll.cpp` 做完整 `-fsyntax-only` 检查通过，
只有仓库既有的 `_tss` literal-operator deprecation warning；`git diff --check`
通过，仅报告工作树既有的 LF/CRLF 转换提示。
