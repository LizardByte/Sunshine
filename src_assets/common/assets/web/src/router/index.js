import { createRouter, createWebHistory } from 'vue-router'
import { Home, Info, Layers, Lock, Settings, Star } from '@lucide/vue'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  linkActiveClass: 'active',
  linkExactActiveClass: 'active',
  routes: [
    {
      path: '/',
      name: 'index',
      component: () => import('../views/IndexView.vue'),
      meta: { nav: { icon: Home, label: 'navbar.home' } },
    },
    {
      path: '/pin',
      name: 'pin',
      component: () => import('../views/PinView.vue'),
      meta: { nav: { icon: Lock, label: 'navbar.pin' } },
    },
    {
      path: '/apps',
      name: 'apps',
      component: () => import('../views/AppsView.vue'),
      meta: { nav: { icon: Layers, label: 'navbar.applications' } },
    },
    {
      path: '/featured',
      name: 'featured',
      component: () => import('../views/FeaturedView.vue'),
      meta: { nav: { icon: Star, label: 'navbar.featured' } },
    },
    {
      path: '/config',
      name: 'config',
      component: () => import('../views/ConfigView.vue'),
      meta: { nav: { icon: Settings, label: 'navbar.configuration' } },
    },
    {
      path: '/troubleshooting',
      name: 'troubleshooting',
      component: () => import('../views/TroubleshootingView.vue'),
      meta: { nav: { icon: Info, label: 'navbar.troubleshoot' } },
    },
    {
      path: '/password',
      name: 'password',
      component: () => import('../views/PasswordView.vue'),
    },
    {
      path: '/logout',
      name: 'logout',
      component: () => import('../views/LogoutView.vue'),
      meta: { public: true },
    },
    {
      path: '/welcome',
      name: 'welcome',
      component: () => import('../views/WelcomeView.vue'),
      meta: { public: true },
    },
  ],
})

export default router
