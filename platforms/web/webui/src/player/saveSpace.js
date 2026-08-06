// 存档空间归属。
//
// 旧实现（js/ui/gallery-app.js）用 'save_' + game.title 当空间 id，
// 于是管理员在后台改一次标题，玩家的存档就"消失"了 —— 引擎去读一个
// 全新的空数据库。这里改绑不可变的 game.id，并对老数据做一次性迁移。

const PREFIX = 'game_';

/** 当前方案：绑 id，标题随便改都不影响。 */
export function spaceIdFor(game) {
    return PREFIX + game.id;
}

/** 旧方案生成的空间名，仅用于迁移探测。 */
function legacySpaceIdFor(game) {
    return 'save_' + (game.title || game.id || 'default');
}

function openSpace(name) {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open('krkr2-space-' + name, 1);
        req.onupgradeneeded = (e) => e.target.result.createObjectStore('files');
        req.onsuccess = (e) => resolve(e.target.result);
        req.onerror = () => reject(req.error);
    });
}

function readAll(db) {
    return new Promise((resolve) => {
        const out = [];
        try {
            const cur = db.transaction('files', 'readonly').objectStore('files').openCursor();
            cur.onsuccess = (e) => {
                const c = e.target.result;
                if (c) { out.push({ path: c.key, data: c.value }); c.continue(); }
                else resolve(out);
            };
            cur.onerror = () => resolve(out);
        } catch {
            resolve(out);
        }
    });
}

function countEntries(db) {
    return new Promise((resolve) => {
        try {
            const req = db.transaction('files', 'readonly').objectStore('files').count();
            req.onsuccess = () => resolve(req.result || 0);
            req.onerror = () => resolve(0);
        } catch {
            resolve(0);
        }
    });
}

/**
 * 把旧的 save_<title> 空间搬进新的 game_<id>。
 *
 * IndexedDB 没有 rename，只能逐条复制。仅在新空间为空且旧空间有数据时执行，
 * 所以重复调用是安全的，也不会覆盖玩家在新空间里的新进度。
 * 旧库保留不删 —— 万一迁移有问题，数据还在。
 */
export async function migrateLegacySpace(game) {
    const legacyName = legacySpaceIdFor(game);
    const targetName = spaceIdFor(game);
    if (legacyName === targetName) return false;

    let legacyDb, targetDb;
    try {
        // 已登记过的空间列表：没出现过就不用白开一个空库
        const known = JSON.parse(localStorage.getItem('krkr2-spaces') || '[]');
        if (!Array.isArray(known) || !known.includes(legacyName)) return false;

        targetDb = await openSpace(targetName);
        if (await countEntries(targetDb) > 0) return false;   // 新空间已有进度

        legacyDb = await openSpace(legacyName);
        const files = await readAll(legacyDb);
        if (files.length === 0) return false;

        await new Promise((resolve, reject) => {
            const tx = targetDb.transaction('files', 'readwrite');
            const store = tx.objectStore('files');
            for (const f of files) store.put(f.data, f.path);
            tx.oncomplete = resolve;
            tx.onerror = () => reject(tx.error);
        });

        console.log(`[saves] 已迁移 ${files.length} 个存档：${legacyName} → ${targetName}`);
        return true;
    } catch (err) {
        // 迁移失败不能挡住游戏启动，最坏情况是玩家看到空存档，旧库仍在
        console.warn('[saves] 迁移失败:', err);
        return false;
    } finally {
        legacyDb?.close();
        targetDb?.close();
    }
}

/** 迁移旧存档后绑定空间。返回实际使用的空间 id。 */
export async function attachSaveSpace(game) {
    await migrateLegacySpace(game);
    const spaceId = spaceIdFor(game);
    await window.KrKr2Engine.setSaveSpace(spaceId, { remember: true, register: true });
    return spaceId;
}
