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

// KRKR2_ENGINE_BASE：把两个大文件交给 R2（或任何公开桶/CDN），只发页面。
//
// 设了之后 index.wasm(21.9MiB) 与 assets.zip(7.8MiB) 不再进 dist，部署产物
// 从 30MB 降到约 680KB —— Workers 静态资源 25MiB 单文件上限从此不适用
// （index.wasm 目前已占 87.5%）。运行时由 js/config.js 的 engineBase 接手：
// wasm 走 Module.locateFile，assets.zip 走 vlfs-bridge 的 fetch。
//
// index.js 与 build-config.js 永远留在同源，不受此开关影响：前者跨域会踩
// _scriptName 定位 pthread worker 的坑，后者带着 initialMemory 权威值
// （拿不到就会用兜底的 64MiB，ASan 构建下直接 LinkError）。
const ENGINE_BASE = process.env.KRKR2_ENGINE_BASE || '';
const REMOTE_FILES = ENGINE_BASE ? ['index.wasm', 'assets.zip'] : [];

// 判据是 index.js 而非 index.wasm：走 R2 时本地可以只有 glue 没有 wasm，
// 那依然是一次完整可用的页面构建。
const hasEngine = existsSync(resolve(ENGINE_DIR, 'index.js'));

if (!hasEngine) {
    console.warn(
        `\n[webui] 未找到引擎产物（${ENGINE_DIR}/index.js）。\n` +
        `        页面可正常构建和浏览，但启动游戏会失败。\n` +
        `        需要完整产物时先 cmake --build out/web/release，\n` +
        `        或用 KRKR2_ENGINE_DIR 指向已有的构建输出。\n`
    );
} else if (ENGINE_BASE) {
    console.log(`[webui] engineBase = ${ENGINE_BASE}` +
                `（index.wasm / assets.zip 不进 dist，运行时从该地址取）`);
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
                if (!hasEngine) return;
                const { readFile } = await import('node:fs/promises');
                let wroteBuildConfig = false;

                for (const name of ENGINE_FILES) {
                    // 走 R2 的不进 dist —— 这就是部署产物从 30MB 降到 680KB 的地方
                    if (REMOTE_FILES.includes(name)) continue;
                    const p = resolve(ENGINE_DIR, name);
                    if (!existsSync(p)) continue;

                    let source = await readFile(p);
                    // build-config.js 由 CMake 生成、顶上写着"请勿手改"，而
                    // engineBase 是部署期信息，引擎构建时根本不知道。所以在这里
                    // 追加，而不是去改 gen_build_config.cmake。
                    if (name === 'build-config.js' && ENGINE_BASE) {
                        source = Buffer.concat([source, Buffer.from(
                            '\n// 以下由 vite.config.js 按 KRKR2_ENGINE_BASE 追加\n' +
                            'window.KRKR2_BUILD_CONFIG.engineBase = ' +
                            `${JSON.stringify(ENGINE_BASE)};\n`
                        )]);
                        wroteBuildConfig = true;
                    }
                    this.emitFile({ type: 'asset', fileName: name, source });
                }

                // ENGINE_DIR 里没有 build-config.js 时的兜底：engineBase 不能丢，
                // 丢了 wasm 就会去同源找，而它已经不在那儿了。
                if (ENGINE_BASE && !wroteBuildConfig) {
                    this.emitFile({
                        type: 'asset',
                        fileName: 'build-config.js',
                        source: '// 由 vite.config.js 生成（ENGINE_DIR 无 build-config.js）\n' +
                                'window.KRKR2_BUILD_CONFIG = window.KRKR2_BUILD_CONFIG || {};\n' +
                                'window.KRKR2_BUILD_CONFIG.engineBase = ' +
                                `${JSON.stringify(ENGINE_BASE)};\n`
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
