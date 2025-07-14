import { fileURLToPath, URL } from 'node:url'
import fs from 'fs';
import { resolve } from 'path'
import { defineConfig } from 'vite'
import { ViteEjsPlugin } from "vite-plugin-ejs";
import vue from '@vitejs/plugin-vue'
import mkcert from 'vite-plugin-mkcert'

let assetsSrcPath = 'src_assets/common/assets/web';
// 使用开发环境专用的模板头文件
let header = fs.readFileSync(resolve(assetsSrcPath, "template_header_dev.html"))

// https://vitejs.dev/config/
export default defineConfig({
    resolve: {
        alias: {
            vue: 'vue/dist/vue.esm-bundler.js',
            // 添加静态资源别名
            '@fortawesome/fontawesome-free': resolve('node_modules/@fortawesome/fontawesome-free'),
            'bootstrap': resolve('node_modules/bootstrap'),
        }
    },
    plugins: [vue(), mkcert(), ViteEjsPlugin({ 
        header,
        // 添加开发环境变量
        sunshineVersion: {
            version: '0.21.0-dev',
            release: 'development',
            commit: 'dev-build'
        }
    })],
    root: resolve(assetsSrcPath),
    server: {
        https: true,
        port: 3000,
        host: '0.0.0.0',
        open: true,
        cors: true,
        proxy: {
            // 代理Steam API请求解决CORS问题
            '/steam-api': {
                target: 'https://api.steampowered.com',
                changeOrigin: true,
                secure: true,
                rewrite: (path) => path.replace(/^\/steam-api/, ''),
                configure: (proxy, options) => {
                    proxy.on('proxyReq', (proxyReq, req, res) => {
                        console.log('🎮 Steam API请求:', req.method, req.url, '-> https://api.steampowered.com' + req.url.replace('/steam-api', ''));
                    });
                    
                    proxy.on('proxyRes', (proxyRes, req, res) => {
                        console.log('✅ Steam API响应:', req.url, '状态码:', proxyRes.statusCode);
                    });
                }
            },
            // 代理Steam Store API请求
            '/steam-store': {
                target: 'https://store.steampowered.com',
                changeOrigin: true,
                secure: true,
                rewrite: (path) => path.replace(/^\/steam-store/, ''),
                configure: (proxy, options) => {
                    proxy.on('proxyReq', (proxyReq, req, res) => {
                        console.log('🛒 Steam Store请求:', req.method, req.url, '-> https://store.steampowered.com' + req.url.replace('/steam-store', ''));
                    });
                    
                    proxy.on('proxyRes', (proxyRes, req, res) => {
                        console.log('✅ Steam Store响应:', req.url, '状态码:', proxyRes.statusCode);
                    });
                }
            },
            // 代理 API 请求到后端服务器（如果需要）
            '/api': {
                target: 'https://localhost:47990',
                changeOrigin: true,
                secure: false, // 忽略SSL证书验证
                configure: (proxy, options) => {
                    // 如果后端服务器不可用，返回模拟数据
                    proxy.on('error', (err, req, res) => {
                        console.log('API proxy error:', err.message);
                        
                        // 提供模拟数据作为后备
                        const mockResponses = {
                            '/api/config': {
                                platform: "windows",
                                version: "0.21.0-dev",
                                notify_pre_releases: true,
                                locale: "zh_CN",
                                sunshine_name: "Sunshine Development Server",
                                min_log_level: 2,
                                port: 47990,
                                upnp: true,
                                enable_ipv6: false,
                                origin_web_ui_allowed: "pc"
                            },
                            '/api/apps': {
                                apps: [
                                    {
                                        name: "Steam",
                                        output: "steam-output",
                                        cmd: "steam.exe",
                                        "exclude-global-prep-cmd": false,
                                        elevated: false,
                                        "auto-detach": true,
                                        "wait-all": true,
                                        "exit-timeout": 5,
                                        "prep-cmd": [],
                                        "menu-cmd": [],
                                        detached: [],
                                        "image-path": "",
                                        "working-dir": ""
                                    },
                                    {
                                        name: "Notepad",
                                        output: "notepad-output", 
                                        cmd: "notepad.exe",
                                        "exclude-global-prep-cmd": false,
                                        elevated: false,
                                        "auto-detach": true,
                                        "wait-all": true,
                                        "exit-timeout": 5,
                                        "prep-cmd": [],
                                        "menu-cmd": [],
                                        detached: [],
                                        "image-path": "",
                                        "working-dir": ""
                                    }
                                ]
                            },
                        };
                        
                        const mockData = mockResponses[req.url] || { error: 'Mock endpoint not found' };
                        
                        if (req.url === '/api/logs') {
                            res.writeHead(200, { 'Content-Type': 'text/plain' });
                            res.end(mockData);
                        } else {
                            res.writeHead(200, { 'Content-Type': 'application/json' });
                            res.end(JSON.stringify(mockData));
                        }
                    });
                    
                    // 处理成功的代理请求
                    proxy.on('proxyReq', (proxyReq, req, res) => {
                        console.log('🔗 代理请求:', req.method, req.url, '-> https://localhost:47990' + req.url);
                    });
                    
                    proxy.on('proxyRes', (proxyRes, req, res) => {
                        console.log('✅ 代理响应:', req.url, '状态码:', proxyRes.statusCode);
                    });
                }
            }
        },
        // 添加静态资源处理
        fs: {
            allow: [
                // 允许访问node_modules
                resolve('node_modules'),
                // 允许访问源代码目录
                resolve(assetsSrcPath),
                // 允许访问根目录
                resolve('.')
            ]
        }
    },
    build: {
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
    // 开发环境优化
    define: {
        __DEV__: true,
        __PROD__: false,
        // 添加版本信息
        __SUNSHINE_VERSION__: JSON.stringify({
            version: '0.21.0-dev',
            release: 'development',
            commit: 'dev-build'
        })
    },
    css: {
        devSourcemap: true
    }
}) 