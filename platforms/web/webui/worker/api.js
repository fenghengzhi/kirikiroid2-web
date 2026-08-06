// /api/* 处理器。
//
// 三类路由：公开读（games / cover）、认证（login / logout）、
// 后台写（admin/*，逐个校验 session）。

import {
    verifyPassword, createSession, sessionCookie, clearCookie,
    isAuthed, checkRateLimit, recordFailedAttempt, clearRateLimit
} from './auth.js';
import { json, error } from './headers.js';
import * as db from './db.js';

const PUBLIC_LIST_CACHE_KEY = 'https://krkr2.internal/api/games';
const PUBLIC_LIST_MAX_AGE = 60;

/**
 * 失效公开列表缓存。任何后台写操作后都要调，
 * 否则后台改完前台最多要等 60 秒才可见。
 */
async function invalidateListCache() {
    try {
        await caches.default.delete(new Request(PUBLIC_LIST_CACHE_KEY));
    } catch {
        // 缓存失效失败不该让写操作本身报错，最坏情况是 60 秒后自然过期
    }
}

// --- 输入清洗 --------------------------------------------------------

const MAX_LENGTHS = {
    title: 200,
    coverUrl: 2000,
    downloadUrl: 2000,
    entryXp3: 260,
    description: 5000
};

function cleanString(value, max) {
    if (typeof value !== 'string') return '';
    return value.trim().slice(0, max);
}

/** 只接受 http/https。挡掉 javascript: 和 data: 落进 <img src> / fetch。 */
function cleanUrl(value, max) {
    const s = cleanString(value, max);
    if (!s) return '';
    let parsed;
    try {
        parsed = new URL(s);
    } catch {
        return '';
    }
    return (parsed.protocol === 'http:' || parsed.protocol === 'https:') ? s : '';
}

function cleanTags(value) {
    let list = value;
    if (typeof value === 'string') {
        list = value.split(/[,，]/);
    }
    if (!Array.isArray(list)) return [];
    return list
        .map((t) => (typeof t === 'string' ? t.trim().slice(0, 40) : ''))
        .filter(Boolean)
        .slice(0, 20);
}

/**
 * 把提交的对象规范成 games 表接受的形状。
 * partial=true 时（PATCH）只返回实际提交过的键。
 */
function normalizeGameInput(body, { partial = false } = {}) {
    const out = {};
    const has = (k) => Object.prototype.hasOwnProperty.call(body, k);

    if (!partial || has('title')) out.title = cleanString(body.title, MAX_LENGTHS.title);
    if (!partial || has('coverUrl')) out.coverUrl = cleanUrl(body.coverUrl, MAX_LENGTHS.coverUrl);
    if (!partial || has('downloadUrl')) out.downloadUrl = cleanUrl(body.downloadUrl, MAX_LENGTHS.downloadUrl);
    if (!partial || has('entryXp3')) out.entryXp3 = cleanString(body.entryXp3, MAX_LENGTHS.entryXp3);
    if (!partial || has('description')) out.description = cleanString(body.description, MAX_LENGTHS.description);
    if (!partial || has('tags')) out.tags = cleanTags(body.tags);
    if (has('pinned')) out.pinned = !!body.pinned;
    if (has('published')) out.published = !!body.published;
    if (has('sortOrder') && Number.isFinite(body.sortOrder)) out.sortOrder = Math.trunc(body.sortOrder);

    return out;
}

function newId() {
    return `game_${Date.now()}_${crypto.randomUUID().slice(0, 8)}`;
}

// --- 公开读 ----------------------------------------------------------

async function handlePublicList(request, env, ctx) {
    const cache = caches.default;
    const cacheKey = new Request(PUBLIC_LIST_CACHE_KEY);

    const hit = await cache.match(cacheKey);
    if (hit) return hit;

    const games = await db.listPublishedGames(env.DB);
    const response = json(
        { games },
        { headers: { 'Cache-Control': `public, max-age=${PUBLIC_LIST_MAX_AGE}` } }
    );

    ctx.waitUntil(cache.put(cacheKey, response.clone()));
    return response;
}

async function handlePublicGame(request, env, id) {
    const game = await db.getGame(env.DB, id, { publishedOnly: true });
    if (!game) return error(404, 'Game not found');
    return json({ game });
}

/**
 * 封面代理。
 *
 * 为什么需要：COEP require-corp 下，第三方图床不发
 * Cross-Origin-Resource-Policy 头，<img> 一律加载失败。
 *
 * 为什么不是开放代理：目标地址只从 D1 按 id 读，不接受任何 URL 参数，
 * 调用方无法指定回源目标，因此没有 SSRF 面。
 */
async function handleCover(request, env, ctx, id) {
    const cache = caches.default;
    const cacheKey = new Request(new URL(request.url).toString());

    const hit = await cache.match(cacheKey);
    if (hit) return hit;

    const coverUrl = await db.getCoverUrl(env.DB, id);
    if (!coverUrl) return error(404, 'No cover');

    let upstream;
    try {
        upstream = await fetch(coverUrl, {
            headers: { Accept: 'image/*' },
            redirect: 'follow',
            cf: { cacheTtl: 86400, cacheEverything: true }
        });
    } catch {
        return error(502, 'Cover fetch failed');
    }

    if (!upstream.ok) return error(502, `Cover upstream ${upstream.status}`);

    const contentType = upstream.headers.get('Content-Type') || '';
    if (!contentType.startsWith('image/')) return error(415, 'Not an image');

    const declaredSize = parseInt(upstream.headers.get('Content-Length') || '0', 10);
    if (declaredSize > 5 * 1024 * 1024) return error(413, 'Cover too large');

    const body = await upstream.arrayBuffer();
    if (body.byteLength > 5 * 1024 * 1024) return error(413, 'Cover too large');

    const response = new Response(body, {
        headers: {
            'Content-Type': contentType,
            'Cache-Control': 'public, max-age=86400',
            // 关键：让 COEP 页面能加载这张图
            'Cross-Origin-Resource-Policy': 'cross-origin'
        }
    });

    ctx.waitUntil(cache.put(cacheKey, response.clone()));
    return response;
}

// --- 认证 ------------------------------------------------------------

async function handleLogin(request, env) {
    if (!env.ADMIN_PASSWORD_HASH || !env.SESSION_SECRET) {
        return error(503, '后台未配置：请设置 ADMIN_PASSWORD_HASH 与 SESSION_SECRET');
    }

    const ip = request.headers.get('CF-Connecting-IP') || 'unknown';
    const rate = await checkRateLimit(env, ip);
    if (!rate.allowed) {
        return error(429, '尝试次数过多，请稍后再试', {
            headers: { 'Retry-After': String(rate.retryAfter) }
        });
    }

    let body;
    try {
        body = await request.json();
    } catch {
        return error(400, 'Invalid JSON');
    }

    const password = typeof body?.password === 'string' ? body.password : '';
    if (!password) {
        await recordFailedAttempt(env, ip);
        return error(400, '请输入密码');
    }

    if (!(await verifyPassword(password, env.ADMIN_PASSWORD_HASH))) {
        await recordFailedAttempt(env, ip);
        // 不透露剩余次数的精确语义，只提示还能再试几次
        return error(401, `密码错误（剩余 ${Math.max(0, rate.remaining - 1)} 次尝试）`);
    }

    await clearRateLimit(env, ip);
    const token = await createSession(env.SESSION_SECRET);
    return json({ ok: true }, { headers: { 'Set-Cookie': sessionCookie(token) } });
}

function handleLogout() {
    return json({ ok: true }, { headers: { 'Set-Cookie': clearCookie() } });
}

// --- 后台写 ----------------------------------------------------------

async function handleAdmin(request, env, ctx, segments) {
    if (!(await isAuthed(request, env))) return error(401, '未登录');

    const [resource, id] = segments;
    const method = request.method;

    if (resource === 'me') return json({ ok: true });

    if (resource === 'games') {
        if (method === 'GET' && !id) {
            return json({ games: await db.listAllGames(env.DB) });
        }

        if (method === 'POST' && !id) {
            let body;
            try {
                body = await request.json();
            } catch {
                return error(400, 'Invalid JSON');
            }
            const input = normalizeGameInput(body);
            if (!input.title) return error(400, '游戏名称不能为空');

            const game = await db.insertGame(env.DB, {
                id: newId(),
                pinned: false,
                published: true,
                ...input
            });
            await invalidateListCache();
            return json({ game }, { status: 201 });
        }

        if (method === 'PATCH' && id) {
            let body;
            try {
                body = await request.json();
            } catch {
                return error(400, 'Invalid JSON');
            }
            const existing = await db.getGame(env.DB, id);
            if (!existing) return error(404, 'Game not found');

            const input = normalizeGameInput(body, { partial: true });
            if (input.title !== undefined && !input.title) {
                return error(400, '游戏名称不能为空');
            }

            const game = await db.updateGame(env.DB, id, input);
            await invalidateListCache();
            return json({ game });
        }

        if (method === 'DELETE' && id) {
            const removed = await db.deleteGame(env.DB, id);
            if (!removed) return error(404, 'Game not found');
            await invalidateListCache();
            return json({ ok: true });
        }
    }

    if (resource === 'reorder' && method === 'POST') {
        let body;
        try {
            body = await request.json();
        } catch {
            return error(400, 'Invalid JSON');
        }
        const ids = Array.isArray(body?.ids) ? body.ids.filter((v) => typeof v === 'string') : null;
        if (!ids || ids.length === 0) return error(400, 'ids 不能为空');

        await db.reorderGames(env.DB, ids);
        await invalidateListCache();
        return json({ ok: true });
    }

    if (resource === 'import' && method === 'POST') {
        let body;
        try {
            body = await request.json();
        } catch {
            return error(400, 'Invalid JSON');
        }
        const raw = Array.isArray(body) ? body : body?.games;
        if (!Array.isArray(raw)) return error(400, '需要一个 JSON 数组');
        if (raw.length > 500) return error(413, '一次最多导入 500 条');

        const games = raw
            .map((item) => {
                const input = normalizeGameInput(item || {});
                if (!input.title) return null;
                // 沿用原 id 便于反复导入不产生重复；缺失才新生成
                const id = typeof item?.id === 'string' && item.id.trim()
                    ? item.id.trim().slice(0, 120)
                    : newId();
                return { id, ...input };
            })
            .filter(Boolean);

        if (games.length === 0) return error(400, '没有可导入的有效条目');

        const count = await db.importGames(env.DB, games);
        await invalidateListCache();
        return json({ ok: true, imported: count });
    }

    return error(404, 'Unknown admin endpoint');
}

// --- 入口 ------------------------------------------------------------

export async function handleApi(request, env, ctx, pathname) {
    // '/api/admin/games/x' -> ['admin', 'games', 'x']
    const segments = pathname.replace(/^\/api\/?/, '').split('/').filter(Boolean);
    const [head, ...rest] = segments;
    const method = request.method;

    if (head === 'games') {
        if (method !== 'GET') return error(405, 'Method not allowed');
        return rest[0]
            ? handlePublicGame(request, env, rest[0])
            : handlePublicList(request, env, ctx);
    }

    if (head === 'cover' && rest[0]) {
        if (method !== 'GET') return error(405, 'Method not allowed');
        return handleCover(request, env, ctx, rest[0]);
    }

    if (head === 'auth') {
        if (rest[0] === 'login' && method === 'POST') return handleLogin(request, env);
        if (rest[0] === 'logout' && method === 'POST') return handleLogout();
        return error(404, 'Unknown auth endpoint');
    }

    if (head === 'admin') return handleAdmin(request, env, ctx, rest);

    return error(404, 'Unknown endpoint');
}
