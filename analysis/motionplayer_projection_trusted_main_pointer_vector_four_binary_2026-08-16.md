# MotionPlayer post-prepare projection 的可信 main 指针向量（四参考二进制，2026-08-16）

## 结论

四份参考二进制的 post-prepare camera/stereovision pass 只借用排序后的 main PreparedRenderItem 指针向量，并原地修改每个 item。空向量是合法的：begin 与 end 相等时整个 pass 不进入循环；但非空向量里的每个已存储指针都被当作有效 PreparedRenderItem，循环取出元素后立即访问对象字段，没有空指针比较或跳过分支。

端口此前在循环入口执行 “if (!entryPtr) continue”。这把损坏的内部指针序列静默转换成少变换一个 item，可能让同一 main list 后续的 command build/submit 继续消费部分已投影、部分未投影的数据。删除该分支恢复了参考实现的强前置条件：合法元素全部原地变换，null slot 在首次字段访问处自然失败。

## 函数与调用边界

| 平台 | post-prepare projection |
|---|---:|
| Android arm64 | 0x6D2644 |
| Android armv7 | 0x596EB0 |
| iOS arm64 | 0x100123038 |
| iOS armv7 | 0x1220F0 |

四端函数参数都只有 Player 指针与 main pointer-vector；aux vector 不在 ABI 中。函数不分配、不删除、AddRef 或 Release item，元素与 backing storage 都由 caller 保有。它只按 main 的当前排序顺序执行 camera-offset 与可选 stereovision 投影。

## 循环入口的四端证据

| 平台 | begin/end gate | 取出 item | 紧接着的使用 |
|---|---|---:|---|
| Android arm64 | 0x6D2678..0x6D2680 比较 begin/end | 0x6D268C “v8 = *v6” | 0x6D2698 从同一 item 读取内部 point-vector begin/end，并访问 corners |
| Android armv7 | 0x596EFC..0x596F04 比较 begin/end | 0x596F0E “v7 = *(_DWORD *)v6” | 0x596F14 起以 v7 为 base 访问 corners，随后访问内部 vectors |
| iOS arm64 | 0x10012306C..0x100123074 比较 begin/end | 0x100123088 “v10 = *v6” | 0x10012308C 直接形成 v10 + 136 的 corners 地址 |
| iOS armv7 | 0x122148..0x122150 比较 begin/end | 0x122168 “v11 = *v8” | 0x12216C 起形成 v11 + offset 并读取/写回 corners |

四种代码生成都只有容器为空的外层 gate。item pointer load 与首个字段访问之间没有：

- CBZ / CMP pointer,#0 / BEQ；
- 替代 item；
- 默认几何写入；
- 跳到下一槽位的恢复边。

## 数据流与故障边界

有效 item 进入循环后的顺序仍是：

1. 平移四个 corners；
2. 平移 command-composite mesh point vector；
3. meshType 等于 1 时平移 processed mesh point vector；
4. 无条件平移 paint box 与 viewport；
5. stereovision 开启且 sortKey 不等于 cameraZ 时，在 double 中投影选定几何、窄化回 float，并从投影结果重建 paint box；
6. 迭代器移动到下一只 borrowed pointer。

null slot 不会代表“这一帧暂时没有 item”。它会在 corners 或内部 vector 字段的首次访问处失败，而且失败发生在该槽位的任何几何写入之前；此前已经遍历的 item 保留其原地修改。参考实现没有事务回滚。

## 源码修正

cpp/plugins/motionplayer/PlayerRenderItems.cpp 的 applyPreparedRenderItemProjectionCore_guess 已：

- 删除循环入口的 “if (!entryPtr) continue”；
- 保留空 vector 的自然 begin/end 行为；
- 在 compiled-source 注释中只记录可信 borrowed pointer 语义，不写入任一平台绝对地址。

诊断 snapshot/trace 的只读格式化分支不属于本 pass 的 native production 语义，本轮没有用它们定义或放宽容器前置条件。

## IDB 更新

四份 recovery IDB 的 projection 函数头与各自 item load 指令均已追加 main pointer-vector 的借用、原地变换与无 null-slot admission 注释，并保存到 out/ida-recovery/ 对应平台目录。

## 验证

- ordinary Emscripten 单元测试翻译单元语法检查：通过，只有既有 _tss literal-operator 弃用警告；
- KRKR2_WASMTIME_HEADLESS=1 同一翻译单元语法检查：通过，同一既有警告；
- Web Debug motionplayer archive：2/2 通过；
- Wasmtime Headless Debug motionplayer archive：2/2 通过；
- 两套 archive 只出现既有 _tss literal-operator 弃用警告；
- Web Debug 完整目标：1/1 链接通过；只出现既有 pthread/memory-growth、JSPI 与 Emscripten JS library 警告。
