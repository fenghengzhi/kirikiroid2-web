<script setup>
// 后台入口壳。
//
// 挂载后先探测 /api/admin/me：未登录只渲染登录框，登录成功才 import()
// 后台主体 —— Vite 会把它切成独立 chunk，未登录者根本不会下载那段代码。
// 真正的安全边界在 Worker 的 session 校验，前端这层只是不做无用的下发。

import { ref, shallowRef, onMounted } from 'vue';
import { api } from '../shared/api.js';

const checking = ref(true);
const authed = ref(false);
const password = ref('');
const submitting = ref(false);
const errorText = ref('');

// shallowRef：装的是组件定义，不需要深响应
const AdminPanel = shallowRef(null);

async function loadPanel() {
    if (AdminPanel.value) return;
    const mod = await import('./GamesAdmin.vue');
    AdminPanel.value = mod.default;
}

async function submit() {
    if (submitting.value) return;
    errorText.value = '';
    submitting.value = true;
    try {
        await api.login(password.value);
        password.value = '';
        authed.value = true;
        await loadPanel();
    } catch (err) {
        errorText.value = err.message || '登录失败';
    } finally {
        submitting.value = false;
    }
}

async function onLogout() {
    try {
        await api.logout();
    } finally {
        authed.value = false;
    }
}

onMounted(async () => {
    try {
        authed.value = await api.checkAuth();
        if (authed.value) await loadPanel();
    } catch (err) {
        errorText.value = err.message || '';
    } finally {
        checking.value = false;
    }
});
</script>

<template>
    <div v-if="checking" class="center-screen"><span class="spinner" /></div>

    <component
        v-else-if="authed && AdminPanel"
        :is="AdminPanel"
        @logout="onLogout" />

    <div v-else class="center-screen">
        <form class="login" @submit.prevent="submit">
            <h1>管理后台</h1>
            <p class="hint">请输入管理密码。</p>

            <div class="field">
                <label for="pw">密码</label>
                <input
                    id="pw"
                    v-model="password"
                    class="input"
                    type="password"
                    autocomplete="current-password"
                    required
                    autofocus>
            </div>

            <p v-if="errorText" class="err">{{ errorText }}</p>

            <button class="btn btn-primary full" type="submit" :disabled="submitting || !password">
                <span v-if="submitting" class="spinner sm" />
                {{ submitting ? '验证中…' : '登录' }}
            </button>

            <a class="back" href="/">← 返回游戏库</a>
        </form>
    </div>
</template>

<style scoped>
.center-screen {
    min-height: 100vh;
    display: grid;
    place-items: center;
    padding: var(--space-4);
}

.login {
    width: min(340px, 100%);
    display: flex;
    flex-direction: column;
    gap: var(--space-4);
    padding: var(--space-5);
    border-radius: var(--radius);
    border: 1px solid var(--line);
    background: var(--bg-1);
}

.login h1 { margin: 0; font-size: 18px; font-weight: 600; }
.login .hint { margin: -8px 0 0; }

.full { width: 100%; justify-content: center; }

.err { margin: 0; font-size: 12px; color: var(--danger); line-height: 1.5; }

.back {
    text-align: center;
    font-size: 12px;
    color: var(--fg-2);
}

.back:hover { color: var(--fg-0); }

.spinner.sm { width: 13px; height: 13px; border-width: 2px; }
</style>
