<script setup>
// 存档空间面板。
//
// 存储逻辑全部复用 public/js/storage/idb.js 暴露的 window.KrKr2IDB，
// 这一层只做展示 —— 不重写存储实现。

import { ref, onMounted } from 'vue';

const emit = defineEmits(['close']);

const spaces = ref([]);
const loading = ref(true);
const status = ref('');
const importInput = ref(null);

const IDB = () => window.KrKr2IDB;

function formatSize(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(1) + ' MB';
}

async function refresh() {
    loading.value = true;
    try {
        const names = await IDB().listSpaces();
        // 先把列表渲染出来，每个空间的体积再逐个异步填充，
        // 避免空间多时白屏等待
        spaces.value = names.map((name) => ({ name, count: null, size: null }));
        loading.value = false;

        await Promise.all(spaces.value.map(async (entry) => {
            const info = await IDB().getSpaceInfo(entry.name);
            entry.count = info.count;
            entry.size = info.size;
        }));
    } catch (err) {
        status.value = '读取失败: ' + err.message;
        loading.value = false;
    }
}

async function exportSpace(name) {
    status.value = '正在导出…';
    try {
        await IDB().exportZip(name);
        status.value = '已导出 ' + name;
    } catch (err) {
        status.value = '导出失败: ' + err.message;
    }
}

async function removeSpace(name) {
    if (!confirm(`删除存档空间「${name}」？此操作不可撤销。`)) return;
    await IDB().deleteSpace(name);
    status.value = '已删除 ' + name;
    refresh();
}

async function onImport(e) {
    const file = e.target.files?.[0];
    e.target.value = '';
    if (!file) return;

    status.value = '正在导入…';
    try {
        const name = await IDB().importZip(file);
        status.value = `已导入「${name}」`;
        refresh();
    } catch (err) {
        status.value = '导入失败: ' + err.message;
    }
}

onMounted(refresh);
</script>

<template>
    <div class="backdrop" @click.self="emit('close')">
        <div class="panel">
            <header class="head">
                <div>
                    <h2>存档空间</h2>
                    <p class="hint">每个空间是一个独立的浏览器数据库，按游戏自动分配。</p>
                </div>
                <button class="btn btn-ghost btn-sm" @click="emit('close')">关闭</button>
            </header>

            <div v-if="loading" class="center"><span class="spinner" /></div>

            <p v-else-if="spaces.length === 0" class="hint center pad">
                还没有存档空间。开始游戏并存档后会自动创建。
            </p>

            <ul v-else class="list">
                <li v-for="s in spaces" :key="s.name" class="row">
                    <div class="info">
                        <span class="name">{{ s.name }}</span>
                        <span class="meta">
                            <template v-if="s.count === null">读取中…</template>
                            <template v-else>{{ s.count }} 个文件 · {{ formatSize(s.size) }}</template>
                        </span>
                    </div>
                    <div class="row-actions">
                        <button class="btn btn-sm" @click="exportSpace(s.name)">导出</button>
                        <button class="btn btn-sm btn-danger" @click="removeSpace(s.name)">删除</button>
                    </div>
                </li>
            </ul>

            <footer class="foot">
                <button class="btn btn-sm" @click="importInput.click()">导入存档 ZIP</button>
                <span class="hint">{{ status }}</span>
                <input ref="importInput" type="file" accept=".zip" hidden @change="onImport">
            </footer>
        </div>
    </div>
</template>

<style scoped>
.backdrop {
    position: fixed;
    inset: 0;
    z-index: var(--z-modal);
    display: grid;
    place-items: center;
    padding: var(--space-4);
    background: rgba(0, 0, 0, 0.72);
    backdrop-filter: blur(8px);
}

.panel {
    width: min(520px, 100%);
    max-height: 82vh;
    display: flex;
    flex-direction: column;
    padding: var(--space-5);
    border-radius: var(--radius);
    border: 1px solid var(--line);
    background: var(--bg-1);
    box-shadow: var(--shadow-lg);
}

.head {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: var(--space-3);
    margin-bottom: var(--space-4);
}

.head h2 { margin: 0 0 4px; font-size: 16px; font-weight: 600; }

.center { display: grid; place-items: center; }
.pad { padding: var(--space-6) 0; text-align: center; }

.list {
    list-style: none;
    margin: 0;
    padding: 0;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: var(--space-2);
}

.row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-3);
    padding: var(--space-3);
    border-radius: var(--radius-sm);
    border: 1px solid var(--line);
    background: var(--bg-2);
}

.info { min-width: 0; display: flex; flex-direction: column; gap: 2px; }

.name {
    font-size: 13px;
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.meta { font-size: 11px; color: var(--fg-2); }

.row-actions { display: flex; gap: var(--space-1); flex-shrink: 0; }

.foot {
    display: flex;
    align-items: center;
    gap: var(--space-3);
    margin-top: var(--space-4);
    padding-top: var(--space-4);
    border-top: 1px solid var(--line);
}
</style>
