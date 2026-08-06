// 前端 API 封装。所有请求走同源 /api，靠 cookie 携带 session。

async function request(path, options = {}) {
    const response = await fetch(path, {
        // 同源请求默认就带 cookie，显式写出以防将来改动
        credentials: 'same-origin',
        headers: options.body ? { 'Content-Type': 'application/json' } : undefined,
        ...options
    });

    let data = null;
    try {
        data = await response.json();
    } catch {
        // 204 或非 JSON 响应
    }

    if (!response.ok) {
        const err = new Error(data?.error || `HTTP ${response.status}`);
        err.status = response.status;
        throw err;
    }
    return data;
}

const body = (data) => ({ body: JSON.stringify(data) });

export const api = {
    // --- 公开 ---
    listGames: () => request('/api/games').then((d) => d.games || []),
    getGame: (id) => request(`/api/games/${encodeURIComponent(id)}`).then((d) => d.game),

    // --- 认证 ---
    login: (password) => request('/api/auth/login', { method: 'POST', ...body({ password }) }),
    logout: () => request('/api/auth/logout', { method: 'POST' }),

    /** 探测登录态。401 返回 false 而不抛，调用方据此决定渲染登录框还是后台。 */
    async checkAuth() {
        try {
            await request('/api/admin/me');
            return true;
        } catch (err) {
            if (err.status === 401) return false;
            throw err;
        }
    },

    // --- 后台 ---
    adminListGames: () => request('/api/admin/games').then((d) => d.games || []),
    createGame: (game) => request('/api/admin/games', { method: 'POST', ...body(game) }).then((d) => d.game),
    updateGame: (id, fields) =>
        request(`/api/admin/games/${encodeURIComponent(id)}`, { method: 'PATCH', ...body(fields) })
            .then((d) => d.game),
    deleteGame: (id) =>
        request(`/api/admin/games/${encodeURIComponent(id)}`, { method: 'DELETE' }),
    reorderGames: (ids) => request('/api/admin/reorder', { method: 'POST', ...body({ ids }) }),
    importGames: (games) => request('/api/admin/import', { method: 'POST', ...body({ games }) })
};

/**
 * 封面地址。
 *
 * 一律走 /api/cover/<id> 而不是直接用 coverUrl：COEP require-corp 下，
 * 第三方图床不发 Cross-Origin-Resource-Policy 头，<img> 会直接加载失败。
 * Worker 侧按 id 从 D1 取真实地址回源并补上该头。
 */
export function coverSrc(game) {
    if (!game?.coverUrl) return null;
    return `/api/cover/${encodeURIComponent(game.id)}`;
}
