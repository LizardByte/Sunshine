<script setup>
import { loadAutoTheme, setupThemeToggleListener } from './theme'
import { onMounted } from 'vue'
import {
  CloudMoon,
  CloudRain,
  Contrast,
  Flame,
  Flower,
  Flower2,
  Layers,
  MonitorSmartphone,
  Moon,
  Mountain,
  Sparkles,
  Sun,
  Sunrise,
  Trees,
  Waves,
} from 'lucide-vue-next'

onMounted(() => {
  loadAutoTheme()
  setupThemeToggleListener()
})
</script>

<template>
  <div class="dropdown bd-mode-toggle">
    <a class="nav-link dropdown-toggle d-flex align-items-center"
            id="bd-theme"
            type="button"
            aria-expanded="false"
            data-bs-toggle="dropdown"
            aria-label="{{ $t('navbar.toggle_theme') }} ({{ $t('navbar.theme_auto') }})">
      <span class="theme-icon-active">
        <MonitorSmartphone :size="18" class="icon"></MonitorSmartphone>
      </span>
      <span id="bd-theme-text">{{ $t('navbar.toggle_theme') }}</span>
    </a>
    <ul class="dropdown-menu dropdown-menu-end" aria-labelledby="bd-theme-text">
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center active" data-bs-theme-value="auto" aria-pressed="true">
          <MonitorSmartphone :size="18" class="theme-icon icon"></MonitorSmartphone>
          {{ $t('navbar.theme_auto') }}
        </button>
      </li>
      <li><hr class="dropdown-divider"></li>
      <!-- Dark Themes -->
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="dark" aria-pressed="false">
          <Moon :size="18" class="theme-icon icon"></Moon>
          {{ $t('navbar.theme_dark') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="slate" aria-pressed="false">
          <Layers :size="18" class="theme-icon icon"></Layers>
          {{ $t('navbar.theme_slate') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="moonlight" aria-pressed="false">
          <CloudMoon :size="18" class="theme-icon icon"></CloudMoon>
          {{ $t('navbar.theme_moonlight') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="midnight" aria-pressed="false">
          <CloudRain :size="18" class="theme-icon icon"></CloudRain>
          {{ $t('navbar.theme_midnight') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="ember" aria-pressed="false">
          <Flame :size="18" class="theme-icon icon"></Flame>
          {{ $t('navbar.theme_ember') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="nord" aria-pressed="false">
          <Mountain :size="18" class="theme-icon icon"></Mountain>
          {{ $t('navbar.theme_nord') }}
        </button>
      </li>
      <li><hr class="dropdown-divider"></li>
      <!-- Light Themes -->
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="light" aria-pressed="false">
          <Sun :size="18" class="theme-icon icon"></Sun>
          {{ $t('navbar.theme_light') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="indigo" aria-pressed="false">
          <Sparkles :size="18" class="theme-icon icon"></Sparkles>
          {{ $t('navbar.theme_indigo') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="sunshine" aria-pressed="false">
          <Sunrise :size="18" class="theme-icon icon"></Sunrise>
          {{ $t('navbar.theme_sunshine') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="ocean" aria-pressed="false">
          <Waves :size="18" class="theme-icon icon"></Waves>
          {{ $t('navbar.theme_ocean') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="forest" aria-pressed="false">
          <Trees :size="18" class="theme-icon icon"></Trees>
          {{ $t('navbar.theme_forest') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="rose" aria-pressed="false">
          <Flower :size="18" class="theme-icon icon"></Flower>
          {{ $t('navbar.theme_rose') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="lavender" aria-pressed="false">
          <Flower2 :size="18" class="theme-icon icon"></Flower2>
          {{ $t('navbar.theme_lavender') }}
        </button>
      </li>
      <li>
        <button type="button" class="dropdown-item d-flex align-items-center" data-bs-theme-value="monochrome" aria-pressed="false">
          <Contrast :size="18" class="theme-icon icon"></Contrast>
          {{ $t('navbar.theme_monochrome') }}
        </button>
      </li>
    </ul>
  </div>
</template>

<style scoped>
</style>
