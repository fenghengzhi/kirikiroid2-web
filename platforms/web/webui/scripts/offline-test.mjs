import puppeteer from 'puppeteer-core';
const BASE = 'http://localhost:8787';
const browser = await puppeteer.launch({
    executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new',
    args: ['--no-sandbox']
});
let failures = 0;
const page = await browser.newPage();

// 先在线访问，让 SW 装好并预缓存
await page.goto(BASE + '/', { waitUntil: 'networkidle2' });
await page.evaluate(() => navigator.serviceWorker.ready);
await new Promise((r) => setTimeout(r, 6000));

const cached = await page.evaluate(async () => {
    const names = await caches.keys();
    let n = 0;
    for (const k of names) n += (await (await caches.open(k)).keys()).length;
    return { names, n };
});
console.log(`  缓存: ${cached.names.join(', ')} — ${cached.n} 条`);

// 断网后访问各路由。
// /admin 刻意不预缓存（SW 里 isNeverCached 排除）：后台离线毫无用处，
// 与其展示一个连不上 API 的登录框，不如直接给浏览器的网络错误。
const expectations = [
    { path: '/', shouldWork: true },
    { path: '/play/local', shouldWork: true },
    { path: '/admin', shouldWork: false }
];

await page.setOfflineMode(true);
for (const { path, shouldWork } of expectations) {
    let status = 0, title = '', rendered = false, threw = false;
    try {
        const resp = await page.goto(BASE + path, { waitUntil: 'domcontentloaded', timeout: 15000 });
        status = resp?.status() ?? 0;
        await new Promise((r) => setTimeout(r, 1200));
        title = await page.title();
        rendered = await page.evaluate(() => (document.getElementById('app')?.children.length ?? 0) > 0);
    } catch {
        threw = true;
    }

    const worked = !threw && status === 200 && rendered;
    const ok = worked === shouldWork;
    const note = shouldWork ? `→ ${status} "${title.slice(0, 28)}"` : '→ 按设计断网不可用';
    console.log(`  ${ok ? 'PASS' : 'FAIL'}  离线 ${path} ${note}`);
    if (!ok) failures++;
}
await browser.close();
console.log(failures ? `\n✗ ${failures} 项离线问题` : '\n✓ 离线全部通过');
process.exit(failures ? 1 : 0);
