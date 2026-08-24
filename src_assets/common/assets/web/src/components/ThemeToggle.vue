<script setup>
import { currentTheme, loadAutoTheme, pickRandomTheme, selectTheme } from '@/utils/theme'
import { computed, onMounted, ref } from 'vue'
import {
  CloudMoon,
  CloudRain,
  Coffee,
  Contrast,
  Droplet,
  Flame,
  Flower,
  Flower2,
  Ghost,
  Layers,
  Milk,
  MonitorSmartphone,
  Moon,
  Mountain,
  Shuffle,
  Sparkles,
  Sprout,
  Sun,
  Sunrise,
  Sunset,
  TreePine,
  Trees,
  Waves,
} from '@lucide/vue'

const darkThemes = [
  { value: 'dark', icon: Moon, labelKey: 'navbar.theme_dark' },
  { value: 'dracula', icon: Ghost, labelKey: 'navbar.theme_dracula' },
  { value: 'mocha', icon: Coffee, labelKey: 'navbar.theme_mocha' },
  { value: 'ember', icon: Flame, labelKey: 'navbar.theme_ember' },
  { value: 'rose-pine', icon: TreePine, labelKey: 'navbar.theme_rose_pine' },
  { value: 'moonlight', icon: CloudMoon, labelKey: 'navbar.theme_moonlight' },
  { value: 'slate', icon: Layers, labelKey: 'navbar.theme_slate' },
  { value: 'midnight', icon: CloudRain, labelKey: 'navbar.theme_midnight' },
  { value: 'nord', icon: Mountain, labelKey: 'navbar.theme_nord' },
]

const lightThemes = [
  { value: 'light', icon: Sun, labelKey: 'navbar.theme_light' },
  { value: 'alucard', icon: Droplet, labelKey: 'navbar.theme_alucard' },
  { value: 'latte', icon: Milk, labelKey: 'navbar.theme_latte' },
  { value: 'ember-light', icon: Sunset, labelKey: 'navbar.theme_ember_light' },
  { value: 'rose-pine-dawn', icon: Sprout, labelKey: 'navbar.theme_rose_pine_dawn' },
  { value: 'sunshine', icon: Sunrise, labelKey: 'navbar.theme_sunshine' },
  { value: 'indigo', icon: Sparkles, labelKey: 'navbar.theme_indigo' },
  { value: 'ocean', icon: Waves, labelKey: 'navbar.theme_ocean' },
  { value: 'forest', icon: Trees, labelKey: 'navbar.theme_forest' },
  { value: 'rose', icon: Flower, labelKey: 'navbar.theme_rose' },
  { value: 'lavender', icon: Flower2, labelKey: 'navbar.theme_lavender' },
  { value: 'monochrome', icon: Contrast, labelKey: 'navbar.theme_monochrome' },
]

const allThemes = [{ value: 'auto', icon: MonitorSmartphone, labelKey: 'navbar.theme_auto' }, ...darkThemes, ...lightThemes]
const themeIcons = Object.fromEntries(allThemes.map(t => [t.value, t.icon]))
const themeLabelKeys = Object.fromEntries(allThemes.map(t => [t.value, t.labelKey]))

const activeThemeIcon = computed(() => themeIcons[currentTheme.value] || MonitorSmartphone)
const activeThemeLabelKey = computed(() => themeLabelKeys[currentTheme.value] || 'navbar.theme_auto')

const toggleEl = ref(null)

function onSelect(value) {
  selectTheme(value)
  toggleEl.value?.focus()
}

function onRandom() {
  const values = allThemes.map(t => t.value).filter(value => value !== 'auto')
  onSelect(pickRandomTheme(values))
}

onMounted(() => {
  loadAutoTheme()
})
</script>

<template>
  <div class="dropdown bd-mode-toggle">
    <a ref="toggleEl" class="nav-link dropdown-toggle d-flex align-items-center" id="bd-theme" type="button"
      aria-expanded="false" data-bs-toggle="dropdown"
      :aria-label="`${$t('navbar.toggle_theme')} (${$t(activeThemeLabelKey)})`">
      <span class="theme-icon-active">
        <component :is="activeThemeIcon" :size="18" class="icon"></component>
      </span>
      <span id="bd-theme-text">{{ $t('navbar.toggle_theme') }}</span>
    </a>
    <ul class="dropdown-menu dropdown-menu-end theme-menu" aria-labelledby="bd-theme-text">
      <li class="theme-menu-full">
        <button type="button" class="dropdown-item d-flex align-items-center"
          :class="{ active: currentTheme === 'auto' }" :aria-pressed="currentTheme === 'auto'"
          @click="onSelect('auto')">
          <MonitorSmartphone :size="18" class="theme-icon icon"></MonitorSmartphone>
          {{ $t('navbar.theme_auto') }}
        </button>
        <button type="button" id="bd-theme-random" class="dropdown-item d-flex align-items-center" @click="onRandom">
          <Shuffle :size="18" class="theme-icon icon"></Shuffle>
          {{ $t('navbar.theme_random') }}
        </button>
      </li>
      <!-- Dark Themes -->
      <li class="theme-menu-group">
        <h6 class="dropdown-header">{{ $t('navbar.theme_group_dark') }}</h6>
        <button v-for="theme in darkThemes" :key="theme.value" type="button"
          class="dropdown-item d-flex align-items-center" :class="{ active: currentTheme === theme.value }"
          :aria-pressed="currentTheme === theme.value" @click="onSelect(theme.value)">
          <component :is="theme.icon" :size="18" class="theme-icon icon"></component>
          {{ $t(theme.labelKey) }}
        </button>
      </li>
      <!-- Light Themes -->
      <li class="theme-menu-group">
        <h6 class="dropdown-header">{{ $t('navbar.theme_group_light') }}</h6>
        <button v-for="theme in lightThemes" :key="theme.value" type="button"
          class="dropdown-item d-flex align-items-center" :class="{ active: currentTheme === theme.value }"
          :aria-pressed="currentTheme === theme.value" @click="onSelect(theme.value)">
          <component :is="theme.icon" :size="18" class="theme-icon icon"></component>
          {{ $t(theme.labelKey) }}
        </button>
      </li>
    </ul>
  </div>
</template>

<style scoped></style>
