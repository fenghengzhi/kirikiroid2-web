// 浏览器冒烟测试：抓控制台错误、失败请求，并断言关键 UI 真的渲染出来。
// 静态检查看不出运行时错误，这一层专门补这个。

import puppeteer from 'puppeteer-core';

const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const BASE = 'http://localhost:8787';
const PASSWORD = process.env.ADMIN_PASSWORD || 'test-password-123';

// 库默认是空的（产品就这么设计的），而画廊/后台的断言都要求至少有一个条目。
// 所以自己建一条、测完删掉，别指望库里留着上次的脏数据。
// downloadUrl 指向不可达域名：只测列表渲染，不需要真把游戏跑起来。
async function login() {
    const res = await fetch(`${BASE}/api/auth/login`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ password: PASSWORD })
    });
    if (!res.ok) throw new Error(`登录失败 (${res.status})，检查 .dev.vars 里的 ADMIN_PASSWORD_HASH`);
    return (res.headers.get('set-cookie') || '').split(';')[0];
}

const cookie = await login();

const fixtureId = await (async () => {
    const res = await fetch(`${BASE}/api/admin/games`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Cookie: cookie },
        body: JSON.stringify({
            title: '冒烟测试用条目',
            downloadUrl: 'https://example.invalid/never.xp3',
            published: 1
        })
    });
    if (!res.ok) throw new Error(`创建测试条目失败 (${res.status})`);
    return (await res.json()).game.id;
})();

const browser = await puppeteer.launch({
    executablePath: CHROME,
    headless: 'new',
    args: ['--no-sandbox', '--disable-dev-shm-usage']
});

let failures = 0;

async function visit(path, { wait = 1800, assert } = {}) {
    const page = await browser.newPage();
    // SW 会拿上一次构建的 chunk / 缓存的 API 响应顶掉新内容，绕开它
    const cdp = await page.createCDPSession();
    await cdp.send('Network.enable');
    await cdp.send('Network.setBypassServiceWorker', { bypass: true });
    const errors = [];
    const badRequests = [];

    page.on('console', (m) => {
        if (m.type() === 'error') errors.push(m.text());
    });
    page.on('pageerror', (e) => errors.push('[pageerror] ' + e.message));
    page.on('requestfailed', (r) => {
        badRequests.push(`${r.failure()?.errorText} ${r.url()}`);
    });
    page.on('response', (r) => {
        if (r.status() >= 400) badRequests.push(`HTTP ${r.status()} ${r.url()}`);
    });

    console.log(`\n=== ${path} ===`);
    try {
        await page.goto(BASE + path, { waitUntil: 'networkidle2', timeout: 30000 });
    } catch (e) {
        console.log('  导航超时/失败: ' + e.message.split('\n')[0]);
    }
    await new Promise((r) => setTimeout(r, wait));

    if (assert) {
        const result = await assert(page);
        for (const [label, ok] of Object.entries(result)) {
            console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${label}`);
            if (!ok) failures++;
        }
    }

    // 预期内的噪音：
    //   - 测试条目的下载地址/封面指向 example.invalid，取不到是设计如此
    //     （所以这里只按网络错误类型过滤，不写具体域名 —— 库是空的，
    //     写死某个图床域名对别人的部署没有意义）
    //   - /api/admin/me 的 401 是设计如此：前端靠它判断未登录
    const ignorable = /favicon|ERR_INTERNET|ERR_NAME|ERR_CONNECTION|ERR_ABORTED|Failed to load resource: net::/i;
    const expected401 = /\/api\/admin\/me/;

    const realErrors = errors.filter((e) => !ignorable.test(e) && !/401/.test(e));
    const realBad = badRequests.filter((r) => !ignorable.test(r) && !expected401.test(r));

    if (realErrors.length) {
        console.log('  控制台错误:');
        realErrors.slice(0, 8).forEach((e) => console.log('    ! ' + e.slice(0, 180)));
        failures += realErrors.length;
    }
    if (realBad.length) {
        console.log('  失败请求:');
        realBad.slice(0, 8).forEach((r) => console.log('    ! ' + r.slice(0, 180)));
        failures += realBad.length;
    }
    if (!realErrors.length && !realBad.length) console.log('  无控制台错误 / 无失败请求');

    await page.close();
}

// --- 画廊页 ---
await visit('/', {
    assert: async (page) => ({
        '渲染出游戏卡片': (await page.$$('.card')).length > 0,
        '标题为“游戏库”': (await page.$eval('h1', (e) => e.textContent).catch(() => '')) === '游戏库',
        '卡片链接指向 /play/': (await page.$$eval('.card', (els) =>
            els.every((e) => e.getAttribute('href')?.startsWith('/play/'))).catch(() => false)),
        '封面走 /api/cover 代理': (await page.$$eval('.card img', (els) =>
            els.length === 0 || els.every((e) => e.getAttribute('src')?.startsWith('/api/cover/'))).catch(() => false)),
        '搜索框存在': !!(await page.$('.search input')),
        // 引擎地址必须取自 engineBase，不能写死。启用 KRKR2_ENGINE_BASE 后
        // wasm 在 /engine/<版本>/ 下，写死 /index.wasm 会 404 —— 预热失灵
        // 且每次进画廊都刷控制台错误。这里钉住"prefetch 与 engineBase 一致"。
        'wasm 预热地址跟随 engineBase': await page.evaluate(() => {
            const base = (window.KrKr2Config && window.KrKr2Config.engineBase) || '/';
            const links = [...document.querySelectorAll('link[rel="prefetch"]')]
                .map((l) => l.getAttribute('href'));
            const wasm = links.filter((h) => h && h.endsWith('index.wasm'));
            return wasm.length > 0 && wasm.every((h) => h === base + 'index.wasm');
        })
    })
});

// --- 后台登录 ---
await visit('/admin', {
    assert: async (page) => {
        const bodyText = await page.evaluate(() => document.body.innerText);
        const scripts = await page.evaluate(() =>
            performance.getEntriesByType('resource').map((r) => r.name).join('\n'));
        return {
            '显示登录表单': !!(await page.$('input[type="password"]')),
            '未登录不下载后台 chunk': !/GamesAdmin/.test(scripts),
            '有返回游戏库链接': bodyText.includes('返回游戏库')
        };
    }
});

// --- 播放页（本地文件模式，不需要真游戏数据）---
await visit('/play/local', {
    wait: 4000,
    assert: async (page) => {
        const hasEngine = await page.evaluate(() => ({
            engine: typeof window.KrKr2Engine?.boot === 'function',
            vlfs: typeof window.VLFS !== 'undefined',
            idb: typeof window.KrKr2IDB?.listSpaces === 'function',
            config: typeof window.KrKr2Config?.assetBase === 'string',
            assetBase: window.KrKr2Config?.assetBase,
            crossOriginIsolated: window.crossOriginIsolated
        }));
        console.log('    assetBase =', hasEngine.assetBase,
                    '| crossOriginIsolated =', hasEngine.crossOriginIsolated);
        return {
            'KrKr2Engine 已就绪': hasEngine.engine,
            'VLFS 已加载': hasEngine.vlfs,
            'KrKr2IDB 已加载': hasEngine.idb,
            'assetBase 配置存在': hasEngine.config,
            '跨源隔离生效(SharedArrayBuffer 可用)': hasEngine.crossOriginIsolated === true,
            '显示本地文件选择器': !!(await page.$('.drop'))
        };
    }
});

// --- 播放页：?xp3= 直连数据源（引擎调试入口）---
//
// coi-server.py --xp3/--zip 打印的就是这种 URL，根 README 的调试流程依赖它。
// 旧 js/app.js 有这个能力，换 Vue 时差点丢掉 —— 这里钉住：
// 带 ?xp3= 时必须走加载器，既不查 D1，也不弹本地文件选择器。
// URL 指向不可达域名：只验证分支走对，不需要真下载成功。
await visit('/play.html?xp3=https://example.invalid/direct.xp3', {
    wait: 3000,
    assert: async (page) => {
        const state = await page.evaluate(() => ({
            picker: !!document.querySelector('.backdrop'),
            title: document.querySelector('.bar .title')?.textContent?.trim() ?? null,
            modalMsg: document.querySelector('.modal .msg')?.textContent?.trim() ?? null
        }));
        return {
            '?xp3= 不弹本地文件选择器': !state.picker,
            '?xp3= 标题取 URL 文件名': state.title === 'direct.xp3',
            // 这条文案只可能来自 api.getGame 分支，出现即说明 URL 参数被忽略了
            '?xp3= 不走游戏库查询分支': state.modalMsg !== '这个游戏不存在，或已被下架。'
        };
    }
});

// --- 后台真实登录流程（走 UI，不是直接打 API）---
{
    const page = await browser.newPage();
    const cdp = await page.createCDPSession();
    await cdp.send('Network.enable');
    await cdp.send('Network.setBypassServiceWorker', { bypass: true });
    console.log('\n=== /admin 登录流程 ===');
    const errors = [];
    page.on('pageerror', (e) => errors.push(e.message));

    await page.goto(BASE + '/admin', { waitUntil: 'networkidle2' });
    await new Promise((r) => setTimeout(r, 800));

    await page.type('input[type="password"]', PASSWORD);
    await page.click('button[type="submit"]');
    await new Promise((r) => setTimeout(r, 2500));

    const after = await page.evaluate(() => ({
        hasTable: !!document.querySelector('table'),
        hasNewButton: document.body.innerText.includes('新增游戏'),
        rowCount: document.querySelectorAll('tbody tr').length,
        stillLogin: !!document.querySelector('input[type="password"]'),
        loadedAdminChunk: performance.getEntriesByType('resource')
            .some((r) => /GamesAdmin/.test(r.name))
    }));

    const checks = {
        '登录后进入后台': !after.stillLogin,
        '渲染出游戏表格': after.hasTable,
        '有新增按钮': after.hasNewButton,
        [`列出 ${after.rowCount} 个条目`]: after.rowCount > 0,
        '登录后才加载后台 chunk': after.loadedAdminChunk,
        '无页面异常': errors.length === 0
    };
    for (const [label, ok] of Object.entries(checks)) {
        console.log(`  ${ok ? 'PASS' : 'FAIL'}  ${label}`);
        if (!ok) failures++;
    }
    if (errors.length) errors.slice(0, 5).forEach((e) => console.log('    ! ' + e.slice(0, 160)));

    await page.close();
}

await browser.close();

// 清掉测试条目，别把库弄脏
await fetch(`${BASE}/api/admin/games/${fixtureId}`, {
    method: 'DELETE',
    headers: { Cookie: cookie }
}).catch(() => {});

console.log(failures ? `\n✗ ${failures} 项问题` : '\n✓ 全部通过');
process.exit(failures ? 1 : 0);