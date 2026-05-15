<template>
  <div>
    <nav class="navbar navbar-expand-lg navbar-sunshine">
      <div class="container-fluid">
        <a class="navbar-brand" href="./" title="Sunshine">
          <img src="/images/logo-sunshine-45.png" height="45" alt="Sunshine">
        </a>
        <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarSupportedContent"
                aria-controls="navbarSupportedContent" aria-expanded="false" aria-label="Toggle navigation">
          <span class="navbar-toggler-icon"></span>
        </button>
        <div class="collapse navbar-collapse" id="navbarSupportedContent">
          <ul class="navbar-nav me-auto mb-2 mb-lg-0">
            <li class="nav-item">
              <a class="nav-link" href="./">
                <Home :size="18" class="icon"></Home>
                {{ $t('navbar.home') }}
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="./pin">
                <Lock :size="18" class="icon"></Lock>
                {{ $t('navbar.pin') }}
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="./apps">
                <Layers :size="18" class="icon"></Layers>
                {{ $t('navbar.applications') }}
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="./featured">
                <Star :size="18" class="icon"></Star>
                {{ $t('navbar.featured') }}
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="./config">
                <Settings :size="18" class="icon"></Settings>
                {{ $t('navbar.configuration') }}
              </a>
            </li>
            <li class="nav-item">
              <a class="nav-link" href="./troubleshooting">
                <Info :size="18" class="icon"></Info>
                {{ $t('navbar.troubleshoot') }}
              </a>
            </li>
          </ul>
          <ul class="navbar-nav ms-auto mb-2 mb-lg-0">
            <li class="nav-item">
              <ThemeToggle/>
            </li>
            <li class="nav-item dropdown">
              <button class="nav-link dropdown-toggle" type="button" id="navbarUserMenu"
                      data-bs-toggle="dropdown" aria-expanded="false" aria-label="User menu" title="User menu">
                <CircleUserRound :size="18" class="icon"></CircleUserRound>
              </button>
              <ul class="dropdown-menu dropdown-menu-end" aria-labelledby="navbarUserMenu">
                <li>
                  <a class="dropdown-item d-flex align-items-center" href="./password">
                    <Shield :size="18" class="icon"></Shield>
                    {{ $t('navbar.password') }}
                  </a>
                </li>
                <li><hr class="dropdown-divider"></li>
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
      </div>
    </nav>
    <Notification></Notification>
  </div>
</template>

<script>
import { CircleUserRound, Home, Info, Layers, Lock, LogOut, Settings, Shield, Star } from 'lucide-vue-next'
import ThemeToggle from './ThemeToggle.vue'
import Notification from './Notification.vue'

export default {
  components: {
    ThemeToggle,
    Notification,
    Home,
    Lock,
    Layers,
    Star,
    Settings,
    Shield,
    Info,
    CircleUserRound,
    LogOut
  },
  created() {
    console.log("Header mounted!")
  },
  mounted() {
    const currentPath = globalThis.location.pathname.replace(/\/$/, '') || '/'
    const links = document.querySelectorAll('.navbar-sunshine a[href]')

    for (const link of links) {
      const href = link.getAttribute('href')
      if (!href || href === '#') {
        continue
      }

      const linkPath = new URL(href, globalThis.location.href).pathname.replace(/\/$/, '') || '/'
      if (linkPath !== currentPath) {
        continue
      }

      link.classList.add('active')
    }
  },
  methods: {
    logout() {
      const cacheBuster = Date.now().toString()
      const logoutPageUrl = new URL('/logout', globalThis.location.href)
      const request = new XMLHttpRequest()
      const finish = () => {
        globalThis.location.replace(logoutPageUrl.toString())
      }

      request.open('GET', '/', true, 'sunshine-logout', cacheBuster)
      request.setRequestHeader('Cache-Control', 'no-store')
      request.onload = finish
      request.onerror = finish
      request.ontimeout = finish
      request.timeout = 5000
      request.send()
    }
  }
}
</script>

<style>
/* Navbar toggler icon for dark text on light background */
.navbar-sunshine .navbar-toggler-icon {
  --bs-navbar-toggler-icon-bg: url("data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 30 30'%3e%3cpath stroke='rgba%28255, 255, 255, 0.9%29' stroke-linecap='round' stroke-miterlimit='10' stroke-width='2' d='M4 7h22M4 15h22M4 23h22'/%3e%3c/svg%3e") !important;
}
</style>
