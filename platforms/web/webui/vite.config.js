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
const hasEngine = existsSync(resolve(ENGINE_DIR, 'index.wasm'));

if (!hasEngine) {
    console.warn(
        `\n[webui] 未找到引擎产物（${ENGINE_DIR}/index.wasm）。\n` +
        `        页面可正常构建和浏览，但启动游戏会失败。\n` +
        `        需要完整产物时先 cmake --build out/web/release，\n` +
        `        或用 KRKR2_ENGINE_DIR 指向已有的构建输出。\n`
    );
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
        // gen-sw.js 要靠它枚举带哈希的产物文件名
        manifest: true,
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
