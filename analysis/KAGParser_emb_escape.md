# KAGParser `emb escape` 复原

## 结论

千恋万花的启动脚本使用 `[emb escape=false exp="createCallConfigFile('custom.ks')"]` 生成可执行的 KAG 标签。当前 Web 端在生成字符串时无条件把 `[` 转成 `[[`，所以生成的 `[call ...]` 被当作普通文本，`custom.ks` 中的 `first.logo` hook 不会注册，启动画面表现为黑屏，但右键菜单仍可用。

## Android 证据

目标是 APK 内 `lib/arm64-v8a/libkrkr2.so` 的 `sub_561F3C @ 0x561F3C`，通过 IDA MCP `decompile` 获取伪代码。函数中把 `emb` 注册为特殊标签值 `7`；活动的非宏 `emb` 分支执行 `exp`，随后读取 `escape` 属性。该属性缺失时使用 `true`，存在时转换为布尔值。输出长度统计和输出复制循环都只在 `escape` 为真时对 `[` 加倍。

## 本地对照

`cpp/core/base/KAGParser.cpp` 的 `tTJSNI_KAGParser::_GetNextTag()` 已有相同的 `emb` 表达式执行、缓冲区重建和行尾处理流程，但原先：

1. 没有读取 `escape`；
2. 长度统计无条件为每个 `[` 增加一个字符；
3. 输出复制无条件写入两个 `[`。

本次只补齐 `escape=true` 默认值、属性读取，以及上述两个条件门控。
