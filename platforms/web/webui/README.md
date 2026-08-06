# Kirikiroid2 Web

浏览器里跑 Kirikiri2（吉里吉里2）视觉小说引擎。Vue 3 前端 + Cloudflare Worker 后端，
游戏库配置存 D1，管理后台独立路径 + 密码保护。

---

## 快速开始（本地）

需要 **Node 22+**（Wrangler 4 的硬性要求）。

```bash
npm install

# 1. 建库并跑迁移
npx wrangler d1 create krkr2-games          # 把返回的 uuid 填进 wrangler.jsonc
npx wrangler kv namespace create RATE_LIMIT  # 同样把 id 填进去
npm run db:local

# 2. 生成本地开发用的密码与签名密钥
node scripts/hash-password.js '你的密码'     # 按提示写入 .dev.vars

# 3. 构建并启动
npm run build
npx wrangler dev
```

打开 http://localhost:8787 —— 初始库是空的，去 `/admin` 添加游戏。

---

## 部署

### 前置：建资源、设密码

```bash
npx wrangler d1 create krkr2-games            # 把 uuid 填进 wrangler.jsonc
npx wrangler kv namespace create RATE_LIMIT   # 把 id 填进 wrangler.jsonc
npm run db:remote

node scripts/hash-password.js '你的密码' --raw | npx wrangler secret put ADMIN_PASSWORD_HASH
openssl rand -base64 32 | npx wrangler secret put SESSION_SECRET
```

`--raw` 是必要的：`echo`/`console.log` 会带尾部换行，混进 secret 后
base64 段解不出来，表现为"密码明明对却一直登录失败"。

> PBKDF2 轮数固定 100000 —— **Workers 的硬性上限**，超过会抛
> `NotSupportedError: iteration counts above 100000 are not supported`。
> OWASP 对 PBKDF2-SHA256 的建议值更高，但平台不允许。

`wrangler.jsonc` 里的 id 不换掉，部署一定失败（构建结束时会有提示）。
改密码就是重新 `wrangler secret put ADMIN_PASSWORD_HASH`，没有用户表要维护。

### 从本地部署

```bash
npm run deploy:local     # 构建 + 部署
```

### 从 Cloudflare Workers Builds（连 Git 仓库自动部署）

面板里 **三项都要设**：

| 字段 | 值 |
|---|---|
| Root directory | `platforms/web/webui` |
| Build command | `npm run build` |
| Deploy command | `npx wrangler deploy` |

Root directory 容易漏 —— 本工程在仓库里是 `platforms/web/webui/` 子目录，
仓库根目录是 C++ 引擎，没有 package.json。不设它构建会直接找不到工程。

`dist/` 在 `.gitignore` 里，仓库中不存在，必须由 build command 现场生成。
只配 deploy command 会报 `assets.directory ... does not exist`。

Node 版本需 22+（Wrangler 4 的要求）；Cloudflare 构建环境默认已满足。

构建时引擎产物（`index.wasm` 等）不在场，Vite 插件会降级为"只产页面"并
打印警告 —— 页面能开，但启动游戏会失败。要部署能真正跑游戏的版本，
得先把引擎产物放进 `out/web/release`（或 `KRKR2_ENGINE_DIR` 指向的目录），
详见根 README。

### GitHub Actions

| workflow | 触发 | 干什么 |
|---|---|---|
| `build-webui.yml` | 改动 `platforms/web/webui/**` | `npm ci` + `npm run build` + `wrangler deploy --dry-run` |
| `build-web.yml` | 改动 C++/CMake（已排除本目录） | 编译 wasm 引擎 |

两者互不触发：改一行 `.vue` 不会启动 emscripten（十几分钟），
改 `cpp/` 也不会跑前端构建。`build-webui.yml` 不部署 —— 线上仍由上面的
Workers Builds 负责，Actions 只是在合并前先把构建/配置错误挡下来。

### 导入现有的 games.json

两条路，任选：

```bash
# A. 命令行（可复现，适合初始化）
node scripts/seed-from-games-json.js games.json seed.sql
npx wrangler d1 execute krkr2-games --remote --file=seed.sql

# B. 后台 UI：/admin → 导入 JSON
```

两者都按 `id` upsert，重复导入不会产生重复条目。

---

## 结构

```
index.html  play.html  admin.html   三个 MPA 入口
src/
  gallery/    游戏库（不加载引擎，浏览列表不下 22MB wasm）
  player/     播放页：引擎包装、边缘唤出工具条、全屏、存档空间
  admin/      后台：登录壳 + 动态 import 的管理面板
  shared/     API 客户端、PWA 注册
  styles/     设计令牌（tokens.css 是唯一颜色来源）
worker/
  index.js    路由 + 安全头
  auth.js     PBKDF2 校验、HMAC session、限速
  api.js      公开读 / 认证 / 后台写
  db.js       D1 查询
public/       引擎层，原样输出不经打包
  index.js index.wasm assets.zip vlfs.js
  js/engine/**  js/loaders/**  js/storage/**
```

**引擎层（`public/js/engine|loaders|storage`、`vlfs.js`）基本没动。**
只碰了两个文件：`vlfs-bridge.js` 把 `assets.zip` 的取址改用 `assetBase`，
`config.js` 新增 `assetBase` 配置项。其余全部逐字节未改。
它通过 `window.KrKr2Engine` 与前端解耦，前端整体替换也不影响它。

---

## 几个不显然的设计点

**为什么是 MPA 而不是 Vue Router 单页。**
引擎是硬单例：`boot-guards.js` 用 Web Lock 抢占独占锁（生命周期绑 Document），
`engine.js` 的 `booted` 标志让 `boot()` 幂等，emscripten runtime 进 `main()` 后
无法在同页销毁重建。所以"退出游戏"必须整页卸载。顺带的好处是画廊页完全不加载引擎。

**`assetBase` 是什么。**
`index.wasm` 已 21.9 MiB，Workers 静态资源单文件上限 25 MiB。
把这几个大文件挪到 R2 时，改 `build-config.js` 里的 `assetBase` 一个值即可。
构建脚本会在文件超过 24 MiB 时报错提醒。

> 切到跨域时还需一步：emscripten glue 用 `_scriptName` 定位 pthread worker 脚本，
> 跨域会被同源策略挡下，得同时设 `Module.mainScriptUrlOrBlob` 指向同源 Blob URL。

**为什么封面要经 `/api/cover/:id` 代理。**
`COEP: require-corp`（SharedArrayBuffer 的前提）下，第三方图床不发
`Cross-Origin-Resource-Policy` 头，`<img>` 一律加载失败。代理只按 id 从 D1 取地址
回源，不接受任何 URL 参数，因此没有 SSRF 面。

**存档空间绑 id 而非 title。**
旧实现用 `'save_' + game.title` 命名，管理员改一次标题玩家存档就"消失"。
现在绑不可变的 `game.id`，并对老数据做一次性迁移（旧库保留不删）。

**游戏包不经 Worker。**
`js/loaders/remote.js` 的 HTTP Range 懒加载是核心优化，单个包可达数 GB，
必须直连 R2。

---

## 测试

```bash
npx wrangler dev        # 另开一个终端
npm test
```

覆盖：三个页面的渲染与控制台错误、后台登录全流程、未登录不下发后台 chunk、
离线可用性、SW 不缓存 API、存档迁移（直接 import 真实模块，不是复刻逻辑）。

---

## 已知边界

- **iOS Safari 无元素全屏**，降级为伪全屏（铺满视口 + `viewport-fit=cover`），
  地址栏无法隐藏。想要真全屏只能"添加到主屏幕"走 PWA standalone。
- **登录限速用 KV**，最终一致，并发下计数可能少算几次。对暴力破解这个量级够用，
  不值得为此上 Durable Object。
- **`/admin` 离线不可用**，刻意为之：展示一个连不上 API 的登录框没有意义。
