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

    window.KrKr2Config = c;
})();
