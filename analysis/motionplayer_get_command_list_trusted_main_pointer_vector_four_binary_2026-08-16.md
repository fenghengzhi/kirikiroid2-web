# MotionPlayer getCommandList 两遍序列化的可信 main 指针向量（四参考二进制，2026-08-16）

## 结论

四份参考二进制的 Player.getCommandList 在 stable-sort 后对同一个 main PreparedRenderItem pointer-vector 执行两遍：

1. 第一遍无条件为每个 item 创建 fresh command Dictionary，并替换 item 上持久保存的 command Variant；
2. 第二遍重新从 main vector 加载每个 item，才按两个 raw flag 与 raw opacity 过滤结果 emission，补写 stencilChain，并把持久 command Variant 的别名加入 fresh result Array。

两遍都把 main vector 视为紧密、可信的 borrowed-pointer sequence。第一遍的 pointer load 后直接以 item 字段地址构造命令；第二遍的 pointer load 后直接读取 filter bytes。任何平台都没有 null slot test 或 continue recovery。

端口此前第一遍以 “if (item)” 包裹 buildCommand，第二遍又以 “if (!itemPtr) continue” 跳过。这会让含 null 的损坏 vector 仍返回部分结果，并且改变第一遍“全部物化后才开始第二遍”的对象生命周期与异常边界。本轮同时删除两处容错。

## 函数映射

| 平台 | Player.getCommandList 序列化 body |
|---|---:|
| Android arm64 | 0x6D0E2C；与短 EmotePlayer wrapper 使用 non-contiguous tail merge |
| Android armv7 | 0x595FF0 |
| iOS arm64 | 0x100121EB0 |
| iOS armv7 | 0x120CF8 |

Android arm64 的 IDA function/chunk 表达仍保留原始 non-contiguous 拓扑；本轮只在共享 Player body 上记录容器语义，没有为了名称整齐重切函数。

## 第一遍：全部 command materialization

| 平台 | item pointer load | load 后的首个 item 使用 |
|---|---:|---|
| Android arm64 | 0x6D0F64 “LDR X23,[X24]” | 创建 Dictionary 后以 X23 + 0xF8 访问 command key，并继续写完整命令 |
| Android armv7 | 0x5960E6 “LDR.W R8,[R6]” | 创建 Dictionary 后形成 R8 + 0xE0 的 command-key/field 地址 |
| iOS arm64 | 0x100121F94 “LDR X20,[X27]” | 创建 Dictionary 后形成 X20 + 0xF8 的 field 地址 |
| iOS armv7 | 0x120E0A “LDR R0,[R1]” | 保存 item pointer，创建 Dictionary 后以该 pointer 访问持久字段 |

pointer load 与 command construction 之间可能夹有 fresh Dictionary allocation，因为 Dictionary 本身先创建；但没有任何以 item pointer 为条件的分支。分配成功后必然使用该 pointer 写属性，null 会在第一项 item-field 地址/读取处失败。

这一位置很重要：如果第 N 个槽位损坏，前 N-1 个持久 item 已各自替换了 command Variant；第 N 个 fresh Dictionary 也可能已经创建。参考实现随后失败，不进入第二遍，也不创建/返回一个看似成功的部分结果。

## 第二遍：filter、stencilChain 与 result emission

| 平台 | item pointer load | 紧邻 filter fields |
|---|---:|---|
| Android arm64 | 0x6D1BDC “LDR X21,[X25]” | 0x6D1BE0 读 +0x11；0x6D1BE8 读 +0x10；随后 raw opacity |
| Android armv7 | 0x5968AA “LDR R6,[R4]” | 0x5968AC 读 +9；0x5968B0 读 +8；随后 raw opacity |
| iOS arm64 | 0x1001226B8 “LDR X26,[X28]” | 0x1001226BC 读 +0x11；0x1001226C4 读 +0x10；随后 raw opacity |
| iOS armv7 | 0x1215D0 “LDR R3,[R1]” | 0x1215D2 读 +9；0x1215DA 读 +8；0x1215E2 读 +0xD0 opacity |

所有平台都是 pointer load 后立即 byte-field load；无 CBZ/CMP pointer。两个 flag 或零 opacity 命中时跳过的是一个已在第一遍成功物化 command Variant 的有效 item。

## 生命周期与部分副作用

合法调用的所有权链不变：

- MotionNode 长期拥有 PreparedRenderItem；
- getCommandList 栈上 main/aux vector 只借用 item pointer；
- 第一遍的 fresh Dictionary 覆盖 item.commandVariant，旧 Variant 按引用计数释放；
- 第二遍生成 fresh result Array；Array、stencil link 与 mesh Array 只持有相应 command Variant 的引用计数别名；
- 返回后 main/aux vector 销毁但不释放 item。

损坏 null slot 的边界分成两种可观察时机：

- 第一遍遇到 null：此前 item 的 command Variant 已替换；本 item 的 Dictionary allocation 可能已发生；不会进入 result-array 第二遍；
- 理论上若 vector 在两遍之间被外部破坏，第二遍遇到 null：第一遍已经完成所有 command replacement，fresh result Array 已创建，已输出的前缀仍保留其 side effects；函数在 flag load 处失败。

正常实现没有并发或 callback 能合法修改 main vector，这只是准确描述 unchecked internal-state failure；端口不应把它重新定义成稀疏容器协议。

## 源码修正

cpp/plugins/motionplayer/PlayerLayerQuery.cpp 已：

- 第一遍改为对 mainList 每个 pointer 无条件调用 buildCommand；
- 第二遍删除 itemPtr null/continue；
- 保留“全部物化 → fresh result Array → 第二遍过滤”的原有顺序；
- 保留 childItems 与 parentItem 链的既有 trusted-pointer 行为；
- compiled-source 注释只记录 dense borrowed-pointer contract，不写四端绝对地址。

## IDB 更新

四份 recovery IDB 的 Player.getCommandList body、第一遍 item load 与第二遍 item load 都已追加“两遍都不允许 null slot”的函数/行注释，并保存到 out/ida-recovery/ 对应平台目录。

## 验证

- ordinary Emscripten 单元测试翻译单元语法检查：通过，只有既有 _tss literal-operator 弃用警告；
- KRKR2_WASMTIME_HEADLESS=1 同一翻译单元语法检查：通过，同一既有警告；
- Web Debug motionplayer archive：2/2 通过；
- Wasmtime Headless Debug motionplayer archive：2/2 通过；
- 两套 archive 只出现既有 _tss literal-operator 弃用警告；
- Web Debug 完整目标：1/1 链接通过；只出现既有 pthread/memory-growth、JSPI 与 Emscripten JS library 警告。
