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

**Cloudflare 的构建环境不编译 C++**，所以这里要解决"引擎从哪来"：

```
Build command:
  curl -fsSL "$ENGINE_URL" | tar xz -C /tmp/engine &&
  KRKR2_ENGINE_DIR=/tmp/engine KRKR2_ENGINE_BASE="/engine/$ENGINE_VERSION/" npm run build
```

`$ENGINE_VERSION` 是 `build-web.yml` 上传时用的 commit sha 前 12 位（任务跑完
会打在 job summary 里）。这样 CF 只构建 343 KB 的页面，22 MB 的 wasm 既不进
仓库也不进部署包，运行时由 Worker 从 R2 读出来。`index.js` 与 `build-config.js`
必须同源，仍需 `KRKR2_ENGINE_DIR` 在构建时提供 —— 它俩合计 336 KB。

### GitHub Actions

| workflow | 触发 | 干什么 |
|---|---|---|
| `build-webui.yml` | 改动 `platforms/web/webui/**` | `npm ci` + `npm run build` + `wrangler deploy --dry-run` |
| `build-web.yml` | 改动 C++/CMake（已排除本目录） | 编译 wasm 引擎，并把 `index.wasm`/`assets.zip` 传 R2 |

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
  engine.js   /engine/* → R2，引擎大文件按同源返回
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

**`assetBase` 与 `engineBase` 的分工。**
`index.wasm` 已 21.9 MiB，占 Workers 静态资源 25 MiB 单文件上限的 87.5%。
把大文件挪到 R2 是迟早的事，所以拆成两个旋钮而不是一个：

| 旋钮 | 管什么 | 去哪 |
|---|---|---|
| `assetBase` | `index.js`（emscripten glue）、`build-config.js` | 必须同源，跟页面一起部署 |
| `engineBase` | `index.wasm`、`assets.zip` | R2，经 `/engine/*` 同源读出 |

`index.js` 不能跨域：glue 顶部的 `_scriptName` 取自 `document.currentScript.src`，
pthread worker 脚本按它定位，跨域会被同源策略挡下。`build-config.js` 同样，
它带着 CMake 写入的 `initialMemory` 权威值，拿不到就退回兜底的 64 MiB，
ASan 构建下直接 `LinkError`。这两个加起来 336 KB，留在同源不心疼。

启用方式是构建期环境变量：

```bash
KRKR2_ENGINE_BASE=/engine/<版本>/ npm run build
```

设了之后这两个大文件**不进 `dist`**，部署产物从 30 MB 降到约 680 KB。

**为什么走 `/engine/*` 而不是让浏览器直连 R2 公开域名。**
页面开着跨源隔离（`COOP: same-origin` + `COEP: require-corp`）——这是引擎用
SharedArrayBuffer 跑 pthread 的前提。`require-corp` 的字面含义是"本页加载的
每一个跨源资源都必须显式声明同意被嵌入"，于是直连 R2 就得配自定义域名 +
`Access-Control-Allow-Origin` + `Cross-Origin-Resource-Policy` 三样东西。
走同源则一样都不用配：`worker/headers.js` 已经给所有响应加了 COOP/COEP，
service worker 的"同源 cache-first"分支也自动覆盖。

代价是字节流经 Worker，但引擎是**整取**（wasm 一次 `instantiateStreaming`，
zip 一次 `fetch` 转 Blob），不是 `js/loaders/remote.js` 那种对多 GB 游戏包的
Range 懒加载——下面"游戏包不经 Worker"那条在这里不适用。且 25 MiB 上限照样
绕过：从 R2 binding 读出的响应不算静态资源（R2 单对象上限 5 TB）。

`build-web.yml` 会在引擎构建后自动上传，按 commit sha 前 12 位分目录。
需要仓库配 `CLOUDFLARE_API_TOKEN` 与 `CLOUDFLARE_ACCOUNT_ID`，桶名用仓库变量
`R2_ENGINE_BUCKET` 覆盖（默认 `krkr2-engine`）。没配就跳过，不会让构建失败。

> 分目录不是洁癖：这两个文件名不带内容哈希，同一路径覆盖更新会让已缓存的
> 客户端拿到新旧混搭的 glue/wasm。换版即换 URL 才能安全地 `immutable` 长缓存。

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
