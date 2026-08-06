// Worker 入口：路由 + 安全头。
//
// 三个 HTML 入口是独立的页面（MPA），因为引擎是硬单例、退出游戏必须整页卸载。
// 用户看到的仍是干净的 /、/play/<id>、/admin，由这里改写到对应的资源。

import { handleApi } from './api.js';
import { withSecurityHeaders, error } from './headers.js';

/** 取静态资源，并把请求路径改写到指定的 HTML 入口。 */
function serveAsset(env, request, url, assetPath) {
    const target = new URL(assetPath, url.origin);
    return env.ASSETS.fetch(new Request(target, {
        method: 'GET',
        headers: request.headers
    }));
}

/**
 * 这是不是一次页面导航（而非子资源请求）？
 *
 * 用来限定 /play/* 和 /admin/* 的 HTML 改写范围。否则
 * /play/assets.zip 这类子资源也会拿到 200 + HTML —— 一旦哪天
 * 相对路径回归，得到的是"看起来正常的 HTML"而不是干净的 404，
 * 排查成本高得多。
 *
 * 判据用 Accept 而不是 Sec-Fetch-Dest：Service Worker 用原 Request
 * 重发 fetch() 时，Sec-Fetch-* 是浏览器管控的禁止标头，会被重算成
 * `empty`，导航请求就会被误判成子资源、整页 404。Accept 则原样保留。
 * 导航一定带 text/html，JS 里 fetch() 子资源是 * / *，区分足够可靠。
 */
function isDocumentRequest(request) {
    return (request.headers.get('Accept') || '').includes('text/html');
}

export default {
    async fetch(request, env, ctx) {
        const url = new URL(request.url);
        const { pathname } = url;

        try {
            // --- API ---------------------------------------------------
            if (pathname === '/api' || pathname.startsWith('/api/')) {
                if (!env.DB) {
                    return withSecurityHeaders(
                        error(503, 'D1 未绑定：请先执行 wrangler d1 create 并填写 wrangler.jsonc')
                    );
                }
                const response = await handleApi(request, env, ctx, pathname);
                return withSecurityHeaders(response);
            }

            // --- 播放页：/play/<id> 统一由 play.html 承载 ----------------
            // 只改写导航请求；子资源继续走静态层，命不中就正常 404。
            if ((pathname === '/play' || pathname.startsWith('/play/')) &&
                isDocumentRequest(request)) {
                const asset = await serveAsset(env, request, url, '/play.html');
                return withSecurityHeaders(asset);
            }

            // --- 后台：/admin[/*] ---------------------------------------
            // 不在此处拦截未登录请求：真正的防线是 /api/admin/* 的 session 校验。
            // admin.html 只是个登录壳，后台主体是登录成功后才动态 import 的 chunk。
            if ((pathname === '/admin' || pathname.startsWith('/admin/')) &&
                isDocumentRequest(request)) {
                const asset = await serveAsset(env, request, url, '/admin.html');
                return withSecurityHeaders(asset, { 'X-Robots-Tag': 'noindex, nofollow' });
            }

            // --- favicon：浏览器会隐式请求 /favicon.ico，站内没有这个文件。
            // 复用已有的 PWA 图标，省掉每次访问一条 404。 ---------------
            if (pathname === '/favicon.ico') {
                const icon = await serveAsset(env, request, url, '/pwa/icon-192.png');
                return withSecurityHeaders(icon, {
                    'Cache-Control': 'public, max-age=604800'
                });
            }

            // --- 其余交给静态资源 ---------------------------------------
            const asset = await env.ASSETS.fetch(request);

            // SPA 式回退：静态资源没命中且看起来是页面导航，回首页而不是裸 404
            if (asset.status === 404 && request.headers.get('Accept')?.includes('text/html')) {
                const fallback = await serveAsset(env, request, url, '/index.html');
                return withSecurityHeaders(
                    new Response(fallback.body, { status: 404, headers: fallback.headers })
                );
            }

            return withSecurityHeaders(asset);
        } catch (err) {
            console.error('[worker] unhandled:', err?.stack || err);
            return withSecurityHeaders(error(500, 'Internal error'));
        }
    }
};
