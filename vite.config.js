import { fileURLToPath, URL } from 'node:url'
import { resolve } from 'path'
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// https://vitejs.dev/config/
export default defineConfig({
    resolve: {
        alias: {
            vue: 'vue/dist/vue.esm-bundler.js'
        }
    },
    plugins: [vue({
        template: {
            compilerOptions: {
                compatConfig: {
                    MODE: 2
                }
            }
        }
    })],
    root: resolve(__dirname, "src_assets/common/assets/web"),
    build: {
        outDir: resolve(__dirname, "build/assets/web"), //TODO Handle SRC_PATH by CMAKE
        rollupOptions: {
            input: {
                apps: resolve(__dirname, 'src_assets/common/assets/web/apps.html'),
                clients: resolve(__dirname, 'src_assets/common/assets/web/clients.html'),
                config: resolve(__dirname, 'src_assets/common/assets/web/config.html'),
                'header-no-nav': resolve(__dirname, 'src_assets/common/assets/web/header-no-nav.html'),
                header: resolve(__dirname, 'src_assets/common/assets/web/header.html'),
                index: resolve(__dirname, 'src_assets/common/assets/web/index.html'),
                password: resolve(__dirname, 'src_assets/common/assets/web/password.html'),
                pin: resolve(__dirname, 'src_assets/common/assets/web/pin.html'),
                troubleshooting: resolve(__dirname, 'src_assets/common/assets/web/troubleshooting.html'),
                welcome: resolve(__dirname, 'src_assets/common/assets/web/welcome.html'),
            },
        },
    },
})
