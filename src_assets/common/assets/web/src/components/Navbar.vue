<template>
  <nav class="navbar navbar-expand-lg navbar-sunshine sticky-top">
    <div class="container-fluid">
      <router-link class="navbar-brand d-flex align-items-center" to="/" title="Sunshine">
        <img src="/images/logo-sunshine-45.png" height="45" alt="Sunshine">
        <span v-if="isPublic" class="ms-2 fw-semibold">Sunshine</span>
      </router-link>
      <template v-if="!isPublic">
        <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarSupportedContent"
          aria-controls="navbarSupportedContent" aria-expanded="false" aria-label="Toggle navigation">
          <Menu :size="18" class="icon"></Menu>
        </button>
        <div class="collapse navbar-collapse" id="navbarSupportedContent">
          <ul class="navbar-nav me-auto mb-2 mb-lg-0">
            <li class="nav-item" v-for="route in navRoutes" :key="route.name">
              <router-link class="nav-link" :to="route.path">
                <component :is="route.meta.nav.icon" :size="18" class="icon"></component>
                {{ $t(route.meta.nav.label) }}
              </router-link>
            </li>
          </ul>
          <ul class="navbar-nav ms-auto mb-2 mb-lg-0">
            <li class="nav-item">
              <ThemeToggle />
            </li>
            <li class="nav-item dropdown">
              <button class="nav-link dropdown-toggle" type="button" id="navbarUserMenu" data-bs-toggle="dropdown"
                aria-expanded="false" aria-label="User menu" title="User menu">
                <CircleUserRound :size="18" class="icon"></CircleUserRound>
              </button>
              <ul class="dropdown-menu dropdown-menu-end" aria-labelledby="navbarUserMenu">
                <li>
                  <router-link class="dropdown-item d-flex align-items-center" to="/password">
                    <Shield :size="18" class="icon"></Shield>
                    {{ $t('navbar.password') }}
                  </router-link>
                </li>
                <li>
                  <hr class="dropdown-divider">
                </li>
                <li>
                  <button type="button" class="dropdown-item d-flex align-items-center" @click="logout">
                    <LogOut :size="18" class="icon"></LogOut>
                    {{ $t('navbar.logout') }}
                  </button>
                </li>
              </ul>
            </li>
          </ul>
        </div>
      </template>
      <template v-else>
        <ul class="navbar-nav ms-auto mb-2 mb-lg-0">
          <li class="nav-item">
            <ThemeToggle />
          </li>
        </ul>
      </template>
    </div>
  </nav>
</template>

<script>
import { CircleUserRound, LogOut, Menu, Shield } from '@lucide/vue'
import ThemeToggle from '@/components/ThemeToggle.vue'
import { apiFetch } from '@/utils/fetch_utils'
import { notifyKey } from '@/components/Notification.vue'

export default {
  components: {
    ThemeToggle,
    Shield,
    CircleUserRound,
    LogOut,
    Menu
  },
  computed: {
    isPublic() {
      return !!this.$route.meta.public
    },
    navRoutes() {
      return this.$router.options.routes.filter(route => route.meta?.nav)
    }
  },
  methods: {
    logout() {
      apiFetch('/api/logout', { method: 'POST' }).catch(() => { }).finally(() => {
        // No real "log out" for Fallback Basic Auth, so force the browser to forget cached credentials.
        const cacheBuster = Date.now().toString()
        const request = new XMLHttpRequest()
        const finish = () => {
          notifyKey.success('logout.logged_out_desc', 'logout.logged_out')
          this.$router.push('/login')
        }

        // Must target a route that still requires auth (unlike '/', the public SPA shell) so the
        // browser actually gets a 401 challenge and evicts its cached credentials.
        // TODO: remove this whole fallback once the Basic Auth browser prompt is no longer needed.
        request.open('GET', '/api/apps', true, 'sunshine-logout', cacheBuster)
        request.setRequestHeader('Cache-Control', 'no-store')
        request.onload = finish
        request.onerror = finish
        request.ontimeout = finish
        request.timeout = 5000
        request.send()
      })
    }
  }
}
</script>
