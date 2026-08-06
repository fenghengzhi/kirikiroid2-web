// D1 查询层。行 ↔ API 对象的字段名转换集中在这里，
// 上层拿到的一律是 camelCase 的 JS 对象。

function rowToGame(row) {
    if (!row) return null;
    let tags = [];
    try {
        const parsed = JSON.parse(row.tags || '[]');
        if (Array.isArray(parsed)) tags = parsed.filter((t) => typeof t === 'string');
    } catch {
        // 脏数据不该让整个列表 500，退化成无标签
    }
    return {
        id: row.id,
        title: row.title,
        coverUrl: row.cover_url || '',
        downloadUrl: row.download_url || '',
        entryXp3: row.entry_xp3 || '',
        description: row.description || '',
        tags,
        sortOrder: row.sort_order,
        pinned: row.pinned === 1,
        published: row.published === 1,
        createdAt: row.created_at,
        updatedAt: row.updated_at
    };
}

/** 公开列表：只出已上架的，置顶优先。 */
export async function listPublishedGames(db) {
    const { results } = await db
        .prepare(
            `SELECT * FROM games WHERE published = 1
             ORDER BY pinned DESC, sort_order ASC, created_at DESC`
        )
        .all();
    return (results || []).map(rowToGame);
}

/** 后台列表：含下架条目。 */
export async function listAllGames(db) {
    const { results } = await db
        .prepare(
            `SELECT * FROM games
             ORDER BY pinned DESC, sort_order ASC, created_at DESC`
        )
        .all();
    return (results || []).map(rowToGame);
}

export async function getGame(db, id, { publishedOnly = false } = {}) {
    const sql = publishedOnly
        ? 'SELECT * FROM games WHERE id = ? AND published = 1'
        : 'SELECT * FROM games WHERE id = ?';
    return rowToGame(await db.prepare(sql).bind(id).first());
}

/** 仅取封面地址，供 /api/cover/:id 回源。故意不暴露其余字段。 */
export async function getCoverUrl(db, id) {
    const row = await db
        .prepare('SELECT cover_url FROM games WHERE id = ? AND published = 1')
        .bind(id)
        .first();
    return row?.cover_url || null;
}

export async function insertGame(db, game) {
    const now = Date.now();
    // 新条目排在最后：取当前最大 sort_order + 1
    const maxRow = await db.prepare('SELECT MAX(sort_order) AS m FROM games').first();
    const sortOrder = (maxRow?.m ?? -1) + 1;

    await db
        .prepare(
            `INSERT INTO games
             (id, title, cover_url, download_url, entry_xp3, description, tags,
              sort_order, pinned, published, created_at, updated_at)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`
        )
        .bind(
            game.id, game.title, game.coverUrl, game.downloadUrl, game.entryXp3,
            game.description, JSON.stringify(game.tags),
            sortOrder, game.pinned ? 1 : 0, game.published ? 1 : 0, now, now
        )
        .run();

    return getGame(db, game.id);
}

/**
 * 部分更新。只有 fields 里出现的键会被写入，
 * 避免 PATCH 时未提交的字段被 undefined 覆盖成 null。
 */
export async function updateGame(db, id, fields) {
    const map = {
        title: 'title',
        coverUrl: 'cover_url',
        downloadUrl: 'download_url',
        entryXp3: 'entry_xp3',
        description: 'description',
        sortOrder: 'sort_order'
    };

    const sets = [];
    const values = [];

    for (const [key, column] of Object.entries(map)) {
        if (fields[key] !== undefined) {
            sets.push(`${column} = ?`);
            values.push(fields[key]);
        }
    }
    if (fields.tags !== undefined) {
        sets.push('tags = ?');
        values.push(JSON.stringify(fields.tags));
    }
    for (const key of ['pinned', 'published']) {
        if (fields[key] !== undefined) {
            sets.push(`${key} = ?`);
            values.push(fields[key] ? 1 : 0);
        }
    }

    if (sets.length === 0) return getGame(db, id);

    sets.push('updated_at = ?');
    values.push(Date.now(), id);

    await db.prepare(`UPDATE games SET ${sets.join(', ')} WHERE id = ?`).bind(...values).run();
    return getGame(db, id);
}

export async function deleteGame(db, id) {
    const res = await db.prepare('DELETE FROM games WHERE id = ?').bind(id).run();
    return (res.meta?.changes ?? 0) > 0;
}

/** 拖拽排序：一次 batch 写入全部新次序，避免 N 次往返。 */
export async function reorderGames(db, orderedIds) {
    const now = Date.now();
    const stmt = db.prepare('UPDATE games SET sort_order = ?, updated_at = ? WHERE id = ?');
    await db.batch(orderedIds.map((id, index) => stmt.bind(index, now, id)));
}

/** 批量导入。同 id 覆盖（upsert），便于反复导入同一份 games.json。 */
export async function importGames(db, games) {
    const now = Date.now();
    const maxRow = await db.prepare('SELECT MAX(sort_order) AS m FROM games').first();
    let sortOrder = (maxRow?.m ?? -1) + 1;

    const stmt = db.prepare(
        `INSERT INTO games
         (id, title, cover_url, download_url, entry_xp3, description, tags,
          sort_order, pinned, published, created_at, updated_at)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0, 1, ?, ?)
         ON CONFLICT(id) DO UPDATE SET
           title = excluded.title,
           cover_url = excluded.cover_url,
           download_url = excluded.download_url,
           entry_xp3 = excluded.entry_xp3,
           description = excluded.description,
           tags = excluded.tags,
           updated_at = excluded.updated_at`
    );

    await db.batch(
        games.map((g) =>
            stmt.bind(
                g.id, g.title, g.coverUrl, g.downloadUrl, g.entryXp3,
                g.description, JSON.stringify(g.tags), sortOrder++, now, now
            )
        )
    );

    return games.length;
}
