# MotionPlayer 已删除 `PlayerRuntime` 的幽灵声明清理（2026-08-16）

## 结论

从 `NodeTree.h`、`Player.h`、`SourceCache.h` 删除三个
`detail::PlayerRuntime` forward declaration，并修正 `RuntimeSupport.h` 仍声称其他 header
“可能保留 forward declaration”的过时说明。

精确 token 检索表明，清理前 `PlayerRuntime` 在 production/tests 的全部出现只有这三个
声明；没有 definition、对象、指针、引用、friend、template argument、`sizeof` 或 cast。
所以它既不是参考插件的对象生命周期节点，也不是本地 Web sidecar 的 owner，只是较早把
runtime fields 从中间 payload 迁移到 `motion::Player` 后遗留的头文件名字。

删除不会改变预处理后的类布局、符号、控制流或销毁顺序。所有恢复出的 node/container/
render 状态继续由 `motion::Player` 及其具体成员直接拥有。
