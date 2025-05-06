import { createWebHistory, createRouter } from 'vue-router'

import IndexView from '../views/IndexView.vue'
import PinView from '../views/PinView.vue'
import AppsView from '../views/AppsView.vue'
import ConfigView from '../views/ConfigView.vue'
import PasswordView from '../views/PasswordView.vue'
import TroubleshootingView from '../views/TroubleshootingView.vue'
import WelcomeView from '../views/WelcomeView.vue'

const routes = [
  { path: '/', component: IndexView },
  { path: '/pin', component: PinView },
  { path: '/apps', component: AppsView },
  { path: '/config', component: ConfigView },
  { path: '/password', component: PasswordView},
  { path: '/troubleshooting', component: TroubleshootingView},
  { path: '/welcome', component: WelcomeView, meta: {
    hideNavbar: true
  }},
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

export default router