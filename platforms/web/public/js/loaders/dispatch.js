// 数据源加载器的调度层。
//
// 加载器只做两件事：把字节注册进 VLFS，并报告发现的 .xp3 列表。
// 进度经 hooks.onProgress(pct|null, text) 上报，多 xp3 的选择经
// hooks.chooseEntry(paths) 交还调用方 —— 原实现里这两件事都是加载器直接
// 操作 DOM（statusElement / progressFill / pickerStatus / showXp3Selector），
// 那正是前端与引擎耦合的主要来源。

(function () {
    var handlers = {};

    /**
     * 决定 Module._startupXp3Path 的取值。
     *
     * 复刻原 checkAndStartGame 的语义：xp3 数量 <= 1 时**不设置**该字段
     * （引擎走自己的默认查找）；> 1 时才需要一个明确的启动目标。
     * 显式 entry（?entry= / 画廊配置）按文件名不区分大小写匹配，匹配不到
     * 同样不设置 —— 与原实现一致。
     */
    async function resolveStartupXp3(xp3Paths, hooks, explicitEntry) {
        if (!xp3Paths || xp3Paths.length === 0) return null;

        if (explicitEntry) {
            var entryLower = explicitEntry.toLowerCase();
            var matched = xp3Paths.find(function (p) {
                return p.substring(p.lastIndexOf('/') + 1).toLowerCase() === entryLower;
            });
            return matched || null;
        }

        if (xp3Paths.length > 1) {
            if (typeof hooks.chooseEntry === 'function') {
                return await hooks.chooseEntry(xp3Paths);
            }
            // 无 UI 决策时的缺省：按不区分大小写的字典序取第一个，
            // 与原 showXp3Selector 的排序一致。
            return xp3Paths.slice().sort(function (a, b) {
                return a.toLowerCase().localeCompare(b.toLowerCase());
            })[0];
        }

        return null;
    }

    window.KrKr2Loaders = {
        handlers: handlers,

        /** @returns {Promise<{startupXp3Path: string|null}>} */
        load: function (src, hooks) {
            var handler = handlers[src && src.type];
            if (!handler) {
                return Promise.reject(new Error('Unknown source type: ' + (src && src.type)));
            }
            return Promise.resolve(handler(src, hooks || {}));
        },

        resolveStartupXp3: resolveStartupXp3
    };
})();
