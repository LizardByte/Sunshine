import { createApp } from 'vue'
import App from '@/App.vue'
import router from '@/router'
import { initApp } from '@/utils/init'

const app = createApp(App)

initApp(app, (app) => {
  app.use(router)
})
