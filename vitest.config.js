import vue from '@vitejs/plugin-vue'
import { defineConfig } from 'vitest/config'

export default defineConfig({
  plugins: [vue()],
  resolve: {
    alias: {
      vue: 'vue/dist/vue.esm-bundler.js',
    },
  },
  test: {
    coverage: {
      provider: 'v8',
      reporter: ['text', 'json', 'lcov'],
      reportsDirectory: 'coverage',
    },
    environment: 'jsdom',
    include: ['tests/web/**/*.test.js'],
    outputFile: {
      junit: 'junit.xml',
    },
    reporters: ['default', 'junit'],
  },
})
