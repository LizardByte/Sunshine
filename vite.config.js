import { fileURLToPath, URL } from 'node:url'
import fs from 'fs';
import { resolve } from 'path'
import { defineConfig } from 'vite'
import { ViteEjsPlugin } from "vite-plugin-ejs";
import vue from '@vitejs/plugin-vue'


/**
 * Before actually building the pages with Vite, we do an intermediate build step using ejs
 * Importing this separately and joining them using ejs 
 * allows us to split some repeating HTML that cannot be added 
 * by Vue itself (e.g. style/script loading, common meta head tags, Widgetbot)
 * The vite-plugin-ejs handles this automatically
 */
let header = fs.readFileSync(resolve(__dirname, "src_assets/common/assets/web/template_header.html"))
let headerMain = fs.readFileSync(resolve(__dirname, "src_assets/common/assets/web/template_header_main.html"))
// https://vitejs.dev/config/
export default defineConfig({
    resolve: {
        alias: {
            vue: 'vue/dist/vue.esm-bundler.js'
        }
    },
    plugins: [vue(), ViteEjsPlugin({ header, headerMain })],
    root: resolve(__dirname, "src_assets/common/assets/web"),
    build: {
        outDir: resolve(__dirname, "build/assets/web"), //TODO Handle SRC_PATH by CMAKE
        rollupOptions: {
            input: {
                apps: resolve(__dirname, 'src_assets/common/assets/web/apps.html'),
                config: resolve(__dirname, 'src_assets/common/assets/web/config.html'),
                index: resolve(__dirname, 'src_assets/common/assets/web/index.html'),
                password: resolve(__dirname, 'src_assets/common/assets/web/password.html'),
                pin: resolve(__dirname, 'src_assets/common/assets/web/pin.html'),
                troubleshooting: resolve(__dirname, 'src_assets/common/assets/web/troubleshooting.html'),
                welcome: resolve(__dirname, 'src_assets/common/assets/web/welcome.html'),
            },
        },
    },
})
