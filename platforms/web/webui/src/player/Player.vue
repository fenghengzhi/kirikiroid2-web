<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue';
import { api } from '../shared/api.js';
import { useEngine } from './useEngine.js';
import { useFullscreen } from './useFullscreen.js';
import { attachSaveSpace } from './saveSpace.js';
import EdgeToolbar from './EdgeToolbar.vue';
import SaveSpacePanel from './SaveSpacePanel.vue';
import LocalPicker from './LocalPicker.vue';

const container = ref(null);
const canvas = ref(null);

const game = ref(null);
const fatal = ref(null);          // 取游戏元数据阶段的错误
const showSaves = ref(false);
const xp3Choices = ref(null);     // 多 xp3 时的候选列表
let xp3Resolve = null;

const { phase, statusText, progress, errorInfo, boot, loadSource } = useEngine();
const { isFullscreen, toggle: toggleFullscreen, available: fullscreenAvailable } =
    useFullscreen(container);

// /play/local 是"打开本地文件"入口，不对应任何库里的条目
const gameId = decodeURIComponent(location.pathname.replace(/^\/play\/?/, ''));

// ?xp3= / ?game= / ?entry=：直接指定数据源，不查 D1。
// 引擎开发用的入口 —— coi-server.py 就打印这种 URL（见根 README「Running」），
// 也是唯一能脱离游戏库单测一个 xp3 的办法。旧 js/app.js 有这个能力，
// 换 Vue 后必须保留，否则 `--xp3` 那套调试流程直接失效。
const urlSource = (() => {
    const p = new URLSearchParams(location.search);
    const xp3 = p.get('xp3');
    const zip = p.get('game');
    const entry = p.get('entry') || undefined;
    // 与旧 app.js 一致：?game= 优先于 ?xp3=，两个都给时不猜意图
    if (zip) return { type: 'zip-url', url: zip, entry };
    if (xp3) return { type: 'xp3-url', url: xp3 };
    return null;
})();

const isLocalMode = !urlSource && (!gameId || gameId === 'local');

const displayTitle = computed(() => {
    if (game.value?.title) return game.value.title;
    if (urlSource) {
        // 拿文件名当标题：URL 可能带 query，先切掉
        const path = urlSource.url.split(/[?#]/)[0];
        return decodeURIComponent(path.substring(path.lastIndexOf('/') + 1)) || '本地文件';
    }
    return isLocalMode ? '本地文件' : '载入中…';
});
const busy = computed(() => phase.value !== 'running' && !errorInfo.value && !fatal.value);

const engineBase = () => (window.KrKr2Config?.engineBase) ||
                         (window.KrKr2Config?.assetBase) || '/';

function exitToGallery() {
    // 整页跳转，不是路由切换：引擎是硬单例，必须靠 Document 销毁
    // 才能释放 Web Lock 和 wasm runtime。
    location.href = '/';
}

// --- canvas 尺寸跟随容器 + devicePixelRatio ---------------------------
// 从旧 js/ui/shell-ui.js 的同名逻辑迁移，行为保持一致。
let resizeObserver = null;

function updateCanvasSize() {
    const el = canvas.value;
    const box = container.value;
    if (!el || !box) return;

    const dpr = window.devicePixelRatio || 1;
    const w = Math.round((Math.round(box.clientWidth) || 1280) * dpr);
    const h = Math.round((Math.round(box.clientHeight) || 720) * dpr);
    if (el.width !== w || el.height !== h) {
        el.width = w;
        el.height = h;
    }
}

function onContextLost(e) {
    e.preventDefault();
    errorInfo.value = {
        title: 'WebGL 上下文丢失',
        message: '显卡上下文被浏览器回收，需要重新加载页面。',
        allowUpdate: false
    };
}

// --- 启动 ------------------------------------------------------------

/** 把 downloadUrl 的后缀映射成加载器类型。 */
function sourceTypeFor(url) {
    const u = url.trim().toLowerCase();
    if (u.endsWith('.json') || u.includes('.json?')) return 'json-url';
    if (u.endsWith('.xp3') || u.includes('.xp3?')) return 'xp3-url';
    return 'zip-url';
}

/** 多 xp3 时由用户选择，返回 Promise 交还给加载器。 */
function chooseEntry(paths) {
    return new Promise((resolve) => {
        xp3Choices.value = [...paths].sort((a, b) =>
            a.toLowerCase().localeCompare(b.toLowerCase()));
        xp3Resolve = resolve;
    });
}

function pickXp3(path) {
    xp3Choices.value = null;
    xp3Resolve?.(path);
    xp3Resolve = null;
}

/** 由 LocalPicker 触发：本地文件/目录已选好，直接注册进引擎。 */
async function startLocalSource(src) {
    // 本地模式没有库条目，用固定空间，避免每个文件一个库
    await window.KrKr2Engine.setSaveSpace('local', { remember: true, register: true });
    try {
        await loadSource(src, chooseEntry);
    } catch (err) {
        if (err?.name !== 'AbortError') console.error('[player] local load failed:', err);
    }
}

onMounted(async () => {
    updateCanvasSize();
    resizeObserver = new ResizeObserver(updateCanvasSize);
    resizeObserver.observe(container.value);
    window.addEventListener('resize', updateCanvasSize);
    canvas.value.addEventListener('webglcontextlost', onContextLost, false);

    // 平台守卫（单例冲突 / JSPI 缺失）优先于一切
    if (window.KrKr2Guards?.fatal) {
        errorInfo.value = window.KrKr2Guards.fatal;
        return;
    }

    const params = new URLSearchParams(location.search);
    const renderer = window.KrKr2FS.normalizeRenderer(params.get('renderer'));

    // 引擎脚本必须用绝对地址：当前页在 /play/<id>，
    // 相对的 'index.js' 会解析成 /play/index.js 而 404。
    //
    // 取 engineBase 而非 assetBase：走 R2 时 glue 也在 /engine/<版本>/ 下。
    // 那条路由是同源的（Worker 读 R2 后按同源返回），所以 glue 顶部的
    // _scriptName 照样能正确定位 pthread worker 脚本。
    boot({
        canvas: canvas.value,
        renderer,
        engineScript: engineBase() + 'index.js'
    });

    if (isLocalMode) return;   // 等 LocalPicker 给数据源

    // ?xp3= / ?game= 直连数据源，跳过 D1 查询。
    // 存档空间用固定的 'url'：这类 URL 是临时调试目标，没有稳定 id 可绑，
    // 按 URL 建空间会让每次改路径都换一个新库。
    if (urlSource) {
        try {
            await window.KrKr2Engine.setSaveSpace('url', { remember: true, register: true });
        } catch (err) {
            console.warn('[player] 存档空间绑定失败，本次游戏不保存进度:', err);
        }
        try {
            await loadSource(urlSource, chooseEntry);
        } catch {
            // 错误已由 useEngine 写入 errorInfo
        }
        return;
    }

    try {
        game.value = await api.getGame(gameId);
    } catch (err) {
        fatal.value = err.status === 404
            ? '这个游戏不存在，或已被下架。'
            : (err.message || '无法加载游戏信息');
        return;
    }

    if (!game.value.downloadUrl) {
        fatal.value = '该条目尚未配置资源地址，请到管理后台补上 downloadUrl。';
        return;
    }

    // 存档空间绑 game.id（并迁移旧的 save_<title>），标题改动不再丢档
    try {
        await attachSaveSpace(game.value);
    } catch (err) {
        console.warn('[player] 存档空间绑定失败，本次游戏不保存进度:', err);
    }

    try {
        await loadSource({
            type: sourceTypeFor(game.value.downloadUrl),
            url: game.value.downloadUrl.trim(),
            entry: game.value.entryXp3?.trim() || undefined
        }, chooseEntry);
    } catch {
        // 错误已由 useEngine 写入 errorInfo
    }
});

onUnmounted(() => {
    resizeObserver?.disconnect();
    window.removeEventListener('resize', updateCanvasSize);
});
</script>

<template>
    <!-- 容器即全屏目标：ResizeObserver 观察的也是它，两者对齐。
         所有 UI 都必须放在容器**内部** —— 元素全屏后 .stage 进入 top layer，
         它的兄弟节点会被 ::backdrop 盖住，哪怕 position:fixed + 高 z-index 也没用。
         手机上一旦如此就彻底没救：没有 hover、没有 F 键，退不出也开不了存档。 -->
    <div ref="container" class="stage">
        <!-- 不绑 dblclick 切全屏：galgame 推文本就是快速连点，必然误触发。
             全屏入口只留边缘工具条的按钮和 F 键。 -->
        <canvas
            ref="canvas"
            id="canvas"
            tabindex="-1"
            @contextmenu.prevent />

        <EdgeToolbar
            :title="displayTitle"
            :is-fullscreen="isFullscreen"
            :fullscreen-available="fullscreenAvailable"
            @exit="exitToGallery"
            @toggle-fullscreen="toggleFullscreen"
            @open-saves="showSaves = true" />

        <!-- 加载浮层：仅在引擎未跑起来时存在，跑起来后彻底移除，不留任何遮挡 -->
        <div v-if="busy" class="overlay">
            <div class="loader">
                <h1 class="brand">{{ displayTitle }}</h1>
                <div class="track">
                    <div
                        class="fill"
                        :class="{ indeterminate: progress === null }"
                        :style="progress !== null ? { width: progress + '%' } : null" />
                </div>
                <p class="status">{{ statusText }}</p>
            </div>
        </div>

        <!-- 本地文件入口 -->
        <LocalPicker
            v-if="isLocalMode && phase !== 'running' && phase !== 'loading'"
            @source="startLocalSource"
            @cancel="exitToGallery" />

        <!-- 多 xp3 选择 -->
        <div v-if="xp3Choices" class="modal-backdrop">
            <div class="modal">
                <h2>选择启动的档案</h2>
                <p class="sub">发现多个 .xp3 文件，请选择要启动的那个：</p>
                <div class="xp3-list">
                    <button v-for="p in xp3Choices" :key="p" class="xp3-item" @click="pickXp3(p)">
                        <span class="xp3-name">{{ p.substring(p.lastIndexOf('/') + 1) }}</span>
                        <span class="xp3-dir">{{ p.substring(0, p.lastIndexOf('/')) || '/' }}</span>
                    </button>
                </div>
            </div>
        </div>

        <SaveSpacePanel v-if="showSaves" @close="showSaves = false" />

        <!-- 致命错误 -->
        <div v-if="errorInfo || fatal" class="modal-backdrop">
            <div class="modal">
                <h2>{{ errorInfo?.title || '无法开始游戏' }}</h2>
                <p class="msg">{{ errorInfo?.message || fatal }}</p>
                <div class="modal-actions">
                    <button class="btn" @click="exitToGallery">返回游戏库</button>
                    <button
                        v-if="errorInfo?.allowUpdate"
                        class="btn btn-primary"
                        @click="() => window.KrKr2PWA?.forceUpdate?.() ?? location.reload()">
                        强制更新并重载
                    </button>
                </div>
            </div>
        </div>
    </div>
</template>

<style scoped>
.stage {
    position: fixed;
    inset: 0;
    background: #000;
    overflow: hidden;
}

.stage canvas {
    display: block;
    width: 100%;
    height: 100%;
    /* 像素风游戏放大时不糊；高分辨率素材由引擎自己处理 */
    image-rendering: auto;
    touch-action: none;
}

.overlay {
    position: fixed;
    inset: 0;
    z-index: var(--z-overlay);
    display: grid;
    place-items: center;
    background: var(--bg-0);
}

.loader { width: min(420px, 82vw); text-align: center; }

.brand {
    margin: 0 0 var(--space-5);
    font-size: 17px;
    font-weight: 600;
    letter-spacing: -0.01em;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.track {
    height: 2px;
    border-radius: 2px;
    background: var(--bg-3);
    overflow: hidden;
}

.fill {
    height: 100%;
    background: var(--fg-0);
    border-radius: 2px;
    transition: width 240ms var(--ease);
}

/* 进度未知时来回扫，而不是卡在 0% 让人以为死了 */
.fill.indeterminate {
    width: 35%;
    animation: sweep 1.3s var(--ease) infinite;
}

@keyframes sweep {
    0%   { transform: translateX(-100%); }
    100% { transform: translateX(320%); }
}

.status {
    margin: var(--space-4) 0 0;
    font-size: 12px;
    color: var(--fg-1);
    min-height: 1.2em;
}

.modal-backdrop {
    position: fixed;
    inset: 0;
    z-index: var(--z-modal);
    display: grid;
    place-items: center;
    padding: var(--space-4);
    background: rgba(0, 0, 0, 0.72);
    backdrop-filter: blur(8px);
}

.modal {
    width: min(460px, 100%);
    max-height: 80vh;
    overflow: auto;
    padding: var(--space-5);
    border-radius: var(--radius);
    border: 1px solid var(--line);
    background: var(--bg-1);
    box-shadow: var(--shadow-lg);
}

.modal h2 { margin: 0 0 var(--space-2); font-size: 16px; font-weight: 600; }
.modal .sub, .modal .msg {
    margin: 0 0 var(--space-4);
    font-size: 13px;
    line-height: 1.7;
    color: var(--fg-1);
    white-space: pre-wrap;
}

.modal-actions {
    display: flex;
    gap: var(--space-2);
    justify-content: flex-end;
    flex-wrap: wrap;
}

.xp3-list { display: flex; flex-direction: column; gap: var(--space-2); }

.xp3-item {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: var(--space-3);
    border-radius: var(--radius-sm);
    border: 1px solid var(--line);
    background: var(--bg-2);
    text-align: left;
    transition: background var(--dur) var(--ease), border-color var(--dur) var(--ease);
}

.xp3-item:hover { background: var(--bg-3); border-color: var(--line-strong); }
.xp3-name { font-size: 13px; font-weight: 500; }
.xp3-dir { font-size: 11px; color: var(--fg-2); }
</style>
