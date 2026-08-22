import fs from 'fs';
import { resolve } from 'path'
import { defineConfig, loadEnv } from 'vite'
import { codecovVitePlugin } from "@codecov/vite-plugin";
import vue from '@vitejs/plugin-vue'
import vueJsx from '@vitejs/plugin-vue-jsx'
import vueDevTools from 'vite-plugin-vue-devtools'
import basicSsl from '@vitejs/plugin-basic-ssl'
import process from 'process'

let assetsSrcPath = 'src_assets/common/assets/web';
let assetsDstPath = 'build/assets/web';

if (process.env.SUNSHINE_BUILD_HOMEBREW) {
    console.log("Building for homebrew, using default paths")
}
else {
    // If the paths supplied in the environment variables contain any symbolic links
    // at any point in the series of directories, the entire build will fail with
    // a cryptic error message like this:
    //     RollupError: The "fileName" or "name" properties of emitted chunks and assets
    //     must be strings that are neither absolute nor relative paths.
    // To avoid this, we resolve the potential symlinks using `fs.realpathSync` before
    // doing anything else with the paths.
    if (process.env.SUNSHINE_SOURCE_ASSETS_DIR) {
        let path = resolve(fs.realpathSync(process.env.SUNSHINE_SOURCE_ASSETS_DIR), "common/assets/web");
        console.log("Using srcdir from Cmake: " + path);
        assetsSrcPath = path;
    }
    if (process.env.SUNSHINE_ASSETS_DIR) {
        let path = resolve(fs.realpathSync(process.env.SUNSHINE_ASSETS_DIR), "assets/web");
        console.log("Using destdir from Cmake: " + path);
        assetsDstPath = path;
    }
}

// https://vitejs.dev/config/
export default defineConfig(({ command, mode }) => {
    const env = loadEnv(mode, process.cwd(), 'VITE_');
    const backendUrl = env.VITE_BACKEND_URL;

    return {
        resolve: {
            alias: {
                vue: 'vue/dist/vue.esm-bundler.js',
                // Frontend `src/`, not the repo-root C++ `src/` directory.
                '@': resolve(assetsSrcPath, 'src'),
            }
        },
        base: './',
        plugins: [
            // Files like /images/*.png live in public/, and Vite forbids importing publicDir
            // contents as JS modules in dev, so don't let Vue's compiler turn
            // <img src="/images/..."> into a build-time import.
            vue({ template: { transformAssetUrls: false } }),
            vueJsx(),
            vueDevTools(),
            // HTTPS only matters for `vite dev`; the production build is served by the backend.
            ...(command === 'serve' ? [basicSsl()] : []),
            // The Codecov vite plugin should be after all other plugins
            codecovVitePlugin({
                enableBundleAnalysis: true,
                bundleName: "sunshine",
                uploadToken: process.env.CODECOV_TOKEN,
                gitService: "github",
            }),
        ],
        root: resolve(assetsSrcPath),
        server: {
            // Bind address/port for `vite dev`; override via VITE_DEV_HOST / VITE_DEV_PORT in .env
            // (e.g. VITE_DEV_HOST=0.0.0.0 for devcontainer/Codespaces port forwarding).
            host: env.VITE_DEV_HOST,
            port: Number(env.VITE_DEV_PORT) ?? 5173,
            // WSL2/devcontainer bind mounts don't reliably propagate inotify events, so poll instead.
            watch: {
                usePolling: true,
            },
            proxy: {
                '/api': {
                    target: backendUrl,
                    changeOrigin: true,
                    secure: false, // the backend uses a self-signed certificate
                },
            },
        },
        build: {
            outDir: resolve(assetsDstPath),
            rollupOptions: {
                input: {
                    index: resolve(assetsSrcPath, 'index.html'),
                },
            },
        },
    };
})
