// PWA 注册与强制更新。
//
// 从旧 js/ui/pwa.js 迁移，逻辑保持一致，两处必须改：
//   1. 注册路径改绝对 '/sw.js' + scope '/' —— 播放页在 /play/<id> 下，
//      原来的 './sw.js' 会解析成 /play/sw.js 而 404；
//   2. 挂载改为可选元素查询，因为三个页面的页脚结构不同。

const config = window.KrKr2Config || {};

const versionEl = document.getElementById('build-version');
if (versionEl) versionEl.textContent = `Build ${config.buildVersion || 'dev'}`;

/**
 * 强制更新：注销全部 SW + 清空 CacheStorage 后重载。
 *
 * 兜底 SW 更新流程失效的场景（旧版本 confirm 被关掉后卡住、缓存损坏）。
 * 只清应用缓存；存档在 IndexedDB 里，不受影响。
 */
export async function forceUpdate() {
    try {
        if ('serviceWorker' in navigator) {
            const regs = await navigator.serviceWorker.getRegistrations();
            await Promise.all(regs.map((r) => r.unregister()));
        }
        if ('caches' in window) {
            const names = await caches.keys();
            await Promise.all(names.map((n) => caches.delete(n)));
        }
    } catch (e) {
        console.warn('[PWA] force update cleanup failed:', e);
    }
    location.reload();
}

const forceBtn = document.getElementById('force-update-btn');
if (forceBtn) {
    forceBtn.addEventListener('click', (e) => {
        e.preventDefault();
        if (confirm('清除缓存的应用文件并加载最新版本？\n存档数据会保留。')) forceUpdate();
    });
}

function registerServiceWorker() {
    if (!('serviceWorker' in navigator)) return;

    window.addEventListener('load', async () => {
        try {
            const reg = await navigator.serviceWorker.register('/sw.js', { scope: '/' });

            setInterval(() => reg.update(), 30 * 60 * 1000);

            const promptUpdate = (worker) => {
                if (confirm('有新版本可用，是否重新加载？')) {
                    worker.postMessage('skipWaiting');
                }
            };

            if (reg.waiting) promptUpdate(reg.waiting);

            reg.addEventListener('updatefound', () => {
                const next = reg.installing;
                if (!next) return;
                next.addEventListener('statechange', () => {
                    if (next.state === 'installed' && navigator.serviceWorker.controller) {
                        promptUpdate(next);
                    }
                });
            });
        } catch (err) {
            console.warn('[PWA] service worker registration failed:', err);
        }

        let refreshing = false;
        navigator.serviceWorker.addEventListener('controllerchange', () => {
            if (!refreshing) {
                refreshing = true;
                location.reload();
            }
        });
    });
}

async function unregisterServiceWorkers() {
    if (!('serviceWorker' in navigator)) return;
    const regs = await navigator.serviceWorker.getRegistrations();
    regs.forEach((r) => r.unregister());
    if ('caches' in window) {
        const names = await caches.keys();
        names.forEach((n) => caches.delete(n));
    }
}

if (config.pwa) {
    registerServiceWorker();
} else {
    unregisterServiceWorkers();
}

window.KrKr2PWA = { forceUpdate };
