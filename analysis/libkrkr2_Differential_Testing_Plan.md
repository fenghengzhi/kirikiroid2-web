# libkrkr2.so 差分测试技术方案

## 1. 背景

当前项目的核心目标不是“功能大致可用”，而是尽可能在以下层面与 Android 原版 `libkrkr2.so` 保持一致：

- 代码架构
- 控制流和数据流
- 关键实现细节
- 对外行为和关键副作用

现有工作流已经强调“先反编译、再实现、再验证”。本文档提出一套可落地的**差分测试方案**，让同一份测试用例描述同时驱动：

- `libkrkr2.so` 原始实现
- 当前项目实现

再将两侧的观测结果做标准化后精确比较。

这套方案的定位是：

- 作为 `libkrkr2.so` 的**行为 oracle**
- 作为当前项目实现的**回归护栏**
- 作为“反编译对照工作流”的**第二道验证关卡**

它**不能替代** IDA 反编译对照，也不能单靠动态测试证明“代码架构完全一致”；但它能显著降低“逻辑偏了但表面还能跑”的风险。

## 2. 目标与非目标

### 2.1 目标

1. 用同一份测试 case 描述驱动两套实现。
2. 用 `libkrkr2.so` 的实际运行结果作为 oracle，而不是人工拍脑袋写 expected。
3. 对比的不只是返回值，还包括关键副作用、关键输出对象、关键调用轨迹。
4. 让测试覆盖从纯函数到复杂对象方法的多个层级。
5. 为后续函数对齐提供可扩展的测试基础设施。

### 2.2 非目标

1. 不试图用一套“零适配”的统一调用壳覆盖所有函数。
2. 不把所有函数都降为“原始内存按字节比较”。
3. 不把差分测试当作架构对齐的唯一依据。
4. 不要求第一阶段就覆盖渲染、线程、异步、TJS 全链路。

## 3. 核心结论

本方案推荐采用：

**Shared Spec + Dual Runner + Normalized Result + Differential Comparator**

其中：

- `Shared Spec` 负责定义“测什么”
- `Dual Runner` 负责分别调用 `libkrkr2.so` 和当前项目实现
- `Normalized Result` 负责把两边结果压成可比较格式
- `Differential Comparator` 负责统一断言

这意味着真正“同一套测试逻辑”的位置，不在 C++ 函数调用层，而在**测试 case 描述、结果标准化和对比规则**这一层。

## 4. 总体架构

```text
              +----------------------+
              |   Shared CaseSpec    |
              |  (JSON/YAML/TOML)    |
              +----------+-----------+
                         |
          +--------------+--------------+
          |                             |
          v                             v
+--------------------+       +----------------------+
|   Port Runner      |       |   Oracle Runner      |
| current project    |       |   libkrkr2.so        |
| native/web-side    |       | Android ARM64 side   |
+---------+----------+       +----------+-----------+
          |                             |
          v                             v
+--------------------+       +----------------------+
| Normalized Result  |       | Normalized Result    |
+---------+----------+       +----------+-----------+
          |                             |
          +--------------+--------------+
                         |
                         v
              +----------------------+
              | Differential Compare |
              +----------------------+
```

## 5. 为什么要用标准化结果，而不是直接比对象内存

`libkrkr2.so` 运行在 Android ARM64 环境；当前项目常见运行环境是 macOS/x86_64、WebAssembly 或其他宿主。即使两边逻辑完全一致，也不意味着以下内容可直接逐字节比较：

- C++ 对象布局
- 指针值
- vtable 地址
- STL/TJS 容器内部表示
- ABI 相关 padding

因此本方案的对比基准应优先落在：

- 返回值
- 输出 buffer
- 关键字段投影
- 关键 TJS 属性/方法观测
- 关键副作用轨迹
- 资源访问轨迹

只有对纯数据结构、显式 packed struct、原始像素缓冲区等，才推荐做字节级比较。

## 6. 核心组件设计

### 6.1 Shared CaseSpec

建议使用 JSON 作为第一版格式，原因：

- Python/Android/C++ 解析都方便
- 易于持久化
- 易于做 schema 校验
- 适合序列化输入输出

建议字段：

```json
{
  "id": "psb_rl_decode/basic_rgba_align4",
  "function": {
    "oracle_offset": "0x695DE8",
    "port_symbol": "motion::decodePsbRlForTest"
  },
  "category": "pure_buffer",
  "fixtures": {
    "input_file": "tests/test_files/emote/sample.psb"
  },
  "setup": {
    "seed": 742877301,
    "env": {
      "locale": "C"
    }
  },
  "args": {
    "width": 512,
    "height": 512,
    "palette": false,
    "compress": "RL"
  },
  "observe": [
    "return_value",
    "output_buffer_sha256",
    "output_buffer_prefix_64",
    "trace.resource_reads"
  ],
  "compare": {
    "float_epsilon": 1e-6,
    "ignore_pointer_values": true
  }
}
```

### 6.2 Port Runner

`Port Runner` 负责调用当前项目实现。建议不要直接把现有 Catch2 test case 当作 runner，而是新增一个可程序化调用的测试入口：

- 可执行文件形式：`port_runner --case case.json`
- 或库接口形式：由 Python driver 直接加载

职责：

1. 读取 `CaseSpec`
2. 构造测试输入
3. 调用当前项目目标函数
4. 生成 `NormalizedResult`
5. 输出 JSON

对于当前仓库，现有 `tests/unit-tests/plugins/motionplayer-dll.cpp` 已经有一批有价值的观测 helper：

- 固定 seed
- 读取 TJS 属性
- 读取数组元素
- 枚举字典成员

这些 helper 很适合抽成 `normalizer` 与 `probe` 基础设施，而不是继续散落在单个测试文件中。

### 6.3 Oracle Runner

`Oracle Runner` 负责调用 `libkrkr2.so`，这是整套方案最关键的部分。

#### 推荐实现路线

优先选择：

- **Android ARM64 真机/模拟器执行**

不优先选择：

- 在桌面宿主上完全依赖 Unicorn/Qiling/QEMU 模拟所有复杂函数

原因：

- `libkrkr2.so` 是 Android ARM64 共享库
- 复杂函数可能依赖 Android 运行环境、libc、线程、文件系统、TJS 对象和其他内部约束
- 纯模拟更适合作为“纯函数/小内核”的补充路线，而不是总方案

#### Oracle Runner 职责

1. 加载外部提供的 `libkrkr2.so`
2. 获取模块基址
3. 将 `oracle_offset` 解析为实际函数地址：`base + offset`
4. 根据函数族构造 `this`、参数、依赖对象
5. 执行函数
6. 收集观测值和轨迹
7. 生成 `NormalizedResult`

#### Oracle Runner 的调用方式

推荐以单独 Android 原生测试程序存在，例如：

```text
oracle_runner --lib /data/local/tmp/libkrkr2.so --case /data/local/tmp/case.json
```

测试驱动层通过 `adb push` / `adb shell` / `adb pull` 进行编排。

#### libkrkr2.so 不入仓库的处理

`libkrkr2.so` 不需要进仓库，但建议统一通过环境变量或配置文件传入路径：

- `KRKR2_ORACLE_LIB`
- `KRKR2_ORACLE_DEVICE_SERIAL`

## 7. NormalizedResult 设计

建议结果结构如下：

```json
{
  "case_id": "psb_rl_decode/basic_rgba_align4",
  "status": "ok",
  "return": {
    "type": "int",
    "value": 0
  },
  "outputs": {
    "buffer_sha256": "....",
    "buffer_size": 1048576,
    "buffer_prefix_hex": "00112233..."
  },
  "state": {
    "player.tickCount": 0,
    "player.maskMode": 0,
    "root.pos": [0.0, 0.0, 0.0]
  },
  "trace": {
    "prop_get": ["count", "x", "y"],
    "prop_set": ["visible"],
    "func_call": ["play", "draw"],
    "resource_reads": ["pixel", "pal"]
  },
  "meta": {
    "runner": "oracle",
    "duration_ms": 8
  }
}
```

### 7.1 对比优先级

建议按以下顺序比较：

1. 执行是否成功
2. 返回值是否一致
3. 输出 buffer 是否一致
4. 关键字段投影是否一致
5. 关键调用轨迹是否一致

### 7.2 浮点比较

默认优先精确比较；只有在确认 `libkrkr2.so` 与当前实现存在不可避免的浮点细节差异时，才针对个别字段启用 epsilon。

## 8. 函数分类与测试策略

不是所有函数都适合用同一种差分策略。建议按函数族分层推进。

### 8.1 A 类：纯函数/纯缓冲区函数

特征：

- 输入是 POD、buffer、标量
- 输出是 POD、buffer、标量
- 几乎不依赖 TJS/线程/渲染

适合：

- 第一阶段 PoC
- 字节级精确比较

典型候选：

- `sub_695DE8` PSB RL 解压路径

### 8.2 B 类：有限状态对象方法

特征：

- 依赖对象状态
- 依赖数个辅助结构
- 但可用 wrapper 手工搭建上下文

适合：

- 第二阶段扩展
- 关键字段投影 + 轨迹比较

### 8.3 C 类：重度依赖 TJS/NCB 的方法

特征：

- 大量 `iTJSDispatch2::PropGet/PropSet/FuncCall`
- 大量 variant/object 交互

适合：

- 假对象 + 轨迹 recorder
- 不建议做原始对象内存比较

### 8.4 D 类：渲染/线程/异步路径

特征：

- 涉及 DrawDevice、OpenGL、调度、线程
- 单函数执行不足以复刻真实上下文

适合：

- 高层 API 差分测试
- 场景回放
- 集成级 trace 对比

## 9. 测试驱动层设计

建议让**Python 作为顶层控制平面**，原因：

- 方便驱动本地可执行文件
- 方便驱动 `adb`
- 方便处理 JSON
- 可以天然表达“同一份 case，跑两个 backend”

推荐流程：

1. Python 读取 `CaseSpec`
2. 调用 `port_runner`
3. 调用 Android `oracle_runner`
4. 读取两边 `NormalizedResult`
5. 执行统一比较逻辑
6. 输出差异报告

可选形式：

- `pytest`
- 自定义 Python CLI

如果团队更偏好 C++，也可以保留 Catch2 用于本地单测，但**差分测试的顶层编排层**仍建议用 Python，避免把 `adb`、设备状态、case 文件分发逻辑硬塞进 C++ test runner。

## 10. Oracle Runner 的适配层原则

真正难的不是“调用函数”，而是“把函数调用前的世界搭对”。因此必须引入**按函数族编写的 adapter**。

每个 adapter 负责：

1. 定义 case 能表达哪些 setup
2. 搭建 `this` 和依赖对象
3. 安装 stub / fake / recorder
4. 调用函数
5. 导出标准化结果

结论：

- **可复用的是测试 spec、normalizer、comparator**
- **需要按函数族定制的是 adapter**

不建议追求“一个万能 adapter 适配所有内部函数”。

## 11. 如何验证测试体系本身是对的

把 `libkrkr2.so` 作为 oracle，能证明 expected 不是拍脑袋写出来的；但仍不足以证明 harness 绝对正确。因此需要额外机制：

### 11.1 三重校验

1. **反编译校验**
   用 IDA 反编译确认 setup、参数和观测点与真实代码一致。

2. **差分校验**
   `oracle` 与 `port` 输出一致。

3. **自洽校验**
   对 oracle 结果增加 invariants，例如：
   - 输出长度必须匹配宽高
   - 返回对象类型必须正确
   - 关键轨迹必须出现

### 11.2 防止“测试假通过”

重点防以下情况：

- 两边都被错误 setup 驱动，但恰好结果相同
- 只比返回值，漏掉关键副作用
- 对象图没有搭全，导致代码走了降级路径

因此建议每个 case 除了 compare 主结果外，还要至少比一个额外观测维度：

- 副作用轨迹
- 关键字段快照
- 资源读取顺序

## 12. 确定性控制

为了让两侧结果可比较，必须控制以下不稳定因素：

- 随机数 seed
- 当前时间
- locale
- 文件路径解析
- 浮点环境
- 全局静态状态
- 插件加载顺序

现有 motionplayer 测试已经有固定 seed 的实践，建议在差分测试中把它提升为统一约束。

## 13. 目录结构建议

建议新增如下结构：

```text
tests/
  differential/
    specs/
      psb_rl_decode/
        basic_rgba_align4.json
        basic_palette_rl.json
    python/
      run_diff_tests.py
      compare.py
      schema.py
    shared/
      README.md
    port_runner/
      CMakeLists.txt
      main.cpp
      adapters/
        psb_rl_decode.cpp
        player_query.cpp
    oracle_runner/
      README.md
      android/
        CMakeLists.txt
        main.cpp
        adapters/
          psb_rl_decode.cpp
          player_query.cpp
```

说明：

- `specs/` 保存与实现无关的 case 描述
- `python/` 保存顶层驱动与比较器
- `port_runner/` 保存当前项目侧执行器
- `oracle_runner/` 保存 Android 侧执行器

## 14. 与现有测试体系的关系

当前仓库的 `tests/` 已经具备本地单元测试基础，但其设计目标主要是：

- 本地开发期验证
- Catch2 驱动
- host-side 执行

差分测试不应粗暴替换现有单测，而应作为新增层：

1. 现有 Catch2 单测继续存在
2. 新增差分测试套件，面向“与 `libkrkr2.so` 对齐”
3. 两者共用部分 fixture 与 helper

## 15. 推进顺序

### 阶段 0：PoC

目标：

- 跑通最小闭环
- 证明 shared spec + dual runner 是可行的

建议只选一个 A 类函数：

- `sub_695DE8` 的一个确定子路径

输出：

- 1 个 `CaseSpec`
- 1 个 `port_adapter`
- 1 个 `oracle_adapter`
- 1 个 Python 对比脚本

### 阶段 1：纯函数批量化

目标：

- 扩展到一批 buffer/math/helper 函数
- 打磨 schema、normalizer、报告格式

### 阶段 2：有限状态对象方法

目标：

- 引入对象快照和轨迹 recorder
- 覆盖关键 Motion/Emote 路径

### 阶段 3：TJS/渲染高层路径

目标：

- 覆盖复杂 API 和关键场景
- 改用更高层、更接近真实运行链路的差分策略

## 16. 首批 PoC 选择建议

建议从下列标准挑选 PoC：

- 已有较完整反编译分析
- 输入输出简单
- 不依赖复杂对象图
- 可做字节级比较

首选：

- `sub_695DE8` PSB RL 解压路径

不建议第一批就做：

- `Player_updateLayers`
- `draw` 渲染路径
- 大量 TJS dispatch 的 NCB 方法

## 17. 风险与缓解

### 风险 1：Oracle Runner 搭环境成本过高

缓解：

- 第一阶段只做 A 类函数
- 优先减少 Android 依赖和对象图依赖

### 风险 2：比较维度过少，出现假对齐

缓解：

- 至少比两个维度：主结果 + 关键轨迹/快照

### 风险 3：CaseSpec 过于抽象，adapter 写起来反而更复杂

缓解：

- 第一版 schema 保守，围绕首批函数定制
- 稳定后再抽象

### 风险 4：把差分测试误当成架构对齐证明

缓解：

- 在流程文档中明确：差分测试是验证层，不是替代反编译层

## 18. 最终建议

结论如下：

1. 这套方案**可行**。
2. 最合理的落点是“同一份 case spec 驱动双 runner”，而不是“所有函数共用一个万能调用器”。
3. 顶层驱动建议使用 Python；本地和二进制侧分别实现 runner。
4. 第一阶段只做 A 类函数 PoC，优先验证方案闭环，不要一开始就挑战 TJS/渲染链。
5. 这套方案应当作为现有“反编译 -> 实现 -> 验证”流程的补强，而不是替代。

如果后续要进入实现阶段，建议下一份文档直接细化为：

- `CaseSpec` schema 定稿
- `NormalizedResult` schema 定稿
- `sub_695DE8` PoC 的 adapter 设计
- Python driver 的 CLI 约定
