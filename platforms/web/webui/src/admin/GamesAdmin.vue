<script setup>
import { ref, computed, onMounted } from 'vue';
import { api, coverSrc } from '../shared/api.js';

const emit = defineEmits(['logout']);

const games = ref([]);
const loading = ref(true);
const status = ref('');
const busy = ref(false);

// 编辑抽屉
const editing = ref(false);
const editId = ref(null);        // null = 新增
const form = ref(blankForm());
const formError = ref('');

const importInput = ref(null);
const dragIndex = ref(null);

function blankForm() {
    return { title: '', coverUrl: '', downloadUrl: '', entryXp3: '', description: '', tags: '' };
}

const isNew = computed(() => editId.value === null);

function flash(msg) {
    status.value = msg;
    setTimeout(() => { if (status.value === msg) status.value = ''; }, 3000);
}

async function refresh() {
    loading.value = true;
    try {
        games.value = await api.adminListGames();
    } catch (err) {
        flash('加载失败: ' + err.message);
    } finally {
        loading.value = false;
    }
}

function openNew() {
    editId.value = null;
    form.value = blankForm();
    formError.value = '';
    editing.value = true;
}

function openEdit(g) {
    editId.value = g.id;
    form.value = {
        title: g.title,
        coverUrl: g.coverUrl,
        downloadUrl: g.downloadUrl,
        entryXp3: g.entryXp3,
        description: g.description,
        tags: g.tags.join(', ')
    };
    formError.value = '';
    editing.value = true;
}

function closeEdit() {
    editing.value = false;
    formError.value = '';
}

async function save() {
    if (!form.value.title.trim()) {
        formError.value = '游戏名称不能为空';
        return;
    }
    busy.value = true;
    formError.value = '';

    const payload = {
        title: form.value.title.trim(),
        coverUrl: form.value.coverUrl.trim(),
        downloadUrl: form.value.downloadUrl.trim(),
        entryXp3: form.value.entryXp3.trim(),
        description: form.value.description.trim(),
        tags: form.value.tags
    };

    try {
        if (isNew.value) {
            const created = await api.createGame(payload);
            games.value.push(created);
            flash('已添加');
        } else {
            const updated = await api.updateGame(editId.value, payload);
            const i = games.value.findIndex((g) => g.id === editId.value);
            if (i !== -1) games.value[i] = updated;
            flash('已保存');
        }
        editing.value = false;
        await refresh();   // 重新拉一次以拿到服务端排序
    } catch (err) {
        formError.value = err.message || '保存失败';
    } finally {
        busy.value = false;
    }
}

async function remove(g) {
    if (!confirm(`删除「${g.title}」？此操作不可撤销。\n（玩家已有的存档不会被删除）`)) return;
    try {
        await api.deleteGame(g.id);
        games.value = games.value.filter((x) => x.id !== g.id);
        flash('已删除');
    } catch (err) {
        flash('删除失败: ' + err.message);
    }
}

/** 置顶 / 上下架：本地先改以获得即时反馈，失败再回滚。 */
async function toggleFlag(g, key) {
    const prev = g[key];
    g[key] = !prev;
    try {
        await api.updateGame(g.id, { [key]: g[key] });
        flash(key === 'pinned' ? (g.pinned ? '已置顶' : '已取消置顶')
                               : (g.published ? '已上架' : '已下架'));
    } catch (err) {
        g[key] = prev;
        flash('操作失败: ' + err.message);
    }
}

// --- 拖拽排序 -------------------------------------------------------

function onDragStart(index) { dragIndex.value = index; }

function onDragOver(index) {
    if (dragIndex.value === null || dragIndex.value === index) return;
    const list = games.value;
    const [moved] = list.splice(dragIndex.value, 1);
    list.splice(index, 0, moved);
    dragIndex.value = index;
}

async function onDragEnd() {
    dragIndex.value = null;
    try {
        await api.reorderGames(games.value.map((g) => g.id));
        flash('顺序已保存');
    } catch (err) {
        flash('排序保存失败: ' + err.message);
        refresh();
    }
}

// --- 导入 / 导出 -----------------------------------------------------

async function onImport(e) {
    const file = e.target.files?.[0];
    e.target.value = '';
    if (!file) return;

    try {
        const parsed = JSON.parse(await file.text());
        const list = Array.isArray(parsed) ? parsed : parsed.games;
        if (!Array.isArray(list)) throw new Error('文件内容不是数组');

        const res = await api.importGames(list);
        flash(`已导入 ${res.imported} 条`);
        refresh();
    } catch (err) {
        flash('导入失败: ' + err.message);
    }
}

function exportJson() {
    const data = games.value.map((g) => ({
        id: g.id,
        title: g.title,
        coverUrl: g.coverUrl,
        downloadUrl: g.downloadUrl,
        entryXp3: g.entryXp3,
        description: g.description,
        tags: g.tags
    }));
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'games.json';
    a.click();
    URL.revokeObjectURL(a.href);
}

onMounted(refresh);
</script>

<template>
    <header class="nav">
        <div class="nav-left">
            <a class="btn btn-ghost btn-sm" href="/">← 游戏库</a>
            <h1>管理后台</h1>
        </div>
        <div class="nav-right">
            <span class="hint">{{ status }}</span>
            <button class="btn btn-ghost btn-sm" @click="emit('logout')">退出登录</button>
        </div>
    </header>

    <main class="body">
        <div class="toolbar">
            <div class="toolbar-left">
                <button class="btn btn-primary btn-sm" @click="openNew">+ 新增游戏</button>
                <span class="hint">共 {{ games.length }} 个条目，拖动行可调整顺序</span>
            </div>
            <div class="toolbar-right">
                <button class="btn btn-sm" @click="importInput.click()">导入 JSON</button>
                <button class="btn btn-sm" @click="exportJson" :disabled="!games.length">导出 JSON</button>
                <input ref="importInput" type="file" accept=".json,application/json" hidden @change="onImport">
            </div>
        </div>

        <div v-if="loading" class="center pad"><span class="spinner" /></div>

        <div v-else-if="games.length === 0" class="empty">
            <h2>还没有游戏</h2>
            <p>点击「新增游戏」手动添加，或导入一份 games.json。</p>
            <button class="btn btn-primary" @click="openNew">新增第一个游戏</button>
        </div>

        <table v-else class="table">
            <thead>
                <tr>
                    <th class="col-drag" />
                    <th class="col-cover">封面</th>
                    <th>标题</th>
                    <th class="col-url">资源地址</th>
                    <th class="col-state">状态</th>
                    <th class="col-actions" />
                </tr>
            </thead>
            <tbody>
                <tr
                    v-for="(g, i) in games"
                    :key="g.id"
                    draggable="true"
                    :class="{ dragging: dragIndex === i, unpublished: !g.published }"
                    @dragstart="onDragStart(i)"
                    @dragover.prevent="onDragOver(i)"
                    @dragend="onDragEnd">
                    <td class="col-drag" title="拖动排序">⠿</td>

                    <td>
                        <img v-if="g.coverUrl" :src="coverSrc(g)" class="thumb" alt="" loading="lazy">
                        <div v-else class="thumb thumb-empty">—</div>
                    </td>

                    <td>
                        <div class="title">{{ g.title }}</div>
                        <div v-if="g.entryXp3" class="sub">入口: {{ g.entryXp3 }}</div>
                        <div v-if="g.tags.length" class="tags">
                            <span v-for="t in g.tags" :key="t" class="tag">{{ t }}</span>
                        </div>
                    </td>

                    <td class="col-url">
                        <span v-if="g.downloadUrl" class="url" :title="g.downloadUrl">{{ g.downloadUrl }}</span>
                        <span v-else class="warn">未设置</span>
                    </td>

                    <td class="col-state">
                        <button
                            class="chip"
                            :class="{ on: g.pinned }"
                            @click="toggleFlag(g, 'pinned')">
                            {{ g.pinned ? '已置顶' : '置顶' }}
                        </button>
                        <button
                            class="chip"
                            :class="{ on: g.published }"
                            @click="toggleFlag(g, 'published')">
                            {{ g.published ? '已上架' : '已下架' }}
                        </button>
                    </td>

                    <td class="col-actions">
                        <button class="btn btn-sm" @click="openEdit(g)">编辑</button>
                        <button class="btn btn-sm btn-danger" @click="remove(g)">删除</button>
                    </td>
                </tr>
            </tbody>
        </table>
    </main>

    <!-- 编辑抽屉 -->
    <div v-if="editing" class="drawer-backdrop" @click.self="closeEdit">
        <div class="drawer">
            <header class="drawer-head">
                <h2>{{ isNew ? '新增游戏' : '编辑游戏' }}</h2>
                <button class="btn btn-ghost btn-sm" @click="closeEdit">关闭</button>
            </header>

            <div class="drawer-body">
                <div class="field">
                    <label for="f-title">游戏名称 *</label>
                    <input id="f-title" v-model="form.title" class="input" placeholder="显示在游戏库里的名称">
                </div>

                <div class="field">
                    <label for="f-url">资源地址</label>
                    <input id="f-url" v-model="form.downloadUrl" class="input" placeholder="https://example.com/manifest.json">
                    <p class="hint">
                        支持三种：<code>.json</code> 清单（散装文件，推荐大游戏）、
                        <code>.zip</code> 整包、<code>.xp3</code> 单档案。
                        需可公开访问且支持 HTTP Range。
                    </p>
                </div>

                <div class="field">
                    <label for="f-cover">封面图片 URL</label>
                    <input id="f-cover" v-model="form.coverUrl" class="input" placeholder="https://example.com/cover.jpg">
                    <p class="hint">图片会经本站代理加载，任意图床都可用。</p>
                </div>

                <div class="field">
                    <label for="f-entry">启动 XP3 文件名</label>
                    <input id="f-entry" v-model="form.entryXp3" class="input" placeholder="data.xp3（留空自动识别）">
                </div>

                <div class="field">
                    <label for="f-tags">标签</label>
                    <input id="f-tags" v-model="form.tags" class="input" placeholder="ADV, 恋爱, 汉化版">
                    <p class="hint">用逗号分隔。</p>
                </div>

                <div class="field">
                    <label for="f-desc">简介</label>
                    <textarea id="f-desc" v-model="form.description" class="input" rows="5" placeholder="游戏的简要介绍…" />
                </div>

                <p v-if="formError" class="err">{{ formError }}</p>
            </div>

            <footer class="drawer-foot">
                <button class="btn" @click="closeEdit">取消</button>
                <button class="btn btn-primary" :disabled="busy" @click="save">
                    {{ busy ? '保存中…' : (isNew ? '添加' : '保存') }}
                </button>
            </footer>
        </div>
    </div>
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
    background: rgba(10, 10, 11, 0.85);
    backdrop-filter: blur(16px);
    border-bottom: 1px solid var(--line);
}

.nav-left, .nav-right { display: flex; align-items: center; gap: var(--space-3); }
.nav h1 { margin: 0; font-size: 14px; font-weight: 600; }

.body { max-width: 1200px; margin: 0 auto; padding: var(--space-5); }

.toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-3);
    flex-wrap: wrap;
    margin-bottom: var(--space-4);
}

.toolbar-left, .toolbar-right { display: flex; align-items: center; gap: var(--space-2); }

.center { display: grid; place-items: center; }
.pad { padding: var(--space-7) 0; }

.table {
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
}

.table th {
    padding: var(--space-2) var(--space-3);
    text-align: left;
    font-size: 11px;
    font-weight: 500;
    color: var(--fg-2);
    border-bottom: 1px solid var(--line);
    white-space: nowrap;
}

.table td {
    padding: var(--space-3);
    border-bottom: 1px solid var(--line);
    vertical-align: middle;
}

.table tbody tr { transition: background var(--dur) var(--ease); }
.table tbody tr:hover { background: var(--bg-1); }
.table tbody tr.dragging { opacity: 0.45; }
.table tbody tr.unpublished { opacity: 0.55; }

.col-drag {
    width: 28px;
    cursor: grab;
    color: var(--fg-2);
    user-select: none;
    text-align: center;
}

.col-cover { width: 52px; }
.col-url { max-width: 240px; }
.col-state { width: 150px; }
.col-actions { width: 130px; text-align: right; white-space: nowrap; }
.col-actions .btn { margin-left: var(--space-1); }

.thumb {
    width: 40px;
    height: 53px;
    object-fit: cover;
    border-radius: 4px;
    border: 1px solid var(--line);
    background: var(--bg-2);
    display: block;
}

.thumb-empty {
    display: grid;
    place-items: center;
    color: var(--fg-2);
    font-size: 11px;
}

.title { font-weight: 500; }
.sub { margin-top: 2px; font-size: 11px; color: var(--fg-2); }
.tags { display: flex; flex-wrap: wrap; gap: 3px; margin-top: 5px; }

.url {
    display: block;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    color: var(--fg-1);
    font-size: 12px;
}

.warn { color: var(--danger); font-size: 12px; }

.chip {
    padding: 3px 9px;
    margin-right: 4px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: var(--bg-2);
    font-size: 11px;
    color: var(--fg-2);
    transition: all var(--dur) var(--ease);
}

.chip:hover { border-color: var(--line-strong); color: var(--fg-0); }
.chip.on { background: var(--bg-3); color: var(--fg-0); border-color: var(--line-strong); }

.empty {
    text-align: center;
    padding: var(--space-7) var(--space-4);
    border: 1px dashed var(--line);
    border-radius: var(--radius-lg);
    background: var(--bg-1);
}

.empty h2 { margin: 0 0 var(--space-2); font-size: 16px; font-weight: 600; }
.empty p { margin: 0 0 var(--space-4); font-size: 13px; color: var(--fg-1); }

.drawer-backdrop {
    position: fixed;
    inset: 0;
    z-index: var(--z-modal);
    display: flex;
    justify-content: flex-end;
    background: rgba(0, 0, 0, 0.6);
    backdrop-filter: blur(6px);
}

.drawer {
    width: min(460px, 100%);
    height: 100%;
    display: flex;
    flex-direction: column;
    background: var(--bg-1);
    border-left: 1px solid var(--line);
    box-shadow: var(--shadow-lg);
    animation: slide-in var(--dur) var(--ease);
}

@keyframes slide-in { from { transform: translateX(100%); } }

.drawer-head, .drawer-foot {
    display: flex;
    align-items: center;
    gap: var(--space-2);
    padding: var(--space-4) var(--space-5);
    flex-shrink: 0;
}

.drawer-head { justify-content: space-between; border-bottom: 1px solid var(--line); }
.drawer-head h2 { margin: 0; font-size: 15px; font-weight: 600; }

.drawer-body {
    flex: 1;
    overflow-y: auto;
    padding: var(--space-5);
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
}

.drawer-body code {
    padding: 1px 4px;
    border-radius: 3px;
    background: var(--bg-2);
    border: 1px solid var(--line);
    font-size: 11px;
}

.drawer-foot { justify-content: flex-end; border-top: 1px solid var(--line); }

.err { margin: 0; font-size: 12px; color: var(--danger); }

@media (max-width: 860px) {
    .col-url { display: none; }
}

@media (max-width: 640px) {
    .body { padding: var(--space-4); }
    .col-state { display: none; }
}
</style>
