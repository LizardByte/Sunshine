<script setup>
import { loadAutoTheme, setupThemeToggleListener } from './theme'
import { onMounted } from 'vue'

onMounted(() => {
  loadAutoTheme()
  setupThemeToggleListener()
})
</script>

<template>
  <div class="dropdown bd-mode-toggle">
    <a class="nav-link dropdown-toggle align-items-center"
            id="bd-theme"
            type="button"
            aria-expanded="false"
            data-bs-toggle="dropdown"
            aria-label="{{ $t('navbar.toggle_theme') }} ({{ $t('navbar.theme_auto') }})">
      <span class="bi my-1 theme-icon-active"><i class="fa-solid fa-circle-half-stroke"></i></span>
      <span id="bd-theme-text">{{ $t('navbar.toggle_theme') }}</span>
    </a>
    <ul class="dropdown-menu dropdown-menu-end" aria-labelledby="bd-theme-text">
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="light" aria-pressed="false">
          <i class="bi me-2 theme-icon fas fa-fw fa-solid fa-sun"></i>
          {{ $t('navbar.theme_light') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="dark" aria-pressed="false">
          <i class="bi me-2 theme-icon fas fa-fw fa-solid fa-moon"></i>
          {{ $t('navbar.theme_dark') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center active" data-bs-theme-value="auto" aria-pressed="true">
          <i class="bi me-2 theme-icon fas fa-fw fa-solid fa-circle-half-stroke"></i>
          {{ $t('navbar.theme_auto') }}
        </button>
      </li>
    </ul>
  </div>
</template>

<style scoped>
</style>
