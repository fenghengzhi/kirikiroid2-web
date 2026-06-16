# 链接后同步 shell 预分配内存大小到 emscripten 实际烘焙的 wasm 最小内存。
#
# 背景：platforms/web/shell.html 为兼容 iOS Safari（JSC 按 maximum 实预留地址空间）
# 预创建 WebAssembly.Memory 交给 glue（initMemory 第一分支 Module.wasmMemory）。该
# Memory 的 initial 必须 >= wasm 模块声明的 min，否则导入校验失败：
#   LinkError: memory import has N pages which is smaller than the declared initial of M
#
# ASan 的影子内存（约 INITIAL_MEMORY/8）+ redzone 会把 wasm 声明的 min 顶到远高于
# 链接参数 -s INITIAL_MEMORY 的值（实测 link 1GB → min ≈ 1.29GB），且随代码体积变化，
# 配置期无法预测。emscripten 把最终值烘焙进 index.js：
#   var INITIAL_MEMORY = Module["INITIAL_MEMORY"] || <N>;
# 这里读出该权威 <N>，写回 index.html 的 prealloc，使 shell 提供的 initial 恒等于
# wasm 声明的 min（over-allocation 也合法，但精确相等最省内存）。
#
# 入参（-D 传入）：JS=index.js 路径，HTML=index.html 路径
file(READ "${JS}" _js)
# 注意：-O3 release 的 index.js 经 JS minify，`||` 两侧空格会被去掉
# （debug 为 `Module["INITIAL_MEMORY"] || N`，release 为 `Module["INITIAL_MEMORY"]||N`）；
# 引号风格也可能因压缩在单/双引号间变化。故对 `[` 后引号、`]` 与 `||`、`||` 与数字
# 之间的空白都放宽匹配，避免 release 构建在此 FATAL_ERROR。
string(REGEX MATCH "Module\\[.INITIAL_MEMORY.\\][ \t]*\\|\\|[ \t]*([0-9]+)" _m "${_js}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "sync_prealloc_memory: 在 ${JS} 中未找到烘焙的 INITIAL_MEMORY 默认值")
endif()
set(_n "${CMAKE_MATCH_1}")
file(READ "${HTML}" _html)
string(REGEX REPLACE
    "var INITIAL_PAGES = [0-9]+ / 65536"
    "var INITIAL_PAGES = ${_n} / 65536"
    _html "${_html}")
file(WRITE "${HTML}" "${_html}")
message(STATUS "sync_prealloc_memory: shell prealloc INITIAL_PAGES 同步为 ${_n} 字节")
