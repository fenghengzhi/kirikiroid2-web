// 认证：PBKDF2 密码校验 + HMAC 签名的无状态 session cookie。
//
// 密码哈希与签名密钥都存 Cloudflare Secret，不进 D1/KV —— 改密码就是
// 重新 `wrangler secret put ADMIN_PASSWORD_HASH`，没有需要维护的用户表。

// Cloudflare Workers 的 WebCrypto 硬性上限就是 100000：
// 超过会抛 NotSupportedError("iteration counts above 100000 are not supported")。
// OWASP 对 PBKDF2-SHA256 的建议值更高（600k），但平台不允许，
// 这里取平台允许的最大值。verifyPassword 从哈希串里读实际轮数，
// 所以将来平台放宽后调大这个常量，旧哈希依然能校验。
const ITERATIONS_DEFAULT = 100000;
const MAX_ITERATIONS = 100000;
const SESSION_TTL_SECONDS = 7 * 24 * 60 * 60;

// __Host- 前缀由浏览器强制：必须 Secure、Path=/、且不带 Domain。
// 这挡掉了子域写入伪造 cookie 的路径。
const COOKIE_NAME = '__Host-krkr2_sess';

const encoder = new TextEncoder();

function b64encode(bytes) {
    let s = '';
    const arr = new Uint8Array(bytes);
    for (let i = 0; i < arr.length; i++) s += String.fromCharCode(arr[i]);
    return btoa(s);
}

function b64decode(str) {
    const bin = atob(str);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
}

// base64url：cookie 值里不能出现 + / =
function b64urlEncode(bytes) {
    return b64encode(bytes).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function b64urlDecode(str) {
    let s = str.replace(/-/g, '+').replace(/_/g, '/');
    while (s.length % 4) s += '=';
    return b64decode(s);
}

/**
 * 常量时间比较。Workers runtime 没有 Node 的 timingSafeEqual，
 * 手写 XOR 累加：耗时只与长度相关，与内容差异位置无关。
 */
function timingSafeEqual(a, b) {
    if (a.length !== b.length) return false;
    let diff = 0;
    for (let i = 0; i < a.length; i++) diff |= a[i] ^ b[i];
    return diff === 0;
}

async function pbkdf2(password, salt, iterations, lengthBits) {
    const keyMaterial = await crypto.subtle.importKey(
        'raw', encoder.encode(password), 'PBKDF2', false, ['deriveBits']
    );
    return new Uint8Array(await crypto.subtle.deriveBits(
        { name: 'PBKDF2', hash: 'SHA-256', salt, iterations },
        keyMaterial,
        lengthBits
    ));
}

/** 生成 `pbkdf2$<iters>$<b64salt>$<b64hash>`，供 scripts/hash-password.js 调用。 */
export async function hashPassword(password, iterations = ITERATIONS_DEFAULT) {
    const salt = crypto.getRandomValues(new Uint8Array(16));
    const hash = await pbkdf2(password, salt, iterations, 256);
    return `pbkdf2$${iterations}$${b64encode(salt)}$${b64encode(hash)}`;
}

/** 校验明文密码是否匹配 stored 哈希串。格式不合法一律返回 false，不抛。 */
export async function verifyPassword(password, stored) {
    if (typeof stored !== 'string') return false;
    // secret 经 `cat file | wrangler secret put` 设置时会带上尾部换行，
    // 不 trim 的话末段 base64 解不出来，表现为"密码明明对却登录失败"。
    const parts = stored.trim().split('$');
    if (parts.length !== 4 || parts[0] !== 'pbkdf2') return false;

    const iterations = parseInt(parts[1], 10);
    if (!Number.isFinite(iterations) || iterations < 1000) return false;
    // 超过平台上限的话 deriveBits 会抛 NotSupportedError，
    // 那是一个 500 而不是"密码错误"。提前挡掉并留下可诊断的日志。
    if (iterations > MAX_ITERATIONS) {
        console.error(
            `[auth] 哈希串里的迭代轮数 ${iterations} 超过 Workers 上限 ${MAX_ITERATIONS}，` +
            `请用当前版本的 scripts/hash-password.js 重新生成 ADMIN_PASSWORD_HASH`
        );
        return false;
    }

    let salt, expected;
    try {
        salt = b64decode(parts[2]);
        expected = b64decode(parts[3]);
    } catch {
        return false;
    }

    const actual = await pbkdf2(password, salt, iterations, expected.length * 8);
    return timingSafeEqual(actual, expected);
}

async function hmacKey(secret) {
    return crypto.subtle.importKey(
        'raw', encoder.encode(secret),
        { name: 'HMAC', hash: 'SHA-256' },
        false, ['sign']
    );
}

async function sign(payloadB64, secret) {
    const key = await hmacKey(secret);
    const sig = await crypto.subtle.sign('HMAC', key, encoder.encode(payloadB64));
    return b64urlEncode(sig);
}

/** 签发 session token：`<payload>.<hmac>`，payload 里带过期时间。 */
export async function createSession(secret, ttlSeconds = SESSION_TTL_SECONDS) {
    const payload = {
        sub: 'admin',
        iat: Math.floor(Date.now() / 1000),
        exp: Math.floor(Date.now() / 1000) + ttlSeconds,
        // 随机 jti 让每次登录的 token 不同，便于将来做吊销
        jti: b64urlEncode(crypto.getRandomValues(new Uint8Array(12)))
    };
    const payloadB64 = b64urlEncode(encoder.encode(JSON.stringify(payload)));
    return `${payloadB64}.${await sign(payloadB64, secret)}`;
}

/** 校验 token 签名与过期时间。任何异常都当作无效，不区分原因。 */
export async function verifySession(token, secret) {
    if (!token || typeof token !== 'string') return null;
    const dot = token.lastIndexOf('.');
    if (dot < 1) return null;

    const payloadB64 = token.slice(0, dot);
    const providedSig = token.slice(dot + 1);

    const expectedSig = await sign(payloadB64, secret);
    // 比较签名字节而非字符串，避免 === 的短路计时差异
    if (!timingSafeEqual(encoder.encode(providedSig), encoder.encode(expectedSig))) {
        return null;
    }

    let payload;
    try {
        payload = JSON.parse(new TextDecoder().decode(b64urlDecode(payloadB64)));
    } catch {
        return null;
    }

    if (!payload || typeof payload.exp !== 'number') return null;
    if (payload.exp < Math.floor(Date.now() / 1000)) return null;
    return payload;
}

export function sessionCookie(token, ttlSeconds = SESSION_TTL_SECONDS) {
    return `${COOKIE_NAME}=${token}; HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age=${ttlSeconds}`;
}

export function clearCookie() {
    return `${COOKIE_NAME}=; HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age=0`;
}

export function readSessionCookie(request) {
    const header = request.headers.get('Cookie');
    if (!header) return null;
    for (const part of header.split(';')) {
        const idx = part.indexOf('=');
        if (idx < 0) continue;
        if (part.slice(0, idx).trim() === COOKIE_NAME) return part.slice(idx + 1).trim();
    }
    return null;
}

/** 请求是否携带有效 session。所有 /api/admin/* 的准入判断都走这里。 */
export async function isAuthed(request, env) {
    if (!env.SESSION_SECRET) return false;
    const token = readSessionCookie(request);
    return (await verifySession(token, env.SESSION_SECRET)) !== null;
}

/**
 * 登录限速：同 IP 15 分钟 5 次失败。
 *
 * KV 最终一致，并发下计数可能少算几次；对登录暴力破解这个量级足够，
 * 不值得为此上 Durable Object。
 */
const RATE_WINDOW_SECONDS = 900;
const RATE_MAX_ATTEMPTS = 5;

export async function checkRateLimit(env, ip) {
    if (!env.RATE_LIMIT) return { allowed: true, remaining: RATE_MAX_ATTEMPTS };
    const raw = await env.RATE_LIMIT.get(`login:${ip}`);
    const count = raw ? parseInt(raw, 10) || 0 : 0;
    return {
        allowed: count < RATE_MAX_ATTEMPTS,
        remaining: Math.max(0, RATE_MAX_ATTEMPTS - count),
        retryAfter: RATE_WINDOW_SECONDS
    };
}

export async function recordFailedAttempt(env, ip) {
    if (!env.RATE_LIMIT) return;
    const key = `login:${ip}`;
    const raw = await env.RATE_LIMIT.get(key);
    const count = (raw ? parseInt(raw, 10) || 0 : 0) + 1;
    // 每次失败都重置 TTL：持续攻击会一直被锁，而不是窗口到点自动放行
    await env.RATE_LIMIT.put(key, String(count), { expirationTtl: RATE_WINDOW_SECONDS });
}

export async function clearRateLimit(env, ip) {
    if (!env.RATE_LIMIT) return;
    await env.RATE_LIMIT.delete(`login:${ip}`);
}

export { COOKIE_NAME, SESSION_TTL_SECONDS };
