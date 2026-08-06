// 共享 wasm 内存预创建（iOS Safari 兼容）。
//
// glue 的 initMemory 默认 new WebAssembly.Memory({maximum: 2GB, shared:true})。
// V8 对 maximum 只做惰性虚拟地址预留，而 JSC（尤其 iOS）按 maximum 实预留，
// iPhone 的 WebContent 进程拿不到 2GB，直接 RangeError: Out of memory，
// 引擎死在启动前。glue 优先采用 Module.wasmMemory（initMemory 第一分支），
// 这里逐级降档预创建：桌面第一档 2048MB 即成功（行为与原来完全一致），
// iOS 自动落到能分配的档位。提供的 maximum ≤ 模块声明的 2GB 可通过导入校验。

(function () {
    var preallocWasmMemory = null;

    // 由构建生成的 build-config.js 提供，与链接参数 -s INITIAL_MEMORY 同源
    // （CMake 变量 KRKR2_WEB_INITIAL_MEMORY），并在链接后按 index.js 里烘焙的
    // 权威值校正。ASan 构建需 ~1.3GB（影子内存把 wasm 声明的 min 顶高），
    // 非 ASan 64MB。预分配的 initial 必须 >= wasm 声明的 min，否则 glue 采用
    // 本 Memory 时导入校验失败 → LinkError。
    var INITIAL_PAGES = window.KrKr2Config.initialMemory / 65536;
    var ladder = [2048, 1536, 1024, 768];  // MB
    for (var i = 0; i < ladder.length; i++) {
        try {
            preallocWasmMemory = new WebAssembly.Memory({
                initial: INITIAL_PAGES,
                maximum: ladder[i] * 16,   // 1MB = 16 个 64KB 页
                shared: true
            });
            if (i > 0) console.warn('[mem] shared memory maximum 降档至 ' + ladder[i] + 'MB（首选 2048MB 分配失败）');
            break;
        } catch (e) { /* 尝试下一档 */ }
    }

    if (!preallocWasmMemory && !window.KrKr2Guards.jspiUnsupported) {
        window.KrKr2Guards.reportFatal({
            code: 'oom',
            title: 'Out of Memory',
            message:
                '无法分配引擎所需的共享内存，请关闭其他标签页/应用后刷新重试。\n' +
                'Failed to allocate shared WebAssembly memory. Close other tabs/apps and reload.',
            allowUpdate: true
        });
    }

    window.KrKr2Memory = { prealloc: preallocWasmMemory };
})();
