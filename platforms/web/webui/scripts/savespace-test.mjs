// 直接 import 真实的 src/player/saveSpace.js，而不是复刻一份逻辑
// —— 复刻只能验证语义，验不了真代码。
//
// 需要 vite dev（原生 ESM，才能 import 未打包的源码）。本脚本自己拉起它，
// 这样 `npm test` 一条命令就能跑，不用手动开两个服务。
import puppeteer from 'puppeteer-core';
import { spawn } from 'node:child_process';

const PORT = 5199;
const BASE = `http://localhost:${PORT}`;

const vite = spawn('npx', ['vite', '--port', String(PORT), '--strictPort'], {
    stdio: 'ignore',
    detached: false
});

// 等 dev server 起来
let up = false;
for (let i = 0; i < 40; i++) {
    try {
        const r = await fetch(BASE + '/play.html');
        if (r.ok) { up = true; break; }
    } catch { /* 还没起来 */ }
    await new Promise((r) => setTimeout(r, 500));
}
if (!up) {
    vite.kill();
    console.error('  vite dev 启动失败');
    process.exit(1);
}

const browser = await puppeteer.launch({
    executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: 'new', args: ['--no-sandbox']
});
let failures = 0;
const ok = (c, m) => { console.log(`  ${c ? 'PASS' : 'FAIL'}  ${m}`); if (!c) failures++; };

const page = await browser.newPage();
page.on('console', (m) => { if (/\[saves\]/.test(m.text())) console.log('    log: ' + m.text()); });
page.on('pageerror', (e) => console.log('    ! ' + e.message.slice(0, 120)));
await page.goto(BASE + '/play.html', { waitUntil: 'domcontentloaded' });

const r = await page.evaluate(async () => {
    const m = await import('/src/player/saveSpace.js');
    const exports = Object.keys(m);

    const open = (n) => new Promise((res) => {
        const rq = indexedDB.open('krkr2-space-' + n, 1);
        rq.onupgradeneeded = (e) => e.target.result.createObjectStore('files');
        rq.onsuccess = (e) => res(e.target.result);
    });
    const readAll = (db) => new Promise((res) => {
        const out = [];
        const c = db.transaction('files', 'readonly').objectStore('files').openCursor();
        c.onsuccess = (e) => { const cur = e.target.result; if (cur) { out.push(cur.key); cur.continue(); } else res(out); };
    });

    // 旧空间：save_<title>
    const game = { id: 'gid-777', title: '会被改掉的标题' };
    const legacy = 'save_' + game.title;
    const db = await open(legacy);
    await new Promise((res) => {
        const tx = db.transaction('files', 'readwrite');
        tx.objectStore('files').put(new Uint8Array([9, 9]), '/savedata/a.ksd');
        tx.objectStore('files').put(new Uint8Array([8]), '/savedata/b.ksd');
        tx.oncomplete = res;
    });
    db.close();
    localStorage.setItem('krkr2-spaces', JSON.stringify([legacy]));

    // 调真函数
    const did = await m.migrateLegacySpace(game);
    const target = m.spaceIdFor ? m.spaceIdFor(game) : ('game_' + game.id);
    const tdb = await open(target);
    const files = (await readAll(tdb)).sort();
    tdb.close();

    // 幂等
    const again = await m.migrateLegacySpace(game);

    // 标题改了、id 不变 → 空间名必须不变
    const sameAfterRename = target === (m.spaceIdFor
        ? m.spaceIdFor({ id: 'gid-777', title: '全新标题' })
        : 'game_gid-777');

    return { exports, did, target, files, again, sameAfterRename };
});

console.log(`    exports: ${r.exports.join(', ')}`);
ok(r.did === true, '真实 migrateLegacySpace 返回已迁移');
ok(r.files.length === 2, `新空间 ${r.target} 收到 2 个存档（实际 ${r.files.length}）`);
ok(JSON.stringify(r.files) === JSON.stringify(['/savedata/a.ksd', '/savedata/b.ksd']), '存档路径不变');
ok(r.again === false, '二次调用不重复迁移（幂等）');
ok(r.sameAfterRename, '改标题后空间名不变（绑 id 而非 title）');

await browser.close();
vite.kill();
console.log(failures ? `\n✗ ${failures} 项问题` : '\n✓ 全部通过');
process.exit(failures ? 1 : 0);
