<script setup>
import { ref, computed, onMounted } from 'vue';
import { api } from '../shared/api.js';
import GameCard from './GameCard.vue';

const games = ref([]);
const loading = ref(true);
const loadError = ref('');
const search = ref('');
const activeTag = ref('');

const allTags = computed(() => {
    const counts = new Map();
    for (const g of games.value) {
        for (const t of g.tags) counts.set(t, (counts.get(t) || 0) + 1);
    }
    return [...counts.entries()].sort((a, b) => b[1] - a[1]).map(([tag]) => tag);
});

const filtered = computed(() => {
    const q = search.value.trim().toLowerCase();
    return games.value.filter((g) => {
        if (activeTag.value && !g.tags.includes(activeTag.value)) return false;
        if (!q) return true;
        return (
            g.title.toLowerCase().includes(q) ||
            g.description.toLowerCase().includes(q) ||
            g.tags.some((t) => t.toLowerCase().includes(q))
        );
    });
});

// 库里一个游戏都没有 vs 有游戏但筛没了 —— 两种空状态的引导完全不同
const isEmptyLibrary = computed(() => !loading.value && games.value.length === 0);

function toggleTag(tag) {
    activeTag.value = activeTag.value === tag ? '' : tag;
}

function clearFilters() {
    search.value = '';
    activeTag.value = '';
}

onMounted(async () => {
    try {
        games.value = await api.listGames();
    } catch (err) {
        loadError.value = err.message || '加载失败';
    } finally {
        loading.value = false;
    }
});
</script>

<template>
    <header class="nav">
        <a class="brand" href="/">
            <svg viewBox="0 0 24 24" fill="currentColor" width="20" height="20" aria-hidden="true">
                <path d="M21 6H3c-1.1 0-2 .9-2 2v8c0 1.1.9 2 2 2h18c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2zm-10 7H8v3H6v-3H3v-2h3V8h2v3h3v2zm4.5 2c-.83 0-1.5-.67-1.5-1.5s.67-1.5 1.5-1.5 1.5.67 1.5 1.5-.67 1.5-1.5 1.5zm3-3c-.83 0-1.5-.67-1.5-1.5s.67-1.5 1.5-1.5 1.5.67 1.5 1.5-.67 1.5-1.5 1.5z" />
            </svg>
            <span>Kirikiroid2</span>
        </a>

        <div class="nav-right">
            <a class="btn btn-ghost btn-sm" href="/play/local">打开本地文件</a>
            <a class="btn btn-ghost btn-sm" href="/admin">管理</a>
        </div>
    </header>

    <main class="body">
        <div class="head">
            <div>
                <h1 class="h1">游戏库</h1>
                <p class="count" v-if="!loading">
                    {{ filtered.length }}<template v-if="filtered.length !== games.length"> / {{ games.length }}</template> 个游戏
                </p>
            </div>

            <label class="search" v-if="games.length">
                <svg viewBox="0 0 24 24" fill="currentColor" width="16" height="16" aria-hidden="true">
                    <path d="M15.5 14h-.79l-.28-.27C15.41 12.59 16 11.11 16 9.5 16 5.91 13.09 3 9.5 3S3 5.91 3 9.5 5.91 16 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z" />
                </svg>
                <input v-model="search" type="search" placeholder="搜索标题、简介或标签" aria-label="搜索游戏">
            </label>
        </div>

        <div class="tagbar" v-if="allTags.length">
            <button
                v-for="tag in allTags"
                :key="tag"
                class="tag-btn"
                :class="{ active: activeTag === tag }"
                @click="toggleTag(tag)">
                {{ tag }}
            </button>
        </div>

        <!-- 骨架屏：保持与真实卡片相同的宽高比，避免加载完成时布局跳动 -->
        <div v-if="loading" class="grid">
            <div v-for="n in 10" :key="n" class="skeleton" />
        </div>

        <div v-else-if="loadError" class="empty">
            <h2>加载失败</h2>
            <p>{{ loadError }}</p>
            <button class="btn" @click="() => location.reload()">重试</button>
        </div>

        <!-- 空库：首次部署的正常状态，引导去后台 -->
        <div v-else-if="isEmptyLibrary" class="empty">
            <h2>游戏库还是空的</h2>
            <p>到管理后台添加第一个游戏，或直接打开本地的 .xp3 / .zip 文件试玩。</p>
            <div class="empty-actions">
                <a class="btn btn-primary" href="/admin">前往管理后台</a>
                <a class="btn" href="/play/local">打开本地文件</a>
            </div>
        </div>

        <!-- 有库但筛空了 -->
        <div v-else-if="filtered.length === 0" class="empty">
            <h2>没有匹配的游戏</h2>
            <p>换个关键词，或清除当前筛选条件。</p>
            <button class="btn" @click="clearFilters">清除筛选</button>
        </div>

        <div v-else class="grid">
            <GameCard v-for="game in filtered" :key="game.id" :game="game" />
        </div>
    </main>
</template>

<style scoped>
.nav {
    position: sticky;
    top: 0;
    z-index: var(--z-toolbar);
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-4);
    padding: var(--space-3) var(--space-5);
    background: rgba(10, 10, 11, 0.8);
    backdrop-filter: blur(16px);
    border-bottom: 1px solid var(--line);
}

.brand {
    display: flex;
    align-items: center;
    gap: var(--space-2);
    font-size: 14px;
    font-weight: 600;
    letter-spacing: 0.01em;
}

.brand svg { color: var(--fg-1); }

.nav-right { display: flex; gap: var(--space-2); }

.body {
    max-width: 1400px;
    margin: 0 auto;
    padding: var(--space-6) var(--space-5) var(--space-7);
}

.head {
    display: flex;
    align-items: flex-end;
    justify-content: space-between;
    gap: var(--space-4);
    flex-wrap: wrap;
    margin-bottom: var(--space-5);
}

.h1 {
    margin: 0;
    font-size: 26px;
    font-weight: 600;
    letter-spacing: -0.02em;
}

.count { margin: 6px 0 0; font-size: 13px; color: var(--fg-1); }

.search {
    display: flex;
    align-items: center;
    gap: var(--space-2);
    padding: 0 var(--space-3);
    min-width: 260px;
    border: 1px solid var(--line);
    border-radius: var(--radius-sm);
    background: var(--bg-1);
    color: var(--fg-2);
    transition: border-color var(--dur) var(--ease);
}

.search:focus-within { border-color: var(--line-strong); }

.search input {
    flex: 1;
    padding: 9px 0;
    border: none;
    background: none;
    outline: none;
    font-size: 13px;
    color: var(--fg-0);
}

.search input::placeholder { color: var(--fg-2); }
.search input::-webkit-search-cancel-button { filter: invert(0.6); }

.tagbar {
    display: flex;
    flex-wrap: wrap;
    gap: var(--space-2);
    margin-bottom: var(--space-5);
}

.tag-btn {
    padding: 4px 11px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: var(--bg-1);
    font-size: 12px;
    color: var(--fg-1);
    transition: background var(--dur) var(--ease),
                color var(--dur) var(--ease),
                border-color var(--dur) var(--ease);
}

.tag-btn:hover { border-color: var(--line-strong); color: var(--fg-0); }

.tag-btn.active {
    background: var(--fg-0);
    border-color: transparent;
    color: var(--bg-0);
}

.grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(190px, 1fr));
    gap: var(--space-4);
}

.skeleton {
    aspect-ratio: 3 / 4.7;   /* 封面 3:4 加下方元信息区 */
    border-radius: var(--radius);
    background: linear-gradient(90deg, var(--bg-1) 25%, var(--bg-2) 50%, var(--bg-1) 75%);
    background-size: 200% 100%;
    animation: shimmer 1.4s infinite;
}

@keyframes shimmer {
    to { background-position: -200% 0; }
}

.empty {
    text-align: center;
    padding: var(--space-7) var(--space-4);
    border: 1px dashed var(--line);
    border-radius: var(--radius-lg);
    background: var(--bg-1);
}

.empty h2 {
    margin: 0 0 var(--space-2);
    font-size: 17px;
    font-weight: 600;
}

.empty p {
    margin: 0 auto var(--space-5);
    max-width: 420px;
    font-size: 13px;
    line-height: 1.65;
    color: var(--fg-1);
}

.empty-actions {
    display: flex;
    gap: var(--space-2);
    justify-content: center;
    flex-wrap: wrap;
}

@media (max-width: 640px) {
    .nav { padding: var(--space-3) var(--space-4); }
    .body { padding: var(--space-5) var(--space-4) var(--space-6); }
    .search { min-width: 0; width: 100%; }
    .grid { grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: var(--space-3); }
    .h1 { font-size: 22px; }
}
</style>
