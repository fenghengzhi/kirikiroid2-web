# libkrkr2.so 差分测试方案

## 背景与目标

当前项目的目标是让代码架构、逻辑、实现、行为都完全对齐 libkrkr2.so。但"对齐"的判定目前依赖人工 diff 反编译伪代码，缺乏自动化回归手段，无法保证后续修改不会破坏已对齐的函数。

本方案设计一套**差分测试 (differential testing)** 框架：同一套测试用例同时驱动两个 backend，

- **Backend A** — 加载 libkrkr2.so，在模拟器中执行目标函数（ARM64）
- **Backend B** — 加载本地项目产物，调用对应的本地实现

两者在相同输入下的输出必须一致。Backend A 的作用是验证测试用例本身写得对（因为 libkrkr2.so 是权威），Backend B 的作用是验证本地实现对齐。

## 整体架构

```
                    同一套 Python 测试用例 (pytest)
                             │
                             ▼
              ┌──────────────────────────────┐
              │     统一 Backend 接口        │
              │  call(func, *args) -> result │
              └──────────┬───────────────────┘
                         │
          ┌──────────────┴──────────────┐
          ▼                              ▼
   ┌─────────────┐                ┌──────────────┐
   │  Backend A  │                │  Backend B   │
   │             │                │              │
   │  Unicorn /  │                │  wasmtime /  │
   │  Qiling     │                │  ctypes /    │
   │             │                │  子进程      │
   │  加载       │                │              │
   │  libkrkr2.so│                │  加载本地    │
   │  (ARM64)    │                │  构建产物    │
   └─────────────┘                └──────────────┘
```

每个 backend 实现相同的抽象接口，测试用例通过 pytest parametrize 同时跑两个 backend，断言结果一致。

## Backend A: libkrkr2.so 模拟执行

### 技术选型

| 工具 | 适用场景 |
|---|---|
| **Unicorn Engine** | 纯 CPU 指令模拟，轻量、快速。适合调用叶子函数或可手动 stub 依赖的函数 |
| **Qiling Framework** | 基于 Unicorn，额外模拟 Android linker / syscall / libc，可以直接 dlopen+调用导出符号 |

推荐优先使用 **Qiling**——libkrkr2.so 内部会调用 libc / libdl / libandroid 符号，手动 stub 工作量大。

### 关键步骤

1. 用 Qiling 加载 libkrkr2.so 到模拟地址空间
2. 通过符号名或已知地址定位目标函数入口
3. 按 AAPCS64 调用约定准备寄存器/栈（x0-x7 传参，x30 = 返回地址）
4. 为 `this` 指针、结构体参数在模拟内存中分配空间并写入初始字段
5. 启动模拟执行到指定返回地址
6. 从寄存器/内存读回结果并反序列化

### 函数定位

libkrkr2.so 的函数大多是 C++ mangled 符号或内部静态函数，可以：

- 用 IDA MCP 已命名的函数（见 `.claude/skills/ida-decompile/SKILL.md` 的 Named Functions 表）记录的地址
- 或用 `readelf -s libkrkr2.so` 查导出符号

## Backend B: 本地实现调用

本地代码不是必须编译成 native，有三种方案：

### 方案一：Native 动态库 + ctypes（推荐先行方案）

```bash
cmake --preset "MacOS Release Config" -DBUILD_SHARED_LIBS=ON ...
```

在 Python 侧：

```python
import ctypes
lib = ctypes.CDLL("out/macos/release/libkrkr2_test.dylib")
lib.my_function.argtypes = [ctypes.c_float, ctypes.c_int]
lib.my_function.restype = ctypes.c_float
```

- **优点**：调试方便，gdb/lldb 可直接 attach；ctypes 生态成熟
- **缺点**：和生产目标 (WASM) 不是同一份产物，理论上可能有编译器差异

### 方案二：WASM + wasmtime-py（测的就是生产产物）

```python
import wasmtime

engine = wasmtime.Engine()
store = wasmtime.Store(engine)
module = wasmtime.Module.from_file(engine, "out/web/debug/index.wasm")
instance = wasmtime.Instance(store, module, [...])
func = instance.exports(store)["_my_function"]
result = func(store, 1.0, 42)
```

需要在 CMake 侧通过 `-s EXPORTED_FUNCTIONS=['_my_function',...]` 或 `EMSCRIPTEN_KEEPALIVE` 把目标函数导出。

- **优点**：测的就是上线的 wasm 产物，100% 准确
- **缺点**：WASM 内存是线性地址空间，构造复杂对象时指针转换稍繁琐

### 方案三：子进程 + 文本 IO

写一个小的 C++ 测试 driver，编译成可执行文件。Python 用 `subprocess` 调起，通过 stdin 传输入、stdout 读结果。

- **优点**：最简单，不需要 FFI
- **缺点**：fork 开销大，每个用例数百毫秒；只适合少量用例

### 推荐路径

**阶段 1** 用方案一（native .dylib）快速验证框架可行性，**阶段 2** 切换到方案二（WASM）以对齐生产环境。

## 核心挑战

### 挑战 1：对象内存布局

很多要测的函数是成员函数，`this` 指针指向一块有特定字段布局的内存。两个 backend 构造同样的对象需要：

- 用 IDA 反编译确定 libkrkr2.so 中对象的字段偏移、类型、大小
- 用 `offsetof` / `static_assert(sizeof)` 确认本地 C++ 结构体和 libkrkr2.so 一致
- 写一个 Python 侧的 `@dataclass` 作为输入描述，两个 backend 的 `call()` 内部各自把它序列化到自己的内存中

```python
@dataclass
class LayerState:
    x: float
    y: float
    width: int
    height: int
    transform: List[float]  # 3x3

class UnicornBackend:
    def _serialize(self, obj: LayerState) -> int:
        # 在 Unicorn 模拟内存中按 libkrkr2.so 布局写入，返回指针
        ...

class WasmBackend:
    def _serialize(self, obj: LayerState) -> int:
        # 在 WASM 线性内存中按本地 struct 布局写入，返回指针
        ...
```

**前提**：本地结构体布局必须和 libkrkr2.so 完全一致——这本来就是对齐目标的一部分，所以差分测试也顺带验证了布局对齐。

### 挑战 2：依赖函数（vtable / 回调 / 子函数）

目标函数往往会调用其他函数（TJS dispatch、内存分配、虚函数等）。处理策略：

- **纯函数 / 叶子函数**：直接跑，最简单
- **调用 libc/linker 符号**：Qiling 已经 stub 了大部分，不需要额外处理
- **调用 libkrkr2.so 内部其他函数**：让它跑，前提是被调用函数也已对齐或无副作用
- **调用外部回调（TJS2 dispatch）**：Unicorn hook 指令地址 + 注入 Python 回调模拟返回值，或者提前在模拟内存构造假的 vtable

### 挑战 3：调用约定差异

| Backend | 架构 | 调用约定 |
|---|---|---|
| A (libkrkr2.so) | ARM64 | AAPCS64 (x0-x7 参数, v0-v7 浮点) |
| B (WASM) | wasm32 | Emscripten ABI (stack-based, i32 指针) |
| B (native macOS) | x86_64 / arm64 | System V / AAPCS64 |

Python 的统一接口 `call()` 必须隔离这些差异，每个 backend 内部自己处理参数编组。

## 测什么函数优先

按"构造难度 vs. 价值"排序：

### 优先级 1：纯算法函数
- PSB RL 解压 (`TVPDecompressPSBRLImpl` 或类似)
- 坐标变换矩阵计算
- 插值 / 缓动函数

构造简单：输入字节数组或几个 float，输出字节数组或 float。

### 优先级 2：有 this 但字段少的成员函数
- `MotionPlayer::UpdateSomething`
- `EmotePlayer::Update`
- Layer 的局部 transform 计算

需要构造 this 对象但字段 < 20 个。

### 优先级 3：渲染链上的核心函数
- `Layer::Update`
- `DrawDevice` 的坐标计算
- 有回调依赖，需要 hook

### 不适合差分测试的
- GC / 内存管理
- 深度依赖 TJS2 runtime 的函数
- 涉及 OpenGL / D3D 调用的渲染输出

## 分阶段实施路线

### Phase 1: 框架搭建（1-2 天）
1. 选一个最简单的纯函数（比如 PSB 解压的某个 helper）
2. 搭出 `BackendBase` 抽象类和 pytest 框架
3. Unicorn backend 能加载 libkrkr2.so 并调用该函数
4. Native .dylib backend 能调用本地对应函数
5. 一个绿色的 pytest 用例

### Phase 2: 对象序列化（2-3 天）
1. 实现 `@dataclass` → 内存的序列化器
2. 写 `sizeof` / `offsetof` 断言，确保本地结构体和 libkrkr2.so 布局一致
3. 测第一个成员函数

### Phase 3: 扩展到渲染链（持续）
1. 逐步把 analysis/ 里已分析过的函数加进测试套
2. 对每个已对齐的函数锁定差分测试，防止回归

### Phase 4: 切换到 WASM backend（可选）
当方案一稳定后，切换 Backend B 到 wasmtime-py，保证测的是真正的生产产物。

## 预期收益

1. **自动化回归**：任何对 cpp/ 的修改都能通过 `pytest tests/diff/` 验证是否破坏对齐
2. **精确定位偏差**：测试失败时能精确指出是哪个函数、哪个字段、哪个中间结果出现差异
3. **补全 analysis/ 文档**：写测试用例本身就是对函数行为的精确描述，是比 Markdown 更严格的文档
4. **降低反编译门槛**：后续维护者不需要每次都手动对比 IDA 伪代码，测试套就是活的参考

## 开放问题

- **浮点精度**：ARM64 和 x86_64 的浮点运算可能有最低位差异，是否需要容忍 epsilon？
- **未定义行为**：libkrkr2.so 编译时可能有特定的 UB 行为，本地 clang 不一定一致
- **多线程**：目前只考虑单线程函数，涉及 TLS / mutex 的函数需要额外设计
- **测试数据来源**：手写输入 vs. 从真实游戏运行时 dump 的状态快照——后者覆盖面更广但需要额外的抓取工具

## 参考

- Unicorn Engine: https://www.unicorn-engine.org/
- Qiling Framework: https://qiling.io/
- wasmtime-py: https://github.com/bytecodealliance/wasmtime-py
- 已有对齐分析文档: `analysis/` 下各 `*_libkrkr2so.md`
