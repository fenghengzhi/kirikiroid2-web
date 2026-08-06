<script setup>
// 本地文件入口（/play/local）。
//
// 四条来源，能力依浏览器而定：
//   fsa-dir   File System Access 目录句柄（Chromium）—— 存档能直接写回硬盘
//   xp3-file  单个 .xp3
//   zip-file  整包 .zip
//   folder    <input webkitdirectory> 上传的文件夹（拷贝进内存）

import { ref } from 'vue';

const emit = defineEmits(['source', 'cancel']);

const dragging = ref(false);
const errorText = ref('');

const hasFSA = typeof window.showDirectoryPicker === 'function';
// Release 构建默认藏起 zip 按钮，?pickZip=1 可强开
const showZip = window.KrKr2Config?.localZipPicker ||
    new URLSearchParams(location.search).has('pickZip');

const fileInput = ref(null);
const dirInput = ref(null);
const zipInput = ref(null);

async function pickDirectory() {
    try {
        const handle = await window.showDirectoryPicker({ mode: 'readwrite' });
        emit('source', { type: 'fsa-dir', handle });
    } catch (err) {
        // 用户按取消不是错误
        if (err?.name !== 'AbortError') errorText.value = err.message || String(err);
    }
}

function onFile(e, type) {
    const file = e.target.files?.[0];
    e.target.value = '';
    if (file) emit('source', { type, file });
}

function onFolder(e) {
    const files = Array.from(e.target.files || []);
    e.target.value = '';
    if (files.length) emit('source', { type: 'folder', files });
}

function onDrop(e) {
    dragging.value = false;
    const file = e.dataTransfer?.files?.[0];
    if (!file) return;

    const name = file.name.toLowerCase();
    if (name.endsWith('.zip')) emit('source', { type: 'zip-file', file });
    else if (name.endsWith('.xp3')) emit('source', { type: 'xp3-file', file });
    else errorText.value = '只支持 .xp3 或 .zip 文件。';
}
</script>

<template>
    <div class="backdrop">
        <div class="panel">
            <header class="head">
                <h2>打开本地游戏</h2>
                <button class="btn btn-ghost btn-sm" @click="emit('cancel')">返回游戏库</button>
            </header>

            <div
                class="drop"
                :class="{ active: dragging }"
                @dragover.prevent="dragging = true"
                @dragleave="dragging = false"
                @drop.prevent="onDrop"
                @click="fileInput.click()">
                <svg viewBox="0 0 24 24" fill="currentColor" width="26" height="26" aria-hidden="true">
                    <path d="M10 4H4a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-8l-2-2z" />
                </svg>
                <p class="drop-title">把 .xp3 或 .zip 拖到这里</p>
                <p class="hint">也可以点击选择文件</p>
            </div>

            <p v-if="errorText" class="err">{{ errorText }}</p>

            <div class="actions">
                <button v-if="hasFSA" class="btn btn-primary" @click="pickDirectory">
                    打开游戏目录
                </button>
                <button class="btn" @click="fileInput.click()">选择 .xp3</button>
                <button class="btn" @click="dirInput.click()">上传文件夹</button>
                <button v-if="showZip" class="btn" @click="zipInput.click()">选择 .zip</button>
            </div>

            <p v-if="hasFSA" class="hint note">
                「打开游戏目录」直接读写你选定的文件夹，存档会写回原目录；
                其余方式会把文件读进浏览器，存档存在 IndexedDB。
            </p>

            <input ref="fileInput" type="file" accept=".xp3" hidden @change="onFile($event, 'xp3-file')">
            <input ref="dirInput" type="file" webkitdirectory hidden @change="onFolder">
            <input ref="zipInput" type="file" accept=".zip,application/zip,application/x-zip-compressed" hidden @change="onFile($event, 'zip-file')">
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
    background: var(--bg-0);
}

.panel {
    width: min(540px, 100%);
    padding: var(--space-5);
    border-radius: var(--radius);
    border: 1px solid var(--line);
    background: var(--bg-1);
}

.head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--space-3);
    margin-bottom: var(--space-4);
}

.head h2 { margin: 0; font-size: 16px; font-weight: 600; }

.drop {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: var(--space-1);
    padding: var(--space-6) var(--space-4);
    border: 1px dashed var(--line-strong);
    border-radius: var(--radius);
    background: var(--bg-2);
    color: var(--fg-2);
    cursor: pointer;
    transition: border-color var(--dur) var(--ease), background var(--dur) var(--ease);
}

.drop:hover, .drop.active { border-color: var(--fg-1); background: var(--bg-3); }

.drop-title { margin: var(--space-2) 0 0; font-size: 13px; color: var(--fg-0); }

.actions {
    display: flex;
    flex-wrap: wrap;
    gap: var(--space-2);
    margin-top: var(--space-4);
}

.note { margin: var(--space-3) 0 0; }

.err {
    margin: var(--space-3) 0 0;
    font-size: 12px;
    color: var(--danger);
}
</style>
