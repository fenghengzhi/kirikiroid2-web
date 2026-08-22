# motionplayer `pass` / `modified` 四参考二进制审计（2026-08-11）

## 结论

旧 `libkrkr2.so` 注释把 D3D 表中的两个相邻成员错误解释为：

- `pass()` -> `addPlayCallback()`，只设置一个 Engine 旁路 bool；
- `modified` -> `getPlayCallback()`，读取同一个旁路 bool。

当前四个参考二进制共同否定这一解释：

- `pass` 是无参数方法。D3D 壳只调整 receiver，随后进入 Engine 的活动 timeline
  flush 核心；它既不推进普通 frame dt，也不写 callback bool。
- `modified` 是只读属性。getter 沿 D3D 壳 -> EmoteObject -> Engine -> Player ->
  root node，返回根节点 delta block 的 dirty byte。
- 二进制 Engine 对象中不存在端口 `_modified` / `_playCallback` 这两个尾部影子
  字段；此前各 mutator 对 `_modified=true` 的写入没有原版数据流消费者。

## 四端地址映射

### 注册点、包装器与主体

| 目标 | `pass` 名称取址 | D3D `pass` 包装 | Engine flush 核心 | `modified` 名称取址 | 根 dirty getter |
|---|---:|---:|---:|---:|---:|
| Android arm64 | `0x52FAF8` | `0x530E30` | `0x67A100` | `0x52FBFC` | `0x530E54` |
| Android armv7 | `0x4944AC` | `0x495016` | `0x55FCC4` | `0x4944D0` | `0x495036` |
| iOS arm64 | `0x100232950` | `0x100233464` | `0x1001B3FE4` | `0x100232990` | `0x100233488` |
| iOS armv7 | `0x2315A8` | `0x2321A6` | `0x1B3BBC` | `0x2315E4` | `0x2321C6` |

### Engine 核心调用的三个关键 helper

| 目标 | HM3 `at` | blend/auto-stop enqueue | timeline frame -> variable enqueue |
|---|---:|---:|---:|
| Android arm64 | `0x689514` | `0x67098C` | `0x66E608` |
| Android armv7 | `0x569DBC` | `0x55ACDC` | `0x559D84` |
| iOS arm64 | `0x1001B374C` | `0x1001AE178` | `0x1001ACDBC` |
| iOS armv7 | `0x1B32A8` | `0x1AD918` | `0x1AC5F4` |

IDB 中分别统一命名为：

- `D3DEmotePlayer_passTimelines_guess`
- `EmoteEngine_passTimelines_guess`
- `D3DEmotePlayer_getRootModified_guess`
- `EmoteHM3Map_at_guess`
- `EmoteEngine_setTimelineBlendController_guess`
- `EmoteEngine_setVariable_guess`

以上函数均已类型化，四份 IDB 已保存。

## 字符串证据

UTF-16LE byte 搜索结果：

| 目标 | `pass` | `modified` | `playCallback` | `addPlayCallback` |
|---|---:|---:|---:|---:|
| Android arm64 | `0x14BEE62` | `0x14BEE7E` | 无 | 无 |
| Android armv7 | `0xD76C3E` | `0xD76C5A` | 无 | 无 |
| iOS arm64（D3D 表） | `0x1019701E0` | `0x1019701FC` | 无 | 无 |
| iOS armv7（D3D 表） | `0x176258C` | `0x17625A8` | 无 | 无 |

Android 两个 NCB 表共享常量池字符串；iOS 的 EmotePlayer / D3D 表各自有一份
`pass` 或 `modified` 字面量。四端均找不到 `playCallback` 或
`addPlayCallback` UTF-16 名称。

## D3D 包装层

64 位共同结构：

```cpp
void D3DEmotePlayer_passTimelines(D3DShell *self) {
    EmoteEngine_passTimelines((*((EmoteObject **)(self + 24)))->engine);
}
```

32 位使用 `self+16` 和 EmoteObject 内的 `+4` Engine 槽；语义相同。包装器没有
参数默认值、返回值、状态写入或 null 检查。

## Engine flush 核心

四端归一后的伪代码：

```cpp
size_t i = 0;
while (i < activeTimelineLabels.size()) {
    const ttstr &label = activeTimelineLabels[i];
    TimelineState &state = timelineMap.at(label);

    if (state.loopBegin >= 0.0) {
        ++i;
        continue;
    }

    if (state.flags & 2) {
        if (state.flags & 4) {
            ++i;
            continue;
        }
        setTimelineBlendController(label, true, 0.0f, 20.0f, 0.0f);
        state.flags |= 4;
    }

    for (size_t trackIndex = 0;
         trackIndex < state.timelineData->variableList.size();
         ++trackIndex) {
        Track &track = state.timelineData->variableList[trackIndex];
        if (!(state.flags & 2) || track.instantVariable) {
            int32_t frameIndex = wrap32(state.frameCursors[trackIndex] + 1);
            while (size_t(sign_extend(frameIndex)) < track.frameList.size()) {
                const Frame &frame = track.frameList[frameIndex++];
                if (!frame.typeZero) {
                    setVariable(
                        track, frame.value, frame.time, frame.easingWeight);
                }
            }
        }
    }

    if (state.flags & 4)
        ++i;
    else
        activeTimelineLabels.erase(activeTimelineLabels.begin() + i);
}
```

边界行为：

- `loopBegin >= 0` 的循环 timeline 完全不处理。
- flags bit 2 未置位：从每条 track 的 `frameCursor+1` 扫到尾部，忽略
  `typeZero` frame，把其余 frame 的 `(value,time,easingWeight)` 依次送入
  Engine variable dispatch；最后从活动 vector 移除该 timeline。
- flags bit 2 已置位、bit 4 未置位：先把 blend controller enqueue 为
  `(autoStop=true,target=0,duration=20,easingWeight=0)` 并置 bit 4；只 flush
  `instantVariable` track；timeline 保留在活动 vector 中等待淡出完成。
- flags bit 2 和 bit 4 都已置位：整项跳过，重复 `pass()` 不会重复 enqueue。
- HM3 访问是 `unordered_map::at`，不是 `operator[]`。活动 vector 含缺失 key
  时，Android 路径进入 `_Map_base::at` 异常，iOS 明确构造
  `std::out_of_range("unordered_map::at: key not found")`；端口保留 `.at()`。
- 核心依赖 `timelineData`、frame cursor 数量和 track 数量已经由 play/init 生命周期
  建立；没有防御性 null/长度检查。
- 2026-08-15 fresh disasm补齐 frame cursor宽度：两份arm64都以32位 `ADD Wn`
  计算 cursor+1/循环自增，再以 `SXTW` 扩到size_t比较；两份armv7同样保留32位
  环绕。不能在加法前把负cursor直接提升成size_t。`-1` 从frame 0开始，`-2`
  在普通可分配frame列表上跳过全部帧。
- frame enqueue helper 会设置 Engine dirty，并按 HM6 type 分派到 eye、eyebrow、
  mouth、transition、selector 等控制器；HM6 miss 则写 HM7。这里没有独立
  callback 容器。

## `modified` 根节点链

Android 两端可以直接看到 Player 的 root 指针：

| 目标 | Engine -> Player | Player -> root | root dirty |
|---|---:|---:|---:|
| Android arm64 | `+1064` | `+200` | `+1584` |
| Android armv7 | `+532` | `+160` | `+1344` |

iOS Player 用 libc++ 分段容器定位逻辑 root，因此反编译表现为 map pointer、start
index、block stride 的组合，但最终仍读同一逻辑字段：

| 目标 | Engine -> Player | root block stride | root dirty |
|---|---:|---:|---:|
| iOS arm64 | `+696` | `2648` | `+1600` |
| iOS armv7 | `+348` | `2228` | `+1312` |

这与四端 `modifyRoot()` 写入的逻辑字段一致。getter 没有空 root 检查；端口的
`Player::getRootModified_guess()` 因此也直接索引根节点，不加保护分支。

## 本地修正

- 新增 `EmoteEngine::passTimelines_guess()`，保留 `at`、20 帧淡出、instant-only
  过滤、bit 4 幂等门和 erase-without-increment 顺序。
- D3D NCB `pass()` 改绑上述零参数 timeline flush。2026-08-16 后续 fresh
  registrar/target/xref 复核确认本地 `pass(double)` 只是未注册、零 production caller
  的测试 convenience，现已删除；需要推进 frame 的测试直接调用 `progress(double)`。
- `modified` 改绑 `getModified()` -> `Player::getRootModified_guess()`。
- 删除端口自造的 `_modified` / `_playCallback` Engine 尾字段、setter/getter 和所有
  mutator 旁路写入。
- 新增测试覆盖普通 timeline flush、parallel/instant-only 过滤、20 帧 fade、bit 4
  重入门、loop 跳过、活动 vector 移除、HM3 缺失 key 的 `at` 异常，以及32位
  signed frame cursor的 `-1/-2` widening边界。

## 验证状态

- Web Debug 全量构建通过（31 个目标）；随后增量构建返回
  `ninja: no work to do`。
- Wasmtime Headless Debug 全量构建通过（59 个目标）；随后增量构建同样返回
  `ninja: no work to do`。
- 复用 Web 编译参数并补入 Catch2 头目录，对完整
  `tests/unit-tests/plugins/motionplayer-dll.cpp` 执行 Emscripten
  `-fsyntax-only` 通过。除项目已有的 `_tss` 字面量声明弃用警告外无诊断；本轮新增
  `pass` / `modified` 用例已进入该翻译单元检查。检查同时发现并修正了旧测试仍调用
  已删除 `setModule(string)` API 的前序 D3D module 生命周期遗留问题，改由
  `D3DEmotePlayer::load` 建立主 EmoteObject。
- `git diff --check` 通过；仅报告工作区原有文件的 LF/CRLF 转换提醒。
- Windows 原生 Catch2 尚未运行：`vcpkg` 在项目 CMake 配置之前构建外部
  `cocos2dx:x64-windows` 失败，错误位于其 `external/unzip/unzip.cpp`，MSVC
  报 `C2491`（`unzSeek64`、`unzEndOfFile` 的 `dllimport` 定义不允许）。当前没有
  生成 `build.ninja`。该阻塞发生在外部 overlay port，而不是 motionplayer
  源码或本轮测试的编译阶段；未修改全局 vcpkg/overlay 来规避它。
