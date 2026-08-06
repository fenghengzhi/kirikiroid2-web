// useEngine：包装 KrKr2Engine，把回调转成 Vue 响应式状态。
//
// 回调签名严格对齐 public/js/engine/engine.js 的 boot() 契约：
//   onStatus(text, pct|null)  onIdle()  onReady()
//   onSourceReady()  onRunning()  onError({code,title,message,allowUpdate})
//
// 引擎是硬单例：boot() 之后 window.Module 与 Web Lock 被终生占据，
// 直到 Document 销毁。这就是"退出游戏 = 整页跳转"的根本原因。

import { ref, onUnmounted } from 'vue';

export function useEngine() {
    // booting: 引擎自身在加载 wasm
    // ready:   引擎就绪，等待游戏数据源
    // loading: 正在注册游戏数据源
    // running: main() 已跑起来，画面出来了
    const phase = ref('booting');
    const statusText = ref('正在初始化…');
    const progress = ref(0);        // 0-100 | null（null = 不确定进度）
    const errorInfo = ref(null);

    let disposed = false;
    const guard = (fn) => (...args) => { if (!disposed) fn(...args); };

    onUnmounted(() => { disposed = true; });

    function boot({ canvas, renderer, engineScript, saveSpace }) {
        const Engine = window.KrKr2Engine;
        if (!Engine) {
            errorInfo.value = {
                title: '引擎未加载',
                message: '引擎脚本未能加载。请检查网络后重试。',
                allowUpdate: true
            };
            return;
        }

        Engine.boot({
            canvas,
            renderer,
            engineScript,
            saveSpace,

            onStatus: guard((text, pct) => {
                if (text) statusText.value = text;
                progress.value = typeof pct === 'number' ? Math.min(100, Math.max(0, pct)) : null;
            }),

            // 引擎不再加载任何东西。此时若游戏源还没就绪，说明在等数据。
            onIdle: guard(() => {
                if (phase.value === 'booting') phase.value = 'ready';
            }),

            // 引擎就绪，只差游戏源
            onReady: guard(() => {
                if (phase.value === 'booting') phase.value = 'ready';
                statusText.value = '引擎就绪';
            }),

            // 数据源已注册，即将进入 main()
            onSourceReady: guard(() => {
                phase.value = 'loading';
            }),

            // postRun：main() 已执行，画面开始渲染
            onRunning: guard(() => {
                phase.value = 'running';
                statusText.value = '';
                progress.value = null;
            }),

            onError: guard((info) => {
                errorInfo.value = {
                    title: info.title || '错误',
                    message: info.message || String(info),
                    allowUpdate: info.allowUpdate !== false
                };
            })
        });
    }

    /**
     * 注册游戏数据源。引擎在此之后才会进入 main()。
     * @param {object} src  {type:'json-url'|'zip-url'|'xp3-url'|'xp3-file'|..., url|file, entry}
     * @param {function} [chooseEntry]  多 xp3 时的选择回调
     */
    function loadSource(src, chooseEntry) {
        phase.value = 'loading';
        return window.KrKr2Engine.loadSource(src, {
            onProgress: guard((pct, text) => {
                progress.value = typeof pct === 'number' ? Math.min(100, Math.max(0, pct)) : null;
                if (text) statusText.value = text;
            }),
            chooseEntry
        }).catch((err) => {
            if (err?.name === 'AbortError') throw err;   // 用户取消不算错误
            errorInfo.value = {
                title: '加载失败',
                message: err?.message || String(err),
                allowUpdate: false
            };
            throw err;
        });
    }

    return { phase, statusText, progress, errorInfo, boot, loadSource };
}
