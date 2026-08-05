// VirtualLazyFS（vlfs.js）与引擎之间的桥接：会话初始化、写回持久化、UI 资源注册。
//
// 依赖 window.VLFS（platforms/web/vlfs.js）与 window.KrKr2Guards（单例锁）。

(function () {
    // --- VirtualLazyFS 初始化（创建独立 OPFS 会话、保留完整 ZIP 缓存）---
    // 必须在取得跨标签页单例锁后才接触 OPFS。冲突页让此 Promise 保持
    // pending，因此不会继续创建 OPFS 会话、注册 ZIP 或启动 wasm main。
    var vlfsReady = window.KrKr2Guards.singletonReady.then(function (acquired) {
        if (!acquired) return new Promise(function () {});
        return VLFS.init();
    }).catch(function (e) {
        console.error('[vlfs] init failed:', e);
    });

    // VLFS 写关闭钩子：统一持久化链路（取代旧 fsafs_flush_file）。
    // 1) MEMFS 小文件镜像 —— 兼容遗留 fopen 直读（GlobalConfigManager 等）；
    // 2) IndexedDB write-through —— 存档空间持久化；
    // 3) FSA 主机目录回写 —— 用户经 showDirectoryPicker 加载时同步回硬盘。
    //
    // Module._hostDirHandle / _hostDirPrefix / _saveSpaceId 是引擎层与 JS 侧共享的
    // 状态槽位，沿用原字段名（详见 js/engine/engine.js 顶部的契约说明）。
    VLFS.onWriteClose = function (path, data) {
        if (data.length <= 8 * 1024 * 1024 && typeof FS !== 'undefined') {
            try { window.KrKr2FS.writeFileToFS(path, data); } catch (e) {}
        }
        // 游戏可能把存档写在游戏目录内的 savedata/ 下（如千恋万花
        // mainwindow.tjs checkSave），一并纳入 IDB 持久化
        var isSavePath = path.startsWith('/savedata/') || path.startsWith('/save/') ||
            path.indexOf('/savedata/') >= 0;
        if (!Module._hostDirHandle) {
            if (Module._saveSpaceId && isSavePath) window.KrKr2IDB.saveFile(path, data);
            return;
        }
        var dirHandle = Module._hostDirHandle;
        var prefix = Module._hostDirPrefix;
        var copy = data.slice();
        setTimeout(async function () {
            try {
                var relPath = path;
                if (prefix && relPath.startsWith(prefix + '/')) {
                    relPath = relPath.substring(prefix.length);
                }
                var parts = relPath.split('/').filter(Boolean);
                var fileName = parts.pop();
                var cur = dirHandle;
                for (var i = 0; i < parts.length; i++) {
                    cur = await cur.getDirectoryHandle(parts[i], { create: true });
                }
                var fh = await cur.getFileHandle(fileName, { create: true });
                var writable = await fh.createWritable();
                await writable.write(copy);
                await writable.close();
            } catch (err) {
                console.warn('[vlfs] FSA write-back FAILED: ' + path + ' - ' + err.message);
            }
        }, 0);
    };

    // --- UI 资源/字体（assets.zip, store 模式打包）注册进 VLFS ---
    // 取代 --preload-file index.data：资源以 Blob 切片按需读取
    // （cocos CCFileUtils 经 krkr2_vlfs_read_all 桥访问），不驻留内存。
    var assetsLoaded = (async function () {
        await vlfsReady;
        var resp = await fetch('assets.zip');
        if (!resp.ok) throw new Error('assets.zip HTTP ' + resp.status);
        var blob = await resp.blob();
        var r = await VLFS.registerZipBlob(blob, { stripPrefix: '' });
        console.log('[vlfs] assets.zip registered: ' + r.paths.length + ' entries');
    })().catch(function (e) {
        console.error('[vlfs] assets.zip load failed:', e);
    });

    window.KrKr2VLFS = {
        ready: vlfsReady,
        assetsLoaded: assetsLoaded
    };
})();
