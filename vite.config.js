import { fileURLToPath, URL } from 'node:url'
import fs from 'fs';
import { resolve } from 'path'
import { defineConfig } from 'vite'
import { ViteEjsPlugin } from "vite-plugin-ejs";
import { codecovVitePlugin } from "@codecov/vite-plugin";
import vue from '@vitejs/plugin-vue'
import process from 'process'

/**
 * Before actually building the pages with Vite, we do an intermediate build step using ejs
 * Importing this separately and joining them using ejs
 * allows us to split some repeating HTML that cannot be added
 * by Vue itself (e.g. style/script loading, common meta head tags, Widgetbot)
 * The vite-plugin-ejs handles this automatically
 */
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

let header = fs.readFileSync(resolve(assetsSrcPath, "template_header.html"))

// https://vitejs.dev/config/
export default defineConfig({
    resolve: {
        alias: {
            vue: 'vue/dist/vue.esm-bundler.js'
        }
    },
    base: './',
    plugins: [
        vue(),
        ViteEjsPlugin({ header }),
        // The Codecov vite plugin should be after all other plugins
        codecovVitePlugin({
            enableBundleAnalysis: true,
            bundleName: "sunshine",
            uploadToken: process.env.CODECOV_TOKEN,
            gitService: "github",
        }),
    ],
    root: resolve(assetsSrcPath),
    build: {
        outDir: resolve(assetsDstPath),
        rollupOptions: {
            input: {
                apps: resolve(assetsSrcPath, 'apps.html'),
                config: resolve(assetsSrcPath, 'config.html'),
                index: resolve(assetsSrcPath, 'index.html'),
                password: resolve(assetsSrcPath, 'password.html'),
                pin: resolve(assetsSrcPath, 'pin.html'),
                troubleshooting: resolve(assetsSrcPath, 'troubleshooting.html'),
                welcome: resolve(assetsSrcPath, 'welcome.html'),
            },
        },
    },
})
