// 安全响应头。
//
// COOP/COEP 是引擎的硬前提：wasm 用 SharedArrayBuffer 跑 pthread，
// 缺这两个头 SharedArrayBuffer 在浏览器里根本不存在，引擎起不来。
//
// 这里主动包装每一个响应而不是依赖 dist/_headers：Worker 有自定义 fetch
// handler 后，/api/* 这类不经资源层的响应拿不到 _headers 的规则。

const SECURITY_HEADERS = {
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp',
    'X-Content-Type-Options': 'nosniff',
    'Referrer-Policy': 'strict-origin-when-cross-origin'
};

/** 给任意响应补上安全头。响应体不动，只重建 headers。 */
export function withSecurityHeaders(response, extra = {}) {
    const headers = new Headers(response.headers);
    for (const [k, v] of Object.entries(SECURITY_HEADERS)) headers.set(k, v);
    for (const [k, v] of Object.entries(extra)) headers.set(k, v);

    return new Response(response.body, {
        status: response.status,
        statusText: response.statusText,
        headers
    });
}

export function json(data, init = {}) {
    return new Response(JSON.stringify(data), {
        status: init.status || 200,
        headers: {
            'Content-Type': 'application/json; charset=utf-8',
            // API 响应默认不给中间层缓存，需要缓存的路由自己覆盖
            'Cache-Control': 'no-store',
            ...(init.headers || {})
        }
    });
}

export function error(status, message, extra = {}) {
    return json({ error: message }, { status, ...extra });
}
