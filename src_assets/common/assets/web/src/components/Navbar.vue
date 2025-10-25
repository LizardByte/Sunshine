<template>
  <nav class="navbar navbar-light navbar-expand-lg navbar-background header">
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
            <router-link activeClass="active" class="nav-link" to="/"><i class="fas fa-fw fa-home"></i> {{ $t('navbar.home') }}</router-link>
          </li>
          <li class="nav-item">
            <router-link activeClass="active" class="nav-link" to="/pin"><i class="fas fa-fw fa-unlock"></i> {{ $t('navbar.pin') }}</router-link>
          </li>
          <li class="nav-item">
            <router-link activeClass="active" class="nav-link" to="/apps"><i class="fas fa-fw fa-stream"></i> {{ $t('navbar.applications') }}</router-link>
          </li>
          <li class="nav-item">
            <router-link activeClass="active" class="nav-link" to="/config"><i class="fas fa-fw fa-cog"></i> {{ $t('navbar.configuration') }}</router-link>
          </li>
          <li class="nav-item">
            <router-link activeClass="active" class="nav-link" to="/password"><i class="fas fa-fw fa-user-shield"></i> {{ $t('navbar.password') }}</router-link>
          </li>
          <li class="nav-item">
            <router-link activeClass="active" class="nav-link" to="/troubleshooting"><i class="fas fa-fw fa-info"></i> {{ $t('navbar.troubleshoot') }}</router-link>
          </li>
          <li class="nav-item">
            <ThemeToggle/>
          </li>
        </ul>
      </div>
    </div>
  </nav>
</template>

<script>
import ThemeToggle from './ThemeToggle.vue'
import { initDiscord } from '@lizardbyte/shared-web/src/js/discord.js'

export default {
  components: { ThemeToggle },
  created() {
    console.log("Header mounted!")
  },
  mounted() {
    initDiscord();
  }
}
</script>
