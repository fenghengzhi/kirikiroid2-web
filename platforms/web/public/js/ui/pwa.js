// PWA 与应用更新。
//
// 取代原来的三个 configure_file 片段：
//   pwa-head.html        → index.html 里的静态 meta（两种构建都有）
//   pwa-sw.html          → registerServiceWorker()，Release 走
//   pwa-unregister.html  → unregisterServiceWorkers()，Debug 走
// 二选一改由 build-config.js 的 pwa 标志在运行时决定。

(function () {
    var BUILD_VERSION = window.KrKr2Config.buildVersion;
    document.getElementById('build-version').textContent = 'Build ' + BUILD_VERSION;

    // --- 强制更新：注销全部 service worker + 清空 CacheStorage 后重载 ---
    // 兜底 SW 更新流程失效的场景（如旧版本 confirm 被关掉后卡住、缓存损坏）。
    // 只清应用缓存；存档在 IndexedDB / 存档空间里，不受影响。
    // 顺序：先 unregister（重载导航即不被旧 SW 接管），后删 cache，再 reload；
    // 重载后本文件会重新注册 sw.js（浏览器对 sw.js 默认绕过 HTTP 缓存），
    // 新 SW 重新预缓存全部资源。
    function forceUpdate() {
        var steps = Promise.resolve();
        if ('serviceWorker' in navigator) {
            steps = steps.then(function () {
                return navigator.serviceWorker.getRegistrations().then(function (regs) {
                    return Promise.all(regs.map(function (reg) { return reg.unregister(); }));
                });
            });
        }
        if ('caches' in window) {
            steps = steps.then(function () {
                return caches.keys().then(function (names) {
                    return Promise.all(names.map(function (name) { return caches.delete(name); }));
                });
            });
        }
        steps.catch(function (e) {
            console.warn('[PWA] force update cleanup failed:', e);
        }).then(function () {
            window.location.reload();
        });
    }

    document.getElementById('force-update-btn').addEventListener('click', function (e) {
        e.preventDefault();
        if (confirm('Clear cached app files and reload the latest version?\nSave data will be kept.')) forceUpdate();
    });
    document.getElementById('error-update-btn').addEventListener('click', function () {
        forceUpdate();
    });

    function registerServiceWorker() {
        if (!('serviceWorker' in navigator)) return;
        window.addEventListener('load', function () {
            navigator.serviceWorker.register('./sw.js', { scope: './' }).then(function (reg) {
                /* Check for updates periodically (every 30 min) */
                setInterval(function () { reg.update(); }, 30 * 60 * 1000);
                /* When a new SW is waiting, prompt user to reload */
                function promptUpdate(worker) {
                    if (confirm('A new version of Kirikiroid2 Web is available. Reload to update?')) {
                        worker.postMessage('skipWaiting');
                    }
                }
                if (reg.waiting) { promptUpdate(reg.waiting); }
                reg.addEventListener('updatefound', function () {
                    var newWorker = reg.installing;
                    newWorker.addEventListener('statechange', function () {
                        if (newWorker.state === 'installed' && navigator.serviceWorker.controller) {
                            promptUpdate(newWorker);
                        }
                    });
                });
            }).catch(function (err) {
                console.warn('[PWA] service worker registration failed:', err);
            });
            /* Reload page when new SW takes over */
            var refreshing = false;
            navigator.serviceWorker.addEventListener('controllerchange', function () {
                if (!refreshing) { refreshing = true; window.location.reload(); }
            });
        });
    }

    function unregisterServiceWorkers() {
        if (!('serviceWorker' in navigator)) return;
        navigator.serviceWorker.getRegistrations().then(function (registrations) {
            registrations.forEach(function (reg) { reg.unregister(); });
        });
        if ('caches' in window) {
            caches.keys().then(function (names) {
                names.forEach(function (name) { caches.delete(name); });
            });
        }
    }

    if (window.KrKr2Config.pwa) {
        registerServiceWorker();
    } else {
        unregisterServiceWorkers();
    }

    window.KrKr2PWA = { forceUpdate: forceUpdate };
})();
