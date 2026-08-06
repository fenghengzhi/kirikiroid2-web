# 生成构建期注入的配置 build-config.js（前端静态文件之外的那一份）。
#
# 取代原 sync_prealloc_memory.cmake：它把权威的 INITIAL_MEMORY 正则改写回
# emscripten 生成的 index.html，因而前端与链接产物耦合在同一个文件里。现在
# index.html 是手写的静态文件（只复制不改写），构建期的所有可变量集中落到这
# 一个生成文件中，页面用 js/config.js 兜底默认值，源码树直开也能跑。
#
# 背景（INITIAL_MEMORY）：
# platforms/web/webui/public/js/engine/memory.js 为兼容 iOS Safari（JSC 按 maximum
# 实预留地址空间）预创建 WebAssembly.Memory 交给 glue（initMemory 第一分支
# Module.wasmMemory）。该 Memory 的 initial 必须 >= wasm 模块声明的 min，否则
# 导入校验失败：
#   LinkError: memory import has N pages which is smaller than the declared initial of M
#
# ASan 的影子内存（约 INITIAL_MEMORY/8）+ redzone 会把 wasm 声明的 min 顶到远高于
# 链接参数 -s INITIAL_MEMORY 的值（实测 link 1GB → min ≈ 1.29GB），且随代码体积变化，
# 配置期无法预测。emscripten 把最终值烘焙进 index.js：
#   var INITIAL_MEMORY = Module["INITIAL_MEMORY"] || <N>;
# 这里读出该权威 <N> 写进 build-config.js，使 shell 提供的 initial 恒等于
# wasm 声明的 min（over-allocation 也合法，但精确相等最省内存）。
#
# 入参（-D 传入）：
#   JS               index.js 路径（读取烘焙的 INITIAL_MEMORY）
#   OUT              输出的 build-config.js 路径
#   BUILD_VERSION    构建时间戳，驱动 service worker 缓存版本与页脚显示
#   PWA              TRUE=注册 service worker，FALSE=注销并清缓存
#   LOCAL_ZIP_PICKER TRUE=默认显示本地 ZIP 选择按钮

file(READ "${JS}" _js)
# 注意：-O3 release 的 index.js 经 JS minify，`||` 两侧空格会被去掉
# （debug 为 `Module["INITIAL_MEMORY"] || N`，release 为 `Module["INITIAL_MEMORY"]||N`）；
# 引号风格也可能因压缩在单/双引号间变化。故对 `[` 后引号、`]` 与 `||`、`||` 与数字
# 之间的空白都放宽匹配，避免 release 构建在此 FATAL_ERROR。
string(REGEX MATCH "Module\\[.INITIAL_MEMORY.\\][ \t]*\\|\\|[ \t]*([0-9]+)" _m "${_js}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "gen_build_config: 在 ${JS} 中未找到烘焙的 INITIAL_MEMORY 默认值")
endif()
set(_initial_memory "${CMAKE_MATCH_1}")

if(PWA)
    set(_pwa "true")
else()
    set(_pwa "false")
endif()

if(LOCAL_ZIP_PICKER)
    set(_zip_picker "true")
else()
    set(_zip_picker "false")
endif()

file(WRITE "${OUT}"
"// 由 CMake 在链接后生成（platforms/web/gen_build_config.cmake）。请勿手改。\n\
// 默认值与字段说明见 platforms/web/webui/public/js/config.js。\n\
window.KRKR2_BUILD_CONFIG = {\n\
    initialMemory: ${_initial_memory},\n\
    buildVersion: '${BUILD_VERSION}',\n\
    pwa: ${_pwa},\n\
    localZipPicker: ${_zip_picker}\n\
};\n")

message(STATUS "gen_build_config: initialMemory=${_initial_memory} pwa=${_pwa} zipPicker=${_zip_picker}")
