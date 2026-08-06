-- 游戏库。初始为空，条目全部经 /admin 后台录入。
--
-- tags 存 JSON 数组字符串而非关联表：标签只用于展示和前端筛选，
-- 量级在几十条以内，拆表带来的 join 成本不值当。

CREATE TABLE IF NOT EXISTS games (
  id           TEXT PRIMARY KEY,
  title        TEXT NOT NULL,
  cover_url    TEXT,
  download_url TEXT,
  entry_xp3    TEXT,
  description  TEXT,
  tags         TEXT NOT NULL DEFAULT '[]',

  -- 展示顺序：pinned 优先，其次 sort_order 升序，最后按创建时间倒序
  sort_order   INTEGER NOT NULL DEFAULT 0,
  pinned       INTEGER NOT NULL DEFAULT 0,
  -- 下架的条目不出现在公开列表，但后台仍可见（软删除之外的临时隐藏）
  published    INTEGER NOT NULL DEFAULT 1,

  created_at   INTEGER NOT NULL,
  updated_at   INTEGER NOT NULL
);

-- 公开列表查询的覆盖索引：WHERE published=1 ORDER BY pinned DESC, sort_order ASC
CREATE INDEX IF NOT EXISTS idx_games_list
  ON games (published, pinned DESC, sort_order ASC);
