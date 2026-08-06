#!/usr/bin/env node
// 构建后生成 dist/sw.js。
//
// 取代旧 sw.js 里手写的 PRECACHE_ASSETS 列表 —— Vite 产物带内容哈希，
// 手写列表一上线就全部 404，离线直接挂掉。
//
// 同时修掉旧 SW 的两个真实缺陷：
//   1. 旧逻辑对所有同源请求 cache-first，会把 /api/* 响应永久缓存；
//   2. 导航离线回退写死 './index.html'，在 /play/<id> 下解析成
//      /play/index.html 而落空。

import { readFileSync, writeFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('..', import.meta.url));
const dist = join(root, 'dist');

// 超过这个大小就不进 precache：装 SW 时会一次性下载全部 precache 资源，
// 把 22MB 的 wasm 放进去会让首次访问卡住很久。它走运行时缓存。
const PRECACHE_MAX_BYTES = 2 * 1024 * 1024;

// 这些即便小也不预缓存
const PRECACHE_EXCLUDE = [/^sw\.js$/, /^_headers$/, /\.map$/, /^\.vite\//];

// 静态资源单文件上限（Cloudflare Workers），逼近时提前报错而不是等部署失败
const ASSET_SIZE_LIMIT = 25 * 1024 * 1024;
const ASSET_SIZE_WARN = 24 * 1024 * 1024;

function walk(dir, out = []) {
    for (const name of readdirSync(dir)) {
        const full = join(dir, name);
        const st = statSync(full);
        if (st.isDirectory()) walk(full, out);
        else out.push({ path: full, size: st.size });
    }
    return out;
}

const files = walk(dist).map((f) => ({
    ...f,
    rel: relative(dist, f.path).split(sep).join('/')
}));

// --- 尺寸守卫 --------------------------------------------------------

let oversized = false;
for (const f of files) {
    if (f.size > ASSET_SIZE_LIMIT) {
        console.error(
            `✗ ${f.rel} 为 ${(f.size / 1048576).toFixed(2)} MiB，` +
            `超过 Cloudflare 静态资源 25 MiB 单文件上限。\n` +
            `  把它挪到 R2 并在 build-config.js 里改 assetBase。`
        );
        oversized = true;
    } else if (f.size > ASSET_SIZE_WARN) {
        console.warn(
            `⚠ ${f.rel} 为 ${(f.size / 1048576).toFixed(2)} MiB，` +
            `已逼近 25 MiB 上限，建议尽快迁到 R2。`
        );
    }
}
if (oversized) process.exit(1);

// --- precache 列表 ---------------------------------------------------

const precache = files
    .filter((f) => f.size <= PRECACHE_MAX_BYTES)
    .filter((f) => !PRECACHE_EXCLUDE.some((re) => re.test(f.rel)))
    .map((f) => '/' + f.rel)
    .sort();

// 三个入口的根路径也要能离线打开
precache.unshift('/');

// 版本号取全部产物内容的哈希：内容没变就不会触发 SW 更新
const hash = createHash('sha256');
for (const f of files.sort((a, b) => a.rel.localeCompare(b.rel))) {
    hash.update(f.rel);
    hash.update(readFileSync(f.path));
}
const version = hash.digest('hex').slice(0, 12);

const sw = `/* 自动生成 —— 请勿手改。源码见 scripts/gen-sw.js */
var CACHE_VERSION = '${version}';
var CACHE_NAME = 'krkr2-v' + CACHE_VERSION;

var PRECACHE_ASSETS = ${JSON.stringify(precache, null, 4)};

/* 永不走缓存的路径：API 响应是动态的，后台页面涉及鉴权状态。
 * 旧版本对所有同源请求一律 cache-first，会把 /api/games 的结果
 * 永久缓存，后台改了数据前台永远看不到。 */
function isNeverCached(url) {
    return url.pathname === '/api' ||
           url.pathname.indexOf('/api/') === 0 ||
           url.pathname === '/admin' ||
           url.pathname.indexOf('/admin/') === 0;
}

self.addEventListener('install', function (event) {
    event.waitUntil(
        caches.open(CACHE_NAME).then(function (cache) {
            /* 逐个 add，单个失败不拖垮整批 —— addAll 是全有或全无，
             * 一个资源 404 会导致整个 SW 安装失败、离线能力全失。 */
            return Promise.all(PRECACHE_ASSETS.map(function (asset) {
                return cache.add(asset).catch(function (err) {
                    console.warn('[SW] precache skipped:', asset, err);
                });
            }));
        }).then(function () {
            return self.skipWaiting();
        })
    );
});

self.addEventListener('activate', function (event) {
    event.waitUntil(
        caches.keys().then(function (names) {
            return Promise.all(names.filter(function (n) {
                return n.indexOf('krkr2-v') === 0 && n !== CACHE_NAME;
            }).map(function (n) {
                return caches.delete(n);
            }));
        }).then(function () {
            return self.clients.claim();
        })
    );
});

self.addEventListener('fetch', function (event) {
    var request = event.request;
    if (request.method !== 'GET') return;

    var url = new URL(request.url);

    if (isNeverCached(url)) return;   /* 交给网络，不拦截 */

    /* 导航：network-first，离线回退到对应入口页。
     * 按路径选回退目标 —— /play/<id> 要回 /play.html 而不是首页。 */
    if (request.mode === 'navigate') {
        event.respondWith(
            fetch(request).then(function (response) {
                var clone = response.clone();
                caches.open(CACHE_NAME).then(function (c) { c.put(request, clone); });
                return response;
            }).catch(function () {
                return caches.match(request).then(function (cached) {
                    if (cached) return cached;
                    var fallback = '/index.html';
                    if (url.pathname === '/play' || url.pathname.indexOf('/play/') === 0) {
                        fallback = '/play.html';
                    }
                    return caches.match(fallback);
                });
            })
        );
        return;
    }

    /* 同源静态资源：cache-first（wasm/js/数据每次构建都换名，天然不可变） */
    if (url.origin === self.location.origin) {
        event.respondWith(
            caches.match(request).then(function (cached) {
                if (cached) return cached;
                return fetch(request).then(function (response) {
                    if (response.ok && response.type === 'basic') {
                        var clone = response.clone();
                        caches.open(CACHE_NAME).then(function (c) { c.put(request, clone); });
                    }
                    return response;
                });
            })
        );
    }
});

self.addEventListener('message', function (event) {
    if (event.data === 'skipWaiting') self.skipWaiting();
});
`;

writeFileSync(join(dist, 'sw.js'), sw);

const totalMiB = files.reduce((n, f) => n + f.size, 0) / 1048576;
console.log(
    `✓ sw.js 已生成 (v${version})\n` +
    `  预缓存 ${precache.length} 个文件 / 产物共 ${files.length} 个，${totalMiB.toFixed(1)} MiB`
);

// wrangler.jsonc 里的绑定 id 还是占位符时提前喊一声。
// 构建本身不该失败（本地 --local 不校验这些值），但部署一定会挂，
// 与其等 wrangler 报一句难懂的错，不如在这里说清楚要跑什么命令。
try {
    const cfg = readFileSync(join(root, 'wrangler.jsonc'), 'utf-8');
    const pending = [];
    if (cfg.includes('REPLACE_ME_RUN_wrangler_d1_create')) {
        pending.push('  D1： npx wrangler d1 create krkr2-games            → 填入 database_id');
    }
    if (cfg.includes('REPLACE_ME_RUN_wrangler_kv_namespace_create')) {
        pending.push('  KV： npx wrangler kv namespace create RATE_LIMIT   → 填入 id');
    }
    if (pending.length) {
        console.warn(
            `\n⚠ wrangler.jsonc 的绑定 id 仍是占位符，部署会失败：\n` +
            pending.join('\n') +
            `\n  另需设置 secret：ADMIN_PASSWORD_HASH、SESSION_SECRET（见 README）`
        );
    }
} catch {
    // 读不到配置不影响构建
}
