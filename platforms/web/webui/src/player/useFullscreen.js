// 全屏控制。
//
// 关键：全屏目标是 #container 而不是 canvas 本身。
// public/js/ui 时代的 ResizeObserver（现在在 Player.vue 里）观察的是 container，
// 对 container 全屏能让现有的 DPR + 尺寸跟随逻辑无缝复用；
// 直接全屏 canvas 会绕过那套逻辑，在高 DPR 屏上画面发糊。

import { ref, onMounted, onUnmounted } from 'vue';

/** iOS Safari 不支持元素全屏（只有 <video> 有专有 API），只能走伪全屏。 */
function detectElementFullscreenSupport() {
    if (typeof document === 'undefined') return false;
    return !!(
        document.fullscreenEnabled ||
        document.webkitFullscreenEnabled
    );
}

export function useFullscreen(targetRef) {
    const isFullscreen = ref(false);
    // 伪全屏：iOS 等不支持元素全屏时，用 position:fixed 铺满视口
    const isFaux = ref(false);
    const supported = detectElementFullscreenSupport();

    // PWA standalone 下已经是全屏了，按钮没有意义
    const isStandalone =
        typeof window !== 'undefined' &&
        (window.matchMedia?.('(display-mode: standalone)').matches ||
         window.navigator.standalone === true);

    function syncState() {
        const el = document.fullscreenElement || document.webkitFullscreenElement;
        isFullscreen.value = !!el;
        // 退出原生全屏时一并解除横屏锁
        if (!el && screen.orientation?.unlock) {
            try { screen.orientation.unlock(); } catch { /* 桌面端不支持，忽略 */ }
        }
    }

    async function enter() {
        const el = targetRef.value;
        if (!el) return;

        if (!supported) {
            isFaux.value = true;
            isFullscreen.value = true;
            document.body.classList.add('faux-fullscreen');
            return;
        }

        try {
            if (el.requestFullscreen) await el.requestFullscreen({ navigationUI: 'hide' });
            else if (el.webkitRequestFullscreen) await el.webkitRequestFullscreen();
        } catch (err) {
            // 用户手势缺失或被策略拒绝 —— 退化成伪全屏而不是什么都不做
            console.warn('[fullscreen] request failed, falling back:', err);
            isFaux.value = true;
            isFullscreen.value = true;
            document.body.classList.add('faux-fullscreen');
            return;
        }

        // 移动端锁横屏。桌面浏览器会抛 NotSupportedError，静默即可。
        if (screen.orientation?.lock) {
            try { await screen.orientation.lock('landscape'); } catch { /* 预期内 */ }
        }
    }

    async function exit() {
        if (isFaux.value) {
            isFaux.value = false;
            isFullscreen.value = false;
            document.body.classList.remove('faux-fullscreen');
            return;
        }
        try {
            if (document.exitFullscreen) await document.exitFullscreen();
            else if (document.webkitExitFullscreen) await document.webkitExitFullscreen();
        } catch (err) {
            console.warn('[fullscreen] exit failed:', err);
        }
    }

    function toggle() {
        return isFullscreen.value ? exit() : enter();
    }

    onMounted(() => {
        document.addEventListener('fullscreenchange', syncState);
        document.addEventListener('webkitfullscreenchange', syncState);
    });

    onUnmounted(() => {
        document.removeEventListener('fullscreenchange', syncState);
        document.removeEventListener('webkitfullscreenchange', syncState);
        document.body.classList.remove('faux-fullscreen');
    });

    return { isFullscreen, toggle, enter, exit, available: !isStandalone };
}
