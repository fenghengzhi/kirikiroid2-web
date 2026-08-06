// 构建配置的运行时兜底默认值。
//
// 真实值由 CMake 在链接后生成的 build-config.js 提供（见 platforms/web/
// gen_build_config.cmake），它在本文件之前加载并直接赋值 window.KRKR2_BUILD_CONFIG。
// 直接从源码树打开页面（无构建产物）时那个文件 404，本文件把每一项填成开发默认值，
// 页面照常可跑 —— 与 sw.js 里 `if (CACHE_VERSION.charAt(0) === '@')` 同一思路。
//
// 取代了原 shell.html 的 4 个 configure_file 占位符：
//   @KRKR2_WEB_INITIAL_MEMORY@            → initialMemory
//   @KRKR2_BUILD_VERSION@                 → buildVersion
//   @KRKR2_LOCAL_ZIP_PICKER_DEFAULT_VISIBLE@ → localZipPicker
//   @KRKR2_PWA_HEAD@ / @KRKR2_PWA_SCRIPT@ → pwa

(function () {
    var c = window.KRKR2_BUILD_CONFIG = window.KRKR2_BUILD_CONFIG || {};

    // 预分配 WebAssembly.Memory 的 initial 字节数。必须 >= wasm 模块声明的 min，
    // 否则 glue 采用本 Memory 时导入校验失败 → LinkError。构建时由链接产物
    // index.js 里烘焙的 INITIAL_MEMORY 权威值填入；此处仅为非 ASan 构建的默认值。
    if (typeof c.initialMemory !== 'number') c.initialMemory = 67108864;   // 64 MiB

    if (!c.buildVersion) c.buildVersion = 'dev';

    // 是否注册 service worker（Release）还是注销既有 SW 并清缓存（Debug）
    if (typeof c.pwa !== 'boolean') c.pwa = false;

    // 本地 ZIP 选择按钮是否默认可见（Debug 可见，Release 需 ?pickZip=1）
    if (typeof c.localZipPicker !== 'boolean') c.localZipPicker = true;

    // 同源资源的基地址，必须以 '/' 结尾。
    //
    // 存在的理由：播放页 URL 是 /play/<id>，裸相对路径会解析成 /play/index.js 而 404。
    //
    // index.js（emscripten glue）必须走这里、必须同源：glue 顶部的 _scriptName
    // 取自 document.currentScript.src，pthread worker 脚本按它定位，跨域会被
    // 同源策略挡下。真要把 glue 也挪走，得同时设 Module.mainScriptUrlOrBlob
    // 指向同源 Blob URL —— 目前没这个必要，见下面的 engineBase。
    if (typeof c.assetBase !== 'string' || !c.assetBase) c.assetBase = '/';
    if (c.assetBase.charAt(c.assetBase.length - 1) !== '/') c.assetBase += '/';

    // 大二进制（index.wasm 21.9 MiB / assets.zip 7.8 MiB）的基地址。
    //
    // 与 assetBase 分开是有技术原因的，不是洁癖：这两个文件跨域没有任何副作用
    //   - index.wasm 经 Module.locateFile 重定向（见 js/engine/engine.js）
    //   - assets.zip 是一次普通 fetch（见 js/engine/vlfs-bridge.js）
    // 而 index.js 跨域就会踩上面 _scriptName 那个坑。所以只挪这两个，glue 留下。
    //
    // 动机：index.wasm 已占 Workers 静态资源 25 MiB 单文件上限的 87.5%。指向
    // R2 公开桶后部署产物从 30 MB 降到约 680 KB，且该上限彻底不适用。
    //
    // 跨域时 R2 侧必须发两个头，否则请求会被挡掉：
    //   Access-Control-Allow-Origin    fetch 需要
    //   Cross-Origin-Resource-Policy   COEP: require-corp 需要（见 worker/headers.js）
    //
    // 不设则回落到 assetBase，即"全部同源"的现状行为。
    if (typeof c.engineBase !== 'string' || !c.engineBase) c.engineBase = c.assetBase;
    if (c.engineBase.charAt(c.engineBase.length - 1) !== '/') c.engineBase += '/';

    window.KrKr2Config = c;
})();
