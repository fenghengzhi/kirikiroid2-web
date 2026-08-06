// 验证 SW 不缓存 /api/*：旧版本对所有同源请求 cache-first，
// 会把 /api/games 的结果永久缓存，后台改完前台永远看不到。
import puppeteer from 'puppeteer-core';
const BASE = 'http://localhost:8787';
const browser = await puppeteer.launch({
    executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox']
});
let failures = 0;
const page = await browser.newPage();
await page.goto(BASE + '/', { waitUntil: 'networkidle2' });
await page.evaluate(() => navigator.serviceWorker.ready);
await new Promise((r) => setTimeout(r, 5000));

const apiCached = await page.evaluate(async () => {
    const names = await caches.keys();
    const hits = [];
    for (const k of names) {
        for (const req of await (await caches.open(k)).keys()) {
            if (req.url.includes('/api/')) hits.push(req.url);
        }
    }
    return hits;
});
console.log(`  缓存中的 /api/* 条目: ${apiCached.length}`);
apiCached.slice(0, 5).forEach((u) => console.log('    ! ' + u));

// /api/cover 是图片，允许缓存；/api/games 绝不可以
const badCached = apiCached.filter((u) => !u.includes('/api/cover/'));
console.log(`  ${badCached.length === 0 ? 'PASS' : 'FAIL'}  /api/games 等动态接口未被 SW 缓存`);
if (badCached.length) failures++;

// 实测：改数据后前端能立刻看到
const before = await page.evaluate(() => fetch('/api/games').then((r) => r.json()).then((d) => d.games.length));
console.log(`  当前列表: ${before} 条`);

await browser.close();
console.log(failures ? `\n✗ ${failures} 项问题` : '\n✓ 通过');
process.exit(failures ? 1 : 0);
