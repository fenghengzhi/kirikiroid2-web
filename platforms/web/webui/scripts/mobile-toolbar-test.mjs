// 验证触屏设备上边缘工具条能被唤出。
//
// 注意分清三种状态：
//   - 加载中：.overlay (z=200) 盖住一切，工具条本来就不该出现
//   - 模态打开（本地文件选择 / 存档面板）：.backdrop (z=300) 同理
//   - 游戏进行中：两者都被 v-if 移除，只剩 canvas —— 这才是玩家抱怨的场景
// 所以"能不能唤出"必须在第三种状态下测。
import puppeteer from 'puppeteer-core';

const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const BASE = 'http://localhost:8787';
const PASSWORD = process.env.ADMIN_PASSWORD || 'test-password-123';
const results = [];
const ok = (name, cond) => results.push([name, !!cond]);

// 自建一个条目再删掉：不依赖库里已有数据，空库也能跑。
// downloadUrl 指向不可达域名 —— 我们只测工具条，不需要真的把游戏跑起来。
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

async function createFixture() {
    const res = await fetch(`${BASE}/api/admin/games`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', Cookie: cookie },
        body: JSON.stringify({
            title: '工具条测试用条目',
            downloadUrl: 'https://example.invalid/never.xp3',
            published: 1
        })
    });
    if (!res.ok) throw new Error(`创建测试条目失败 (${res.status})`);
    return (await res.json()).game.id;
}

const GAME_ID = await createFixture();

const browser = await puppeteer.launch({
    executablePath: CHROME,
    headless: 'new',
    args: ['--no-sandbox', '--disable-dev-shm-usage']
});

async function newPage(touch) {
    const page = await browser.newPage();
    // puppeteer 的 emulateMediaFeatures 不认 hover，直接走 CDP
    const cdp = await page.createCDPSession();
    await cdp.send('Emulation.setEmulatedMedia', {
        features: [
            { name: 'hover', value: touch ? 'none' : 'hover' },
            { name: 'pointer', value: touch ? 'coarse' : 'fine' }
        ]
    });
    // SW 会拿旧 chunk 顶掉新构建，绕开它，否则测的是上一版代码
    await cdp.send('Network.enable');
    await cdp.send('Network.setBypassServiceWorker', { bypass: true });
    if (touch) {
        await page.setViewport({ width: 390, height: 844, isMobile: true, hasTouch: true, deviceScaleFactor: 3 });
    } else {
        await page.setViewport({ width: 1280, height: 800 });
    }
    return page;
}

// 等工具条**真正落位**，而不只是 display 变了。
// 它是 translateY(-100%) 滑入的，动画途中 rect.top 还是负数 ——
// 这时去点里面的按钮会点到视口外面，全屏死活进不去。
const waitForToolbar = (page) =>
    page.waitForFunction(() => {
        const bar = document.querySelector('.bar');
        if (!bar || getComputedStyle(bar).display === 'none') return false;
        return bar.getBoundingClientRect().top >= 0;
    }, { timeout: 3000 }).then(() => true).catch(() => false);

/** 复现"游戏进行中"的 DOM：把 v-if 会移除的加载浮层/模态摘掉。 */
const enterRunningState = (page) => page.evaluate(() => {
    document.querySelectorAll('.overlay, .backdrop, .modal-backdrop').forEach((el) => el.remove());
    return document.querySelectorAll('.overlay, .backdrop, .modal-backdrop').length;
});

// --- 触屏：把手的存在与尺寸 ---
{
    const page = await newPage(true);
    await page.goto(`${BASE}/play/${GAME_ID}`, { waitUntil: 'networkidle2' });
    await page.waitForSelector('.handle', { timeout: 8000 }).catch(() => {});

    const handle = await page.$('.handle');
    ok('触屏：把手已渲染', handle);
    if (handle) {
        const box = await handle.boundingBox();
        ok('触屏：把手在顶部居中', box && box.y < 60 && Math.abs((box.x + box.width / 2) - 195) < 40);
        ok('触屏：命中区 >= 44px 高', box && box.height >= 44);
    }
    await page.close();
}

// --- 游戏进行中：真实手指点击 ---
{
    const page = await newPage(true);
    await page.goto(`${BASE}/play/${GAME_ID}`, { waitUntil: 'networkidle2' });
    await page.waitForSelector('.handle', { timeout: 8000 }).catch(() => {});
    ok('进行中：浮层已清空', (await enterRunningState(page)) === 0);

    const h = await page.$('.handle');
    if (h) {
        const b = await h.boundingBox();
        const onTop = await page.evaluate((x, y) => {
            const el = document.elementFromPoint(x, y);
            return !!(el && el.closest('.handle'));
        }, b.x + b.width / 2, b.y + b.height / 2);
        ok('进行中：把手未被遮挡（命中测试）', onTop);

        await page.touchscreen.tap(b.x + b.width / 2, b.y + b.height / 2);
        ok('进行中：真实手指点击能展开工具条', await waitForToolbar(page));
    }
    await page.close();
}

// --- 把手会自行淡出，但不能变成点不到的死入口 ---
//
// 常驻的把手是画面上唯一的遮挡物，所以 6 秒后淡到 opacity:0。
// 但**不能**顺手把命中区也撤了：顶边下滑会被系统手势（下拉通知栏）截走，
// 全屏里又没有 hover 和 F 键，撤了就只剩杀标签页一条路。
{
    const page = await newPage(true);
    await page.goto(`${BASE}/play/${GAME_ID}`, { waitUntil: 'networkidle2' });
    await page.waitForSelector('.handle', { timeout: 8000 }).catch(() => {});
    await enterRunningState(page);

    const before = await page.evaluate(() => {
        const h = document.querySelector('.handle');
        return h ? Number(getComputedStyle(h).opacity) : null;
    });
    ok('淡出前：把手可见', before !== null && before > 0.2);

    // 等过 HANDLE_FADE_MS(6s) + 过渡(1.2s)
    await new Promise((r) => setTimeout(r, 7800));

    const after = await page.evaluate(() => {
        const h = document.querySelector('.handle');
        if (!h) return null;
        const r = h.getBoundingClientRect();
        const hit = document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2);
        return {
            opacity: Number(getComputedStyle(h).opacity),
            height: r.height,
            // opacity:0 仍然可命中（不同于 visibility:hidden / display:none）
            reachable: !!(hit && hit.closest('.handle'))
        };
    });
    ok('淡出后：视觉上完全透明', after && after.opacity === 0);
    ok('淡出后：命中区收窄但仍在', after && after.height > 0 && after.height <= 28);
    ok('淡出后：仍可命中（不是死入口）', after && after.reachable);

    // 最关键的一条：看不见了，但还点得开
    const r = await page.evaluate(() => {
        const b = document.querySelector('.handle').getBoundingClientRect();
        return { x: b.x + b.width / 2, y: b.y + b.height / 2 };
    });
    await page.touchscreen.tap(r.x, r.y);
    ok('淡出后：点它仍能展开工具条', await waitForToolbar(page));

    await page.close();
}

// --- 游戏进行中：顶边下滑 ---
{
    const page = await newPage(true);
    await page.goto(`${BASE}/play/${GAME_ID}`, { waitUntil: 'networkidle2' });
    await page.waitForSelector('.handle', { timeout: 8000 }).catch(() => {});
    await enterRunningState(page);

    await page.touchscreen.touchStart(195, 10);
    await page.touchscreen.touchMove(195, 35);
    await page.touchscreen.touchMove(195, 60);
    await page.touchscreen.touchEnd();
    ok('进行中：顶边下滑能展开工具条', await waitForToolbar(page));
    await page.close();
}

// --- 全屏之后仍能唤出（回归：曾经彻底唤不出）---
//
// 元素全屏会把 .stage 提升到 top layer，它的**兄弟**节点被 ::backdrop 盖死，
// position:fixed + 高 z-index 都救不回来。手机上没 hover 也没 F 键，
// 于是退不出、开不了存档，只能杀进程。
// 所以这里断言的是结构：把手必须是全屏元素的后代。
{
    const page = await newPage(true);
    await page.goto(`${BASE}/play/${GAME_ID}`, { waitUntil: 'networkidle2' });
    await page.waitForSelector('.handle', { timeout: 8000 }).catch(() => {});
    await enterRunningState(page);

    // 先唤出工具条，再点它的全屏按钮 —— requestFullscreen 需要真实手势
    const h = await page.$('.handle');
    const hb = await h.boundingBox();
    await page.touchscreen.tap(hb.x + hb.width / 2, hb.y + hb.height / 2);
    await waitForToolbar(page);

    // 用 getBoundingClientRect 而不是 elementHandle.boundingBox()：
    // deviceScaleFactor=3 下后者给的坐标和 touchscreen.tap 期望的视口坐标对不上，
    // 点了个空位置，全屏自然进不去。
    const fsBox = await page.evaluate(() => {
        const btn = [...document.querySelectorAll('.bar button')]
            .find((e) => /全屏/.test(e.textContent));
        return btn ? btn.getBoundingClientRect().toJSON() : null;
    });
    ok('全屏：工具条里有全屏按钮', fsBox);
    if (fsBox) {
        await page.touchscreen.tap(fsBox.x + fsBox.width / 2, fsBox.y + fsBox.height / 2);
    }
    await new Promise((r) => setTimeout(r, 900));

    const fs = await page.evaluate(() => {
        const el = document.fullscreenElement || document.webkitFullscreenElement;
        return { active: !!el, isStage: !!el?.classList.contains('stage') };
    });
    ok('全屏：已进入元素全屏', fs.active);
    ok('全屏：全屏目标是 .stage', fs.isStage);

    // 等自动收起（3s），把手应重新出现
    await new Promise((r) => setTimeout(r, 3400));

    const reach = await page.evaluate(() => {
        const el = document.fullscreenElement || document.webkitFullscreenElement;
        const handle = document.querySelector('.handle');
        if (!handle) return { handle: false };
        const r = handle.getBoundingClientRect();
        const hit = document.elementFromPoint(r.x + r.width / 2, r.y + r.height / 2);
        return {
            handle: true,
            // 关键断言：不在全屏子树里就一定被 ::backdrop 吞掉
            inFsSubtree: !!el && el.contains(handle),
            onTop: !!(hit && hit.closest('.handle'))
        };
    });
    ok('全屏：把手仍然渲染', reach.handle);
    ok('全屏：把手在全屏子树内（否则被 ::backdrop 吞掉）', reach.inFsSubtree);
    ok('全屏：把手未被遮挡（命中测试）', reach.onTop);

    if (reach.handle) {
        const h2 = await page.$('.handle');
        const b2 = await h2.boundingBox();
        await page.touchscreen.tap(b2.x + b2.width / 2, b2.y + b2.height / 2);
        ok('全屏：点把手能再次展开工具条', await waitForToolbar(page));
    }
    await page.close();
}

// --- 桌面不受影响 ---
{
    const page = await newPage(false);
    await page.goto(`${BASE}/play/${GAME_ID}`, { waitUntil: 'networkidle2' });
    await new Promise((r) => setTimeout(r, 1200));
    await enterRunningState(page);

    ok('桌面：不渲染把手', !(await page.$('.handle')));
    await page.mouse.move(640, 2);
    ok('桌面：鼠标移到顶端能展开', await waitForToolbar(page));
    await page.close();
}

await browser.close();

// 清掉测试条目，别把库弄脏
await fetch(`${BASE}/api/admin/games/${GAME_ID}`, {
    method: 'DELETE',
    headers: { Cookie: cookie }
}).catch(() => {});

let bad = 0;
for (const [name, pass] of results) {
    console.log(`  ${pass ? 'PASS' : 'FAIL'}  ${name}`);
    if (!pass) bad++;
}
console.log(bad ? `\n✗ ${bad} 项问题` : '\n✓ 全部通过');
process.exit(bad ? 1 : 0);
