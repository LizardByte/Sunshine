import { fileURLToPath, URL } from 'node:url'
import fs from 'fs';
import { resolve } from 'path'
import { defineConfig } from 'vite'
import { ViteEjsPlugin } from "vite-plugin-ejs";
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
let header = fs.readFileSync(resolve(__dirname, assetsSrcPath, "template_header.html"))
let headerMain = fs.readFileSync(resolve(__dirname, assetsSrcPath, "template_header_main.html"))
let assetsDstPath = 'src_assets/common/assets/web';

console.log(process.argv);
if(process.argv[2]){
   console.log("Using srcdir from Cmake: " + process.argv[2]);
   assetsSrcPath = process.argv[2]
}
if(process.argv[3]){
    console.log("Using destdir from Cmake: " + + process.argv[3]);
    assetsDstPath = process.argv[3]
 }
// https://vitejs.dev/config/
export default defineConfig({
    resolve: {
        alias: {
            vue: 'vue/dist/vue.esm-bundler.js'
        }
    },
    plugins: [vue(), ViteEjsPlugin({ header, headerMain })],
    root: resolve(__dirname, assetsSrcPath),
    build: {
        outDir: resolve(__dirname, assetsDstPath),
        rollupOptions: {
            input: {
                apps: resolve(__dirname, assetsSrcPath , 'apps.html'),
                config: resolve(__dirname, assetsSrcPath , 'config.html'),
                index: resolve(__dirname, assetsSrcPath , 'index.html'),
                password: resolve(__dirname, assetsSrcPath , 'password.html'),
                pin: resolve(__dirname, assetsSrcPath , 'pin.html'),
                troubleshooting: resolve(__dirname, assetsSrcPath , 'troubleshooting.html'),
                welcome: resolve(__dirname, assetsSrcPath , 'welcome.html'),
            },
        },
    },
})
