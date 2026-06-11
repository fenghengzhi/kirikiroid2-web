// WebKit/JSC JSPI 兼容补丁：覆盖 emscripten 内置 emscripten_set_main_loop_arg。
//
// 背景：glue 的 getWasmTableEntry 依赖「wasmTable.get(ptr) === instance.exports.f」
// 的函数对象身份比较（Asyncify.isAsyncExport）识别 JSPI_EXPORTS 导出，命中才包
// WebAssembly.promising。V8 满足该身份等价（Wasm JS-API 规范要求同一 wasm 函数
// 对应同一 JS 对象），但 JSC（实测 WebKit 26.4 / Safari 27 beta，2026-06）的
// table.get 返回与 export 不同一的函数对象，识别落空 → krkr2_main_loop_tick
// 未包 promising → 主循环 tick 内首次 JSPI 挂起读抛
// "SuspendError: Suspending() wrapper called outside of a promising() context"。
//
// 本项目 emscripten_set_main_loop_arg 的唯一注册者是 krkr2_main_loop_tick
//（JSPI_EXPORTS 唯一成员，见 CMakeLists.txt 与 vcpkg cocos2dx patch
// CCApplication-emscripten.cpp），故此处无条件包 promising——与 Chrome 下
// getWasmTableEntry 命中身份比较后的行为完全一致，对 V8 无行为变化。
addToLibrary({
  emscripten_set_main_loop_arg__deps: ['$setMainLoop'],
  emscripten_set_main_loop_arg: (func, arg, fps, simulateInfiniteLoop) => {
    var wrappedTick = WebAssembly.promising(wasmTable.get(func));
    var iterFunc = () => wrappedTick(arg);
    setMainLoop(iterFunc, fps, simulateInfiniteLoop, arg);
  },
});
