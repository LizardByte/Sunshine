<script setup>
import { ref, computed } from 'vue'
import { Search, X } from '@lucide/vue'

const props = defineProps({
  options: { type: Array, required: true }
})

const emit = defineEmits(['select'])

const query = ref('')
const isOpen = ref(false)
const activeIndex = ref(-1)

const results = computed(() => {
  if (!query.value) return []
  const q = query.value.toLowerCase()
  return props.options.filter(option =>
    (option.key.toLowerCase().includes(q) || option.label.toLowerCase().includes(q) || option.tab.toLowerCase().includes(q)) &&
    // Only surface options that are actually rendered right now - platform or
    // config-value gates (e.g. platform === 'macos') keep some out of the DOM.
    document.getElementById(option.key)
  )
})

function onInput() {
  isOpen.value = true
  activeIndex.value = -1
}

function clear() {
  query.value = ''
  isOpen.value = false
  activeIndex.value = -1
}

function choose(option) {
  emit('select', option)
  query.value = option.label
  isOpen.value = false
  activeIndex.value = -1
}

function onKeydown(e) {
  if (!isOpen.value || results.value.length === 0) return

  if (e.key === 'ArrowDown') {
    e.preventDefault()
    activeIndex.value = (activeIndex.value + 1) % results.value.length
  } else if (e.key === 'ArrowUp') {
    e.preventDefault()
    activeIndex.value = (activeIndex.value - 1 + results.value.length) % results.value.length
  } else if (e.key === 'Enter' && activeIndex.value >= 0) {
    e.preventDefault()
    choose(results.value[activeIndex.value])
  } else if (e.key === 'Escape') {
    isOpen.value = false
  }
}
</script>

<template>
  <div class="config-search-wrapper">
    <div class="input-group">
      <input type="text" class="form-control config-search" v-model="query" :placeholder="$t('config.search_options')"
        :aria-label="$t('config.search_options')" autocomplete="off" @input="onInput" @focus="isOpen = true"
        @blur="isOpen = false" @keydown="onKeydown" />
      <button v-if="query" class="btn btn-outline-secondary" type="button" @mousedown.prevent="clear"
        :aria-label="$t('_common.close')">
        <X :size="16" class="icon"></X>
      </button>
      <span v-else class="input-group-text">
        <Search :size="16" class="icon"></Search>
      </span>
    </div>

    <ul class="dropdown-menu config-search-results" :class="{ show: isOpen && query }">
      <li v-if="results.length === 0">
        <span class="dropdown-item disabled">{{ $t('config.search_no_results', { query }) }}</span>
      </li>
      <li v-for="(option, index) in results" :key="option.key">
        <button type="button" class="dropdown-item" :class="{ active: index === activeIndex }"
          @mousedown.prevent="choose(option)">
          <div>{{ option.label }}</div>
          <div class="text-muted small">{{ option.tab }}</div>
        </button>
      </li>
    </ul>
  </div>
</template>
