# MotionPlayer clamp / mirror 数据流四端审计（2026-08-11）

## 结论

本纵切面重新从 `reference/binaries/` 的四份当前参考定位，不继承旧
`libkrkr2.so` 地址。旧源码注释中的地址在当前 Android ARM64 IDB 中已经落到
完全不同的函数：

- 旧 `0x66EE5C` 位于当前 `sub_66EE30` 内；
- 旧 `0x66F364` 位于当前 `sub_66F1D0` 内；
- 旧 `0x67C560` / `0x67C6B0` 都位于当前 `sub_67C4AC` 内；
- 旧 `0x67C8A8` 位于当前 `EmoteEngine_dtor_guess` 内。

因此这些旧地址既不能继续作为源码名称的一部分，也不能作为当前实现正确性的
证据。四端重新定位后，clamp/mirror 仍然由五个共同源码角色组成：两个 metadata
builder、活动 timeline 贡献累加器、mirror 分类/缓存函数和 clamp 绑定器。

## 四端映射

| 源码角色 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_buildClampControl_guess` | `0x66C23C` | `0x55892C` | `0x1001AB0A8` | `0x1AA760` |
| `EmoteEngine_buildMirrorControl_guess` | `0x66C744` | `0x558C24` | `0x1001AB4F4` | `0x1AABCC` |
| `EmoteEngine_accumulateTimelineContribution_guess` | `0x679940` | `0x55F860` | `0x1001B35F4` | `0x1B31B0` |
| `EmoteEngine_shouldMirrorLabel_guess` | `0x679A90` | `0x55F8FC` | `0x1001B37C4` | `0x1B3394` |
| `EmoteEngine_applyClampControls_guess` | `0x679C88` | `0x55F9A0` | `0x1001B3C9C` | `0x1B38A0` |

进度核心与 post-loop 外提差异：

| 参考 | Engine progress core | HM7 bind + clamp 外提块 |
|---|---:|---:|
| Android ARM64 | `0x67A3F8` | 普通 HM7 bind loop 内联；直接调用三个 helper |
| Android ARMv7 | `0x55FEF0` | `0x55FC58` |
| iOS ARM64 | `0x1001B4304` | `0x1001B3F64` |
| iOS ARMv7 | `0x1B3E10` | `0x1B3B58` |

后三端把“遍历 HM7、累加、mirror、普通 bind、最后 clamp”整体外提成一个优化器
helper；Android ARM64 把外围 HM7 遍历留在 progress core。它们不代表不同的源级
对象或额外公共 API。

## clamp builder 与自然布局

2026-08-13 又独立闭合了 deque #7 的 default-emplace、四端 block ABI、字符串逆序
析构和异常后的部分初始化状态，详见
`analysis/motionplayer_clamp_entry_container_four_binary_2026-08-13.md`。本节保留
业务字段与 builder 顺序摘要；容器内部实现以新专题为准。

四端共同伪代码：

```cpp
for (int i = 0; i < count(clampControl); ++i) {
    value = clampControl[i];
    if (!getBool(value, "enabled"))
        continue;
    clampDeque.emplace_back();
    back.type = getInt(value, "type");
    back.varLr = getString(value, "var_lr");
    back.varUd = getString(value, "var_ud");
    back.minValue = getDouble(value, "min");
    back.maxValue = getDouble(value, "max");
}
```

没有 builder-local clear，没有 disabled 占位节点，也不注册 HM6。新节点的数值和
两个字符串槽先是零/空状态，然后按上述源顺序覆盖。追加完成以后若任一 getter 或
字符串赋值抛出，四端 landing pad 都只清理局部 temporary，不 pop 新节点；因此零或
部分填充的 entry 会留在 Engine deque 中。

共同字段序列为：

```cpp
struct ClampEntry {
    int type;
    double minValue;
    double maxValue;
    ttstr varLr;
    ttstr varUd;
};
```

自然 ABI 结果：

| 参考 | entry 大小 | `std::deque` 块大小 / entry 数 |
|---|---:|---:|
| Android ARM64（libstdc++） | 40 | 480 / 12 |
| Android ARMv7（libstdc++） | 32 | 512 / 16 |
| iOS ARM64（libc++） | 40 | 4080 / 102 |
| iOS ARMv7（libc++） | 28 | 4088 / 146 |

Android ARM32 与 iOS ARM32 的差异来自 `double` 对齐规则，不是条件编译字段；
跨平台源码应保留自然成员而不能硬编码某一端的 40 字节布局或 deque 块公式。

## mirror builder、匹配与缓存

builder 只读取 `mirrorControl.variableMatchList`，把每一项直接转换成 `ttstr` 并
追加到 vector。四端共同保留：

- 原始顺序；
- 重复项；
- 空字符串；
- 已有 vector 内容。

它没有 `enabled` gate、过滤、去重或 builder-local clear。清理由更外层 metadata
reset / Engine 生命周期负责。

mirror 查询的共同顺序是：

1. `_mirrorChanged == false` 立即返回 false，且不会查询两个缓存；
2. positive set 命中返回 true；
3. negative set 命中返回 false；
4. 按 vector 顺序扫描 pattern，仅当 `label.IndexOf(pattern, 0) >= 1` 时记录到
   positive set 并返回 true；
5. 全部未命中时记录到 negative set 并返回 false。

`>= 1` 是真实边界，不是 `>= 0`，而且比较的是 `IndexOf` 返回的第一个 occurrence。
因此 pattern 位于 label 首字符时被当作 miss；即使同一 pattern 后面再次出现，首个
返回值仍为 0，结果仍是 miss。只有首个 occurrence 位于第二字符或更后才 match。
缓存优先于后续 pattern 内容变化；只有更外层 reset 清理缓存。全局
`_mirrorChanged` gate 又优先于缓存，所以 gate 关闭时即使 positive set 已命中仍返回
false；重新打开 gate 后旧缓存继续生效。

四端读取 `variableMatchList` 还各自传入一个独立进程级 hint：Android arm64
`0x1AB4F78`、Android armv7 `0x1111510`、iOS arm64 `0x101B6A028`、iOS armv7
`0x187DA48`。该 slot 只属于 mirror builder，不与顶层 metadata 的 `mirror` hint
共用；本地实现现已恢复这一身份。

四端内部容器共同是两个 `unordered_set<ttstr>`，但 ABI header/node 细节随
libstdc++ / libc++ 与指针宽度变化；本地使用带共同 ttstr hash/equality 的 typed set，
不复制宿主 bucket 布局。

## timeline 贡献累加

共同数据流：

```cpp
for (timelineLabel : activeTimelineLabels) {
    state = compoundHM3.at(timelineLabel);
    if (!(state.flags & 2))
        continue;
    for (track : state.timelineData->variableList) {
        if (!track.instantVariable && !track.frameList.empty() &&
            track.label == label) {
            value += float(track.output * state.blendWeight);
        }
    }
}
```

边界细节：

- HM3 使用 bounds-checked `at`；active vector 中不存在的 label 会失败且不插入默认
  state。2026-08-15 的 fresh 四端复核纠正了本页旧版“下标物化”的错误；
- 必须设置 flags bit 1（数值 `2`）；
- instant track 或空 frame deque 被跳过；
- 乘积先收窄为 `float`，再加进 `double` 目标；
- 四端调用者均不消费退出寄存器残值，真实源返回是 `void`。

checked lookup、逐 contribution 舍入、重复项、部分提交和 caller optimizer 拓扑详见
`analysis/motionplayer_timeline_contribution_checked_lookup_rounding_callers_four_binary_2026-08-15.md`。

嵌套 track entry 为 56 字节（64 位）/ 28 字节（32 位）。遍历块再次显示 STL ABI
差异：Android 为 504 字节块（9 / 18 项），iOS 为 4088 字节块（73 / 146 项）。

## clamp 运行时数据流与边界

每个 entry 的共同流程：

1. 在 Engine HM7 中分别查 `varLr`、`varUd`；miss 从 `0.0` 开始。
2. 对两个局部值各执行一次 timeline 贡献累加。
3. 以同一个 `[min,max]` 归一化到 `[-1,1]`；没有 `max == min` guard。
4. 仅当两个归一化分量都不等于零时进入二维 remap。ARM32 反编译一度只显示
   一个比较，但原始 `VCMP` + `ITT NE` 明确证明它也是 `x != 0 && y != 0`。
5. `type == 0` 执行 squircle remap；`type == 1` 仅在向量长度大于 `1.0` 时以
   `atan2` / `cos` / `sin` 投影到单位圆；其他 type 不 remap。
6. 反归一化回 `[min,max]`。
7. 只对 LR label 执行 mirror 判定/可能取负；UD 永不镜像。
8. 两者都以 mode `0` 送入 `Player_bindParameterValue_guess`。

零轴保持原分量、不进入除法 remap；`max == min` 则按硬件浮点继续传播 NaN/Inf，
原版没有防御性修正。

在 progress 中，普通 HM7 bind loop 先把 timeline 贡献原地累加回 HM7，再执行 clamp；
clamp 随后重新读取 Engine HM7 并再次调用贡献累加器。这意味着一个同时存在于 HM7、
clamp entry 和活动 timeline track 的 key 会在 clamp 路径观察到第二次贡献累加。
这是真实调用拓扑，不能把 clamp 改为读取刚写入的 Player map，也不能把第二次累加
“去重”。clamp 完成后才进入 embedded Player progress。

## IDB 改进

四个 IDB 已完成：

- 重新定位并统一命名三个运行时 helper；
- 累加器应用 `void` 原型，mirror helper 应用 `bool` 原型，clamp binder 应用
  `void` 原型；
- 修复 clamp builder 中被 IDA 截断为 `"t"` / `"v"` 的 UTF-16 `type`、
  `var_ud` 数据边界，以及 Android ARM64 的 `max` 边界；
- 2026-08-13 补命名 default-emplace、range destruction 和 iOS builder EH cleanup，
  并记录四端 block capacity、`varUd -> varLr` 逆析构与无 post-emplace rollback；
- 强制刷新 builder/helper、progress core 和三个优化器外提 wrapper 的伪代码，
  确认新 helper 名出现在正确调用点；
- 为 builder、三个运行时 helper、progress core/wrapper 写入四端语义与边界注释，
  并将四个 IDB 原位保存成功。

## 源码与测试

源码变更：

- 删除函数名中的旧地址后缀，统一为
  `accumulateTimelineContribution_guess`、`shouldMirrorLabel_guess`、
  `applyClampControls_guess`；
- 用四端结论替换本纵切面旧 `libkrkr2.so` 地址注释；
- 保留 typed vector/set/deque/HM3/HM7 所有权与自然 ABI 边界。

新增测试覆盖：

- mirror 首字符 miss、第二字符 match、正负缓存和全局 gate 优先级；
- clamp 单轴不 remap、type 1 单位圆投影与零范围 NaN；
- clamp builder 的 enabled gate 与字段填充；
- mirror builder 对重复项和空字符串的原样保留。
- timeline contribution 的 checked miss不插入、重复 active label/track逐项累加、
  flags-off null-data跳过，以及较晚 miss 保留较早 double累加前缀。

验证结果：

- Web `motionplayer` 静态库目标通过；
- Wasmtime `motionplayer` 静态库目标通过；
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 的完整 Emscripten 语法检查通过，
  仅有仓库既有的 `_tss` literal-operator 警告；
- Web 完整 `index.html` 链接通过；
- Wasmtime 完整 `krkr2_wasmtime_guest.wasm` 链接及 exnref 转换通过；
- `git diff --check` 通过，仅报告工作树中既有的 LF/CRLF 转换提示；
- 本轮环境没有可直接运行的 Catch2 motionplayer 测试目标，因此这里只声明编译、
  链接和语法检查结果，不声明 Catch2 运行时通过。
