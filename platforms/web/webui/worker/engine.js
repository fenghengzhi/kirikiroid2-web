// 引擎大文件（index.wasm / assets.zip）从 R2 读出来，走同源路径 /engine/*。
//
// 为什么不让浏览器直连 R2 公开域名：
//   页面开着跨源隔离（COOP: same-origin + COEP: require-corp），这是引擎用
//   SharedArrayBuffer 跑 pthread 的前提。require-corp 的字面含义是"本页加载的
//   每一个跨源资源都必须显式声明同意被嵌入"，于是 R2 侧就得配自定义域名 +
//   Access-Control-Allow-Origin + Cross-Origin-Resource-Policy 三样东西。
//   走同源则一样都不需要 —— headers.js 已经给所有响应加了 COOP/COEP，
//   service worker 的"同源 cache-first"分支也自动覆盖。
//
// 为什么不怕经 Worker 中转：
//   引擎是整取（wasm 一次 instantiateStreaming 的 GET，zip 一次 fetch 转 Blob），
//   不是 js/loaders/remote.js 那种对多 GB 游戏包的 HTTP Range 懒加载。
//   README 里"游戏包不经 Worker"的理由在这里不适用。
//
// 顺带：Workers 静态资源有 25 MiB 单文件上限（index.wasm 已 21.9 MiB），
// 而从 R2 binding 读出来的响应不算静态资源，该上限不适用（R2 单对象上限 5 TB）。

import { error } from './headers.js';

// 白名单：这个路由不能退化成"任意读桶"的开放代理。
// 只有这两个文件名可达，其余一律 404，不泄漏桶里有什么。
const ENGINE_FILES = {
    'index.wasm': 'application/wasm',
    'assets.zip': 'application/zip'
};

// 路径带版本段（build-web.yml 用 commit sha 前 12 位），内容因此不可变。
const IMMUTABLE = 'public, max-age=31536000, immutable';

/**
 * 解析 Range: bytes=a-b / bytes=a- / bytes=-n。
 *
 * 直接产出 R2 的 range 选项（{offset,length} 或 {suffix}），因此不需要先知道
 * 对象大小 —— 少一次 head 往返。越界与否交给 R2 判断。
 */
function parseRange(header) {
    const m = /^bytes=(\d*)-(\d*)$/.exec((header || '').trim());
    if (!m) return null;
    const [, rawStart, rawEnd] = m;

    if (rawStart === '') {
        // bytes=-N：最后 N 字节
        const suffix = Number(rawEnd);
        return suffix > 0 ? { suffix } : null;
    }
    const offset = Number(rawStart);
    if (rawEnd === '') return { offset };                    // bytes=N-  直到结尾
    const end = Number(rawEnd);
    if (end < offset) return null;
    return { offset, length: end - offset + 1 };
}

/**
 * GET /engine/<版本>/<文件名>
 *
 * @param {Request} request
 * @param {object}  env      需要 env.ENGINE（R2 桶绑定）
 * @param {object}  ctx      用于 waitUntil 写边缘缓存
 * @param {string}  pathname
 */
export async function serveEngine(request, env, ctx, pathname) {
    if (!env.ENGINE) {
        return error(503,
            'R2 未绑定：请先 wrangler r2 bucket create krkr2-engine，' +
            '并在 wrangler.jsonc 的 r2_buckets 里填上 bucket_name');
    }

    const key = pathname.slice('/engine/'.length);
    const name = key.slice(key.lastIndexOf('/') + 1);
    // 白名单已经排除了路径穿越（'..' 不在表里），这里只是把意图写明
    if (!Object.prototype.hasOwnProperty.call(ENGINE_FILES, name)) {
        return error(404, 'Not found');
    }
    const contentType = ENGINE_FILES[name];

    // 边缘缓存只对整取生效：Range 响应是 206 + 各不相同的 Content-Range，
    // 按同一个 key 缓存会互相污染。206 直接回源，R2 读本来就在同机房。
    const rangeHeader = request.headers.get('Range');
    const cache = caches.default;

    if (!rangeHeader) {
        const hit = await cache.match(request);
        if (hit) return hit;
    }

    const range = rangeHeader ? parseRange(rangeHeader) : null;
    const object = range
        ? await env.ENGINE.get(key, { range })
        : await env.ENGINE.get(key);

    if (!object) return error(404, 'Not found');

    const headers = new Headers({
        'Content-Type': contentType,
        'Cache-Control': IMMUTABLE,
        'ETag': object.httpEtag,
        // 让浏览器知道可以断点续传 —— 22MB 在移动网络上断一次代价不小
        'Accept-Ranges': 'bytes'
    });

    if (object.range && range) {
        // R2 已按 range 截好，但回给浏览器的 Content-Range 需要绝对区间。
        // suffix 形式下 offset 由 R2 算出，统一从 object.range 取。
        const offset = object.range.offset ?? 0;
        const length = object.range.length ?? (object.size - offset);
        headers.set('Content-Range',
            `bytes ${offset}-${offset + length - 1}/${object.size}`);
        headers.set('Content-Length', String(length));
        return new Response(object.body, { status: 206, headers });
    }

    headers.set('Content-Length', String(object.size));
    const response = new Response(object.body, { status: 200, headers });

    // 整取才写边缘缓存。clone 必须在返回前做：body 是流，只能消费一次。
    ctx.waitUntil(cache.put(request, response.clone()));
    return response;
}
