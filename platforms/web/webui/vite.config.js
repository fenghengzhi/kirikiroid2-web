import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import { resolve } from 'node:path';
import { existsSync, createReadStream } from 'node:fs';

// 引擎产物（index.js / index.wasm / assets.zip / build-config.js）由 CMake 生成，
// 体积 30MB 且每次编译都变，不入库。构建时从 CMake 输出目录取。
//
// 找不到也照常构建：只改前端时不必先跑一遍 emscripten（编译一次十几分钟），
// 页面能起、能进后台，只有真正启动游戏那一步会缺 wasm。CI 里前端 job 就靠这个。
const ENGINE_DIR = process.env.KRKR2_ENGINE_DIR ||
    resolve(__dirname, '../../../out/web/release');
const ENGINE_FILES = ['index.js', 'index.wasm', 'assets.zip',
                      'build-config.js', 'index.worker.js'];

// KRKR2_ENGINE_BASE：引擎产物全部交给 R2，页面构建只产页面。
//
// 设了之后 ENGINE_FILES 一个都不进 dist，部署产物从 30MB 降到约 350KB。
// 运行时全部经 Worker 的 /engine/* 路由读出（见 worker/engine.js）——
// 那是**同源**，所以 emscripten glue 的 _scriptName 定位 pthread worker
// 不受影响，index.js 也可以一起搬走。
//
// 这是关键：正因为 /engine/* 同源，Cloudflare 的构建环境（编译不了 C++）
// 完全不需要任何引擎产物在场，只要知道版本号这一个字符串即可。
// build-config.js 的 <script> 地址由下面的 transformIndexHtml 改写。
const ENGINE_BASE = process.env.KRKR2_ENGINE_BASE || '';
const REMOTE_FILES = ENGINE_BASE ? ENGINE_FILES : [];

// 只在"不走 R2"时才需要本地引擎产物
const hasEngine = ENGINE_BASE || existsSync(resolve(ENGINE_DIR, 'index.js'));

if (!hasEngine) {
    console.warn(
        `\n[webui] 未找到引擎产物（${ENGINE_DIR}/index.js）。\n` +
        `        页面可正常构建和浏览，但启动游戏会失败。\n` +
        `        需要完整产物时先 cmake --build out/web/release，\n` +
        `        或用 KRKR2_ENGINE_DIR 指向已有的构建输出，\n` +
        `        或用 KRKR2_ENGINE_BASE=/engine/ 走 R2。\n`
    );
} else if (ENGINE_BASE) {
    console.log(`[webui] engineBase = ${ENGINE_BASE}` +
                `（引擎产物全部不进 dist，运行时经 Worker 的 /engine/* 读出）`);
}

// MPA：三个入口各自独立成页。这不是审美选择 —— 引擎是硬单例
// （public/js/engine/boot-guards.js 的 Web Lock + engine.js 的 booted 标志），
// wasm runtime 进入 main() 后无法在同页销毁重建，所以"退出游戏"必须整页卸载。
// 附带收益：画廊页完全不加载引擎脚本，浏览列表不再先下 22MB wasm。
export default defineConfig({
    publicDir: 'public',

    build: {
        outDir: 'dist',
        emptyOutDir: true,
        // 不产 .vite/manifest.json：没人读它。gen-sw.js 是直接遍历 dist 目录
        // 拿文件名的（readdirSync），不依赖 manifest；开着只会把一份构建产物
        // 清单一起部署上去。
        rollupOptions: {
            input: {
                gallery: resolve(__dirname, 'index.html'),
                player: resolve(__dirname, 'play.html'),
                admin: resolve(__dirname, 'admin.html')
            }
        }
    },

    plugins: [
        vue(),
        // 把 CMake 产物并进 dist / dev server —— 放 publicDir 会要求它们进版本库，
        // 这里直接从构建目录取，源码树保持干净。
        // 注意不含 vlfs.js：那个是手写源码，已经在 public/ 里，重复 emit 会冲突。
        {
            name: 'krkr2-engine-artifacts',
            // index.worker.js 只在 pthread 构建里出现，缺了不算错

            // build-config.js 是 <script src> 静态引用的，而它自己才带着
            // engineBase —— 鸡生蛋问题。所以地址只能在构建期改写进 HTML。
            // /js/* 那些是 public/ 里的手写源码，照常同源，不动。
            transformIndexHtml(html) {
                if (!ENGINE_BASE) return html;
                return html.replace(/(<script\s+src=")\/build-config\.js(")/g,
                                    `$1${ENGINE_BASE}build-config.js$2`);
            },

            configureServer(server) {
                if (!hasEngine) return;
                server.middlewares.use((req, res, next) => {
                    const name = (req.url || '').split('?')[0].replace(/^\//, '');
                    if (!ENGINE_FILES.includes(name)) return next();
                    const p = resolve(ENGINE_DIR, name);
                    if (!existsSync(p)) return next();
                    createReadStream(p).pipe(res);
                });
            },
            async generateBundle() {
                // 走 R2 时一个引擎文件都不进 dist —— 这就是部署产物从 30MB
                // 降到约 350KB 的地方，也是 Cloudflare 构建环境（编译不了 C++）
                // 能独立产出可用页面的原因。
                if (!hasEngine || ENGINE_BASE) return;

                const { readFile } = await import('node:fs/promises');
                for (const name of ENGINE_FILES) {
                    const p = resolve(ENGINE_DIR, name);
                    if (!existsSync(p)) continue;
                    this.emitFile({
                        type: 'asset',
                        fileName: name,
                        source: await readFile(p)
                    });
                }
            }
        }
    ],

    server: {
        port: 5173,
        // 引擎依赖 SharedArrayBuffer，dev server 也必须发这两个头，
        // 否则本地开发时 wasm 起不来。生产环境由 worker/headers.js 负责。
        headers: {
            'Cross-Origin-Opener-Policy': 'same-origin',
            'Cross-Origin-Embedder-Policy': 'require-corp'
        }
    }
});
