<script setup>
import { ref, computed } from 'vue';
import { coverSrc } from '../shared/api.js';

const props = defineProps({
    game: { type: Object, required: true }
});

const failed = ref(false);
const src = computed(() => (failed.value ? null : coverSrc(props.game)));

// 无封面时用标题首字符占位，比一张通用 icon 更容易区分条目
const initial = computed(() => (props.game.title || '?').trim().charAt(0).toUpperCase());
</script>

<template>
    <a class="card" :href="`/play/${encodeURIComponent(game.id)}`">
        <div class="cover">
            <img
                v-if="src"
                :src="src"
                :alt="game.title"
                loading="lazy"
                decoding="async"
                @error="failed = true">
            <div v-else class="cover-fallback" aria-hidden="true">{{ initial }}</div>

            <div class="overlay">
                <span class="play">
                    <svg viewBox="0 0 24 24" fill="currentColor" width="20" height="20">
                        <path d="M8 5v14l11-7z" />
                    </svg>
                </span>
            </div>

            <span v-if="game.pinned" class="pin" title="置顶">置顶</span>
        </div>

        <div class="meta">
            <h3 class="title">{{ game.title }}</h3>
            <p v-if="game.description" class="desc">{{ game.description }}</p>
            <div v-if="game.tags.length" class="tags">
                <span v-for="tag in game.tags.slice(0, 3)" :key="tag" class="tag">{{ tag }}</span>
            </div>
        </div>
    </a>
</template>

<style scoped>
.card {
    display: flex;
    flex-direction: column;
    background: var(--bg-1);
    border: 1px solid var(--line);
    border-radius: var(--radius);
    overflow: hidden;
    transition: transform var(--dur) var(--ease),
                border-color var(--dur) var(--ease),
                background var(--dur) var(--ease);
}

/* 克制的 hover：位移 2px + 边框提亮。不用放大和彩色阴影。 */
.card:hover {
    transform: translateY(-2px);
    border-color: var(--line-strong);
    background: var(--bg-2);
}

.cover {
    position: relative;
    aspect-ratio: 3 / 4;   /* galgame 封面惯例竖版 */
    background: var(--bg-2);
    overflow: hidden;
}

.cover img {
    width: 100%;
    height: 100%;
    object-fit: cover;
    display: block;
}

.cover-fallback {
    width: 100%;
    height: 100%;
    display: grid;
    place-items: center;
    font-size: 48px;
    font-weight: 300;
    color: var(--fg-2);
    background: linear-gradient(160deg, var(--bg-2), var(--bg-1));
}

.overlay {
    position: absolute;
    inset: 0;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.5);
    opacity: 0;
    transition: opacity var(--dur) var(--ease);
}

.card:hover .overlay { opacity: 1; }

.play {
    width: 46px;
    height: 46px;
    display: grid;
    place-items: center;
    border-radius: 50%;
    background: rgba(255, 255, 255, 0.95);
    color: #000;
    padding-left: 3px;   /* 三角形视觉居中 */
}

.pin {
    position: absolute;
    top: var(--space-2);
    left: var(--space-2);
    padding: 2px 7px;
    border-radius: 999px;
    background: rgba(0, 0, 0, 0.7);
    backdrop-filter: blur(8px);
    font-size: 10px;
    color: var(--fg-0);
}

.meta {
    padding: var(--space-3);
    display: flex;
    flex-direction: column;
    gap: 6px;
    flex: 1;
}

.title {
    margin: 0;
    font-size: 14px;
    font-weight: 600;
    line-height: 1.35;
    /* 标题最多两行，超出省略 */
    display: -webkit-box;
    -webkit-line-clamp: 2;
    line-clamp: 2;
    -webkit-box-orient: vertical;
    overflow: hidden;
}

.desc {
    margin: 0;
    font-size: 12px;
    line-height: 1.5;
    color: var(--fg-1);
    display: -webkit-box;
    -webkit-line-clamp: 2;
    line-clamp: 2;
    -webkit-box-orient: vertical;
    overflow: hidden;
}

.tags {
    display: flex;
    flex-wrap: wrap;
    gap: var(--space-1);
    margin-top: auto;
    padding-top: 2px;
}
</style>
