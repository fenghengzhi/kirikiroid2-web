<script setup>
// 顶部边缘唤出工具条。
//
// 取代旧的 #game-controls-bar —— 那个常驻在右上角，永远挡着画面。
// 鼠标：指针移到屏幕最顶端 6px 即滑出。
// 触屏：顶边下滑，外加一个顶部居中的小把手可直接点开 —— 只靠下滑在手机上
// 不可靠（引擎会吃掉 canvas 上的 touch，系统/浏览器又会抢顶边手势）。
// 露出后 3 秒无交互自动收起；指针停在条上时不收。

import { ref, onMounted, onUnmounted } from 'vue';

const props = defineProps({
    title: { type: String, default: '' },
    isFullscreen: { type: Boolean, default: false },
    fullscreenAvailable: { type: Boolean, default: true }
});

const emit = defineEmits(['exit', 'toggle-fullscreen', 'open-saves']);

const visible = ref(false);
const hovering = ref(false);
// 首次进入给一次性提示，让用户知道 UI 藏在哪儿
const showHint = ref(true);
// 无悬停能力的设备（手机/平板）：热区靠不住，得给一个能点的把手
const isTouch = ref(false);
// 把手已淡出：视觉上彻底消失，命中区收窄但仍在
const handleDimmed = ref(false);

const HOT_ZONE_PX = 6;
const AUTO_HIDE_MS = 3000;

// 把手只在最初几秒亮着，之后淡到全透明 —— 它是画面上唯一常驻的遮挡物。
// 淡掉后**不撤命中区**，只把它收成贴顶的一条窄带：顶边下滑偶尔还是会被
// 系统手势（下拉通知栏）截走，全屏里又没有 hover 和 F 键，
// 必须留一个看不见但点得到的逃生口，否则只能杀标签页。
const HANDLE_FADE_MS = 6000;

let hideTimer = null;
let fadeTimer = null;
let hintTimer = null;

function scheduleHide() {
    clearTimeout(hideTimer);
    hideTimer = setTimeout(() => {
        if (!hovering.value) visible.value = false;
    }, AUTO_HIDE_MS);
}

function reveal() {
    visible.value = true;
    showHint.value = false;
    scheduleHide();
}

function onPointerMove(e) {
    if (e.clientY <= HOT_ZONE_PX) reveal();
}

// 触屏：从顶部边缘起手下滑。
// 起手区放宽到 48px、位移阈值降到 16px —— 手机上顶边最外侧那几像素常被
// 系统手势（下拉通知栏 / 浏览器 UI）先截走，卡在 24px 基本划不出来。
const TOUCH_START_ZONE_PX = 48;
const TOUCH_DRAG_PX = 16;

let touchStartY = null;
function onTouchStart(e) {
    touchStartY = e.touches[0]?.clientY ?? null;
}
function onTouchMove(e) {
    if (touchStartY === null) return;
    const y = e.touches[0]?.clientY ?? 0;
    if (touchStartY <= TOUCH_START_ZONE_PX && y - touchStartY > TOUCH_DRAG_PX) {
        reveal();
        touchStartY = null;
    }
}

function onKeydown(e) {
    // 输入框里不抢键
    if (e.target instanceof HTMLElement &&
        ['INPUT', 'TEXTAREA', 'SELECT'].includes(e.target.tagName)) return;

    if (e.key === 'Escape') { reveal(); return; }
    if (e.key === 'f' || e.key === 'F') {
        e.preventDefault();
        emit('toggle-fullscreen');
    }
}

function onBarEnter() {
    hovering.value = true;
    clearTimeout(hideTimer);
}

function onBarLeave() {
    hovering.value = false;
    scheduleHide();
}

// capture 阶段监听：引擎在 canvas 上注册的 touch 处理器会 stopPropagation，
// 冒泡阶段的 window 监听收不到事件，手机上就完全唤不出工具条。
// capture 让我们先于 canvas 拿到事件；passive 保证不影响引擎自己的手势。
const TOUCH_OPTS = { passive: true, capture: true };

onMounted(() => {
    isTouch.value = window.matchMedia('(hover: none)').matches;

    // 一旦淡出就不再自己亮回来：用户已经知道入口在哪，
    // 再周期性闪出来又变成新的干扰。
    fadeTimer = setTimeout(() => { handleDimmed.value = true; }, HANDLE_FADE_MS);

    window.addEventListener('pointermove', onPointerMove, { passive: true });
    window.addEventListener('touchstart', onTouchStart, TOUCH_OPTS);
    window.addEventListener('touchmove', onTouchMove, TOUCH_OPTS);
    window.addEventListener('keydown', onKeydown);
    // 提示和把手一起退场：提示先走的话，会出现"箭头还亮着但没人解释它"的空档
    hintTimer = setTimeout(() => { showHint.value = false; }, HANDLE_FADE_MS);
});

onUnmounted(() => {
    window.removeEventListener('pointermove', onPointerMove);
    window.removeEventListener('touchstart', onTouchStart, TOUCH_OPTS);
    window.removeEventListener('touchmove', onTouchMove, TOUCH_OPTS);
    window.removeEventListener('keydown', onKeydown);
    clearTimeout(hideTimer);
    clearTimeout(fadeTimer);
    clearTimeout(hintTimer);
});
</script>

<template>
    <!-- 热区本身不可见、不吃事件，只用来兜住指针位置判断 -->
    <div class="hotzone" aria-hidden="true" />

    <!-- 触屏兜底：一个半透明小把手，点一下就展开。
         手机上没有 hover，顶边下滑又常被系统手势截走，必须留个能点的入口。 -->
    <button
        v-if="isTouch && !visible"
        class="handle"
        :class="{ dimmed: handleDimmed }"
        type="button"
        aria-label="显示控制栏"
        @click="reveal">
        <svg viewBox="0 0 24 24" fill="currentColor" width="18" height="12" aria-hidden="true">
            <path d="M7 10l5 5 5-5z" />
        </svg>
    </button>

    <Transition name="slide">
        <div
            v-show="visible"
            class="bar"
            @pointerenter="onBarEnter"
            @pointerleave="onBarLeave">
            <button class="btn btn-ghost btn-sm" @click="emit('exit')" title="退出游戏（回到游戏库）">
                <svg viewBox="0 0 24 24" fill="currentColor" width="15" height="15" aria-hidden="true">
                    <path d="M20 11H7.83l5.59-5.59L12 4l-8 8 8 8 1.41-1.41L7.83 13H20v-2z" />
                </svg>
                退出
            </button>

            <span class="title" :title="title">{{ title }}</span>

            <div class="right">
                <button class="btn btn-ghost btn-sm" @click="emit('open-saves')" title="存档空间">
                    <svg viewBox="0 0 24 24" fill="currentColor" width="15" height="15" aria-hidden="true">
                        <path d="M17 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2V7l-4-4zm-5 16a3 3 0 1 1 0-6 3 3 0 0 1 0 6zm3-10H5V5h10v4z" />
                    </svg>
                    存档
                </button>

                <button
                    v-if="fullscreenAvailable"
                    class="btn btn-ghost btn-sm"
                    @click="emit('toggle-fullscreen')"
                    :title="isFullscreen ? '退出全屏 (F)' : '全屏 (F)'">
                    <svg v-if="!isFullscreen" viewBox="0 0 24 24" fill="currentColor" width="15" height="15" aria-hidden="true">
                        <path d="M7 14H5v5h5v-2H7v-3zm-2-4h2V7h3V5H5v5zm12 7h-3v2h5v-5h-2v3zM14 5v2h3v3h2V5h-5z" />
                    </svg>
                    <svg v-else viewBox="0 0 24 24" fill="currentColor" width="15" height="15" aria-hidden="true">
                        <path d="M5 16h3v3h2v-5H5v2zm3-8H5v2h5V5H8v3zm6 11h2v-3h3v-2h-5v5zm2-11V5h-2v5h5V8h-3z" />
                    </svg>
                    {{ isFullscreen ? '退出全屏' : '全屏' }}
                </button>
            </div>
        </div>
    </Transition>

    <!-- 一次性提示：告诉用户控制条在哪，随后自行消失 -->
    <Transition name="fade">
        <div v-if="showHint && !visible" class="hint-toast" :class="{ 'below-handle': isTouch }">
            <template v-if="isTouch">点顶部箭头，或从顶边下滑</template>
            <template v-else>移动到顶部显示控制栏 · <kbd>F</kbd> 全屏</template>
        </div>
    </Transition>
</template>

<style scoped>
.hotzone {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    height: 6px;
    z-index: calc(var(--z-toolbar) - 1);
    pointer-events: none;
}

/* 触屏把手：贴顶居中，默认很淡，不抢画面。
   z-index 要压过 LocalPicker 的 .backdrop，否则选文件界面上把手点不动。 */
.handle {
    position: fixed;
    top: 0;
    left: 50%;
    transform: translateX(-50%);
    z-index: calc(var(--z-toolbar) + 1);
    display: grid;
    place-items: center;
    width: 56px;
    /* 命中区撑到 44px 以上（最小可点尺寸），视觉上仍是个小箭头 */
    min-height: 44px;
    padding: 8px 0 12px;
    padding-top: max(8px, env(safe-area-inset-top));
    border: 0;
    border-radius: 0 0 12px 12px;
    background: rgba(10, 10, 11, 0.45);
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
    color: var(--fg-2);
    opacity: 0.55;
    transition: opacity var(--dur) var(--ease);
}

.handle:active { opacity: 1; }

/* 淡出后：视觉上完全消失，但仍留一条 24px 的贴顶命中带。
   背景/箭头都透明，所以不挡画面；顶边下滑被系统手势吃掉时还能点它兜底。
   transition 只作用在观感属性上，命中区是瞬时收窄的。 */
.handle.dimmed {
    opacity: 0;
    min-height: 24px;
    padding: 0;
    background: transparent;
    backdrop-filter: none;
    -webkit-backdrop-filter: none;
    transition: opacity 1.2s var(--ease);
}

/* 淡出后再按下时给一点反馈，让人确认自己点中了 */
.handle.dimmed:active {
    opacity: 0.7;
    transition: opacity 80ms var(--ease);
}

/* 明确表达意图：淡出是纯装饰性的渐变，
   开了减弱动效就直接切换，不做长过渡 */
@media (prefers-reduced-motion: reduce) {
    .handle, .handle.dimmed { transition: none; }
}

.bar {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    z-index: var(--z-toolbar);
    display: flex;
    align-items: center;
    gap: var(--space-3);
    padding: var(--space-2) var(--space-3);
    /* 顶部有刘海/状态栏时（全屏 + viewport-fit=cover）不被遮住 */
    padding-top: max(var(--space-2), env(safe-area-inset-top));
    background: rgba(10, 10, 11, 0.72);
    backdrop-filter: blur(14px);
    -webkit-backdrop-filter: blur(14px);
    border-bottom: 1px solid var(--line);
}

.title {
    flex: 1;
    min-width: 0;
    font-size: 13px;
    font-weight: 500;
    color: var(--fg-1);
    text-align: center;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.right { display: flex; gap: var(--space-1); }

.slide-enter-active, .slide-leave-active {
    transition: transform var(--dur) var(--ease), opacity var(--dur) var(--ease);
}
.slide-enter-from, .slide-leave-to {
    transform: translateY(-100%);
    opacity: 0;
}

.hint-toast {
    position: fixed;
    top: var(--space-4);
    left: 50%;
    transform: translateX(-50%);
    z-index: var(--z-toolbar);
    padding: 7px 14px;
    border-radius: 999px;
    background: rgba(10, 10, 11, 0.8);
    backdrop-filter: blur(12px);
    border: 1px solid var(--line);
    font-size: 12px;
    color: var(--fg-1);
    pointer-events: none;
    white-space: nowrap;
}

/* 触屏时把手占着顶部居中，提示往下让一段，避免叠在一起 */
.hint-toast.below-handle {
    top: calc(var(--space-4) + 44px);
}

.hint-toast kbd {
    padding: 1px 5px;
    border-radius: 4px;
    background: var(--bg-3);
    border: 1px solid var(--line);
    font-family: inherit;
    font-size: 11px;
}

.fade-enter-active, .fade-leave-active { transition: opacity 400ms var(--ease); }
.fade-enter-from, .fade-leave-to { opacity: 0; }

@media (max-width: 640px) {
    .title { font-size: 12px; }
}
</style>
