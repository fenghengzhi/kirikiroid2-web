#!/usr/bin/env node
// 把仓库里现有的 games.json 转成可直接灌进 D1 的 SQL。
//
//   node scripts/seed-from-games-json.js            # 默认读 ./games.json
//   node scripts/seed-from-games-json.js my.json
//   wrangler d1 execute krkr2-games --local --file=seed.sql
//   wrangler d1 execute krkr2-games --remote --file=seed.sql
//
// 后台也有「导入 JSON」按钮，两条路都行；命令行这条可复现、适合初始化。

import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const input = resolve(process.argv[2] || 'games.json');
const output = resolve(process.argv[3] || 'seed.sql');

let games;
try {
    games = JSON.parse(readFileSync(input, 'utf-8'));
} catch (err) {
    console.error(`读取 ${input} 失败: ${err.message}`);
    process.exit(1);
}

if (!Array.isArray(games)) {
    console.error('文件内容必须是一个 JSON 数组。');
    process.exit(1);
}

/** SQLite 字符串字面量转义：单引号翻倍。 */
const q = (v) => `'${String(v ?? '').replace(/'/g, "''")}'`;

const now = Date.now();
const rows = [];

games.forEach((g, i) => {
    if (!g?.title) {
        console.warn(`跳过第 ${i + 1} 条：缺少 title`);
        return;
    }
    const id = g.id || `game_${now}_${i}`;
    const tags = Array.isArray(g.tags) ? g.tags : [];

    rows.push(
        `  (${q(id)}, ${q(g.title)}, ${q(g.coverUrl)}, ${q(g.downloadUrl)}, ` +
        `${q(g.entryXp3)}, ${q(g.description)}, ${q(JSON.stringify(tags))}, ` +
        `${i}, 0, 1, ${now}, ${now})`
    );
});

if (rows.length === 0) {
    console.error('没有可导入的有效条目。');
    process.exit(1);
}

// ON CONFLICT 更新：重复执行同一份 seed 不会产生重复条目
const sql = `-- 由 scripts/seed-from-games-json.js 从 ${input} 生成
-- 重复执行安全：同 id 会被覆盖而非重复插入。

INSERT INTO games
  (id, title, cover_url, download_url, entry_xp3, description, tags,
   sort_order, pinned, published, created_at, updated_at)
VALUES
${rows.join(',\n')}
ON CONFLICT(id) DO UPDATE SET
  title        = excluded.title,
  cover_url    = excluded.cover_url,
  download_url = excluded.download_url,
  entry_xp3    = excluded.entry_xp3,
  description  = excluded.description,
  tags         = excluded.tags,
  updated_at   = excluded.updated_at;
`;

writeFileSync(output, sql);
console.log(`✓ 已写出 ${output}（${rows.length} 条）\n`);
console.log('接下来：');
console.log(`  wrangler d1 execute krkr2-games --local  --file=${output}`);
console.log(`  wrangler d1 execute krkr2-games --remote --file=${output}`);
