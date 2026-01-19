<template>
  <nav class="navbar navbar-dark navbar-expand-lg header">
    <div class="container-fluid">
      <a class="navbar-brand" href="./" title="Backlight">
        <img src="/images/backlight-logo.png" height="40" alt="Backlight">
      </a>
      <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarSupportedContent"
              aria-controls="navbarSupportedContent" aria-expanded="false" aria-label="Toggle navigation">
        <span class="navbar-toggler-icon"></span>
      </button>
      <div class="collapse navbar-collapse" id="navbarSupportedContent">
        <ul class="navbar-nav me-auto mb-2 mb-lg-0">
          <li class="nav-item">
            <a class="nav-link" href="./"><i class="fas fa-fw fa-home"></i> {{ $t('navbar.home') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./pin"><i class="fas fa-fw fa-unlock"></i> {{ $t('navbar.pin') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./apps"><i class="fas fa-fw fa-stream"></i> {{ $t('navbar.applications') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./config"><i class="fas fa-fw fa-cog"></i> {{ $t('navbar.configuration') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./password"><i class="fas fa-fw fa-user-shield"></i> {{ $t('navbar.password') }}</a>
          </li>
          <li class="nav-item">
            <a class="nav-link" href="./troubleshooting"><i class="fas fa-fw fa-info"></i> {{ $t('navbar.troubleshoot') }}</a>
          </li>
        </ul>
      </div>
    </div>
  </nav>
</template>

<script>
import { initDiscord } from '@lizardbyte/shared-web/src/js/discord.js'

export default {
  created() {
    console.log("Header mounted!")
  },
  mounted() {
    let el = document.querySelector("a[href='" + document.location.pathname + "']");
    if (el) el.classList.add("active")
    initDiscord();
  }
}
</script>

<style>
/* Backbone-branded navbar with glass effect */
.header {
  background: linear-gradient(180deg, rgba(28, 28, 30, 0.95) 0%, rgba(0, 0, 0, 0.9) 100%);
  backdrop-filter: saturate(180%) blur(20px);
  -webkit-backdrop-filter: saturate(180%) blur(20px);
  border-bottom: 1px solid #38383a;
  padding: 0.5rem 0;
}

.header .navbar-brand {
  display: flex;
  align-items: center;
}

.header .navbar-brand img {
  transition: opacity 0.2s ease;
}

.header .navbar-brand:hover img {
  opacity: 0.8;
}

.header .nav-link {
  color: rgba(255, 255, 255, 0.7) !important;
  font-weight: 500;
  padding: 0.5rem 0.875rem !important;
  border-radius: 6px;
  transition: color 0.2s ease, background-color 0.2s ease;
  margin: 0 0.125rem;
}

.header .nav-link:hover {
  color: #ff6b35 !important;
  background-color: rgba(255, 107, 53, 0.1);
}

.header .nav-link.active {
  color: #ff6b35 !important;
  background-color: rgba(255, 107, 53, 0.15);
  font-weight: 600;
}

.header .nav-link i {
  margin-right: 0.25rem;
  opacity: 0.8;
}

.header .nav-link:hover i,
.header .nav-link.active i {
  opacity: 1;
}

.header .navbar-toggler {
  border-color: #38383a !important;
  padding: 0.375rem 0.625rem;
}

.header .navbar-toggler:focus {
  box-shadow: 0 0 0 3px rgba(255, 107, 53, 0.2);
}

.header .navbar-toggler-icon {
  --bs-navbar-toggler-icon-bg: url("data:image/svg+xml,%3csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 30 30'%3e%3cpath stroke='rgba%28255, 255, 255, 0.75%29' stroke-linecap='round' stroke-miterlimit='10' stroke-width='2' d='M4 7h22M4 15h22M4 23h22'/%3e%3c/svg%3e") !important;
}

/* Mobile responsiveness */
@media (max-width: 991.98px) {
  .header .navbar-nav {
    padding: 0.5rem 0;
  }

  .header .nav-link {
    padding: 0.625rem 0.875rem !important;
  }
}
</style>
