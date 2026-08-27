<template>
  <div class="mb-3">
    <label :for="id" class="form-label">{{ label }}</label>
    <select :id="id" class="form-select" v-model="model">
      <slot name="prepend"></slot>
      <PlatformLayout>
        <template v-for="(list, slotName) in optionsBySlot" :key="slotName" #[slotName]>
          <option v-for="opt in list" :key="opt.value" :value="opt.value">{{ opt.label }}</option>
        </template>
      </PlatformLayout>
      <slot name="append"></slot>
    </select>
    <div class="form-text" v-if="desc">{{ desc }}</div>
  </div>
</template>

<script setup>
import { computed } from 'vue'
import PlatformLayout from '@/components/PlatformLayout.vue'

const props = defineProps({
  id: { type: String, required: true },
  label: String,
  desc: String,
  // Array (same for all platforms or default) or object keyed by platform (default/freebsd/linux/windows/macos).
  options: { type: [Array, Object], required: true },
})

const model = defineModel({ type: [String, Number] })

// Normalizes to { slotName: options[] } for PlatformLayout.
const optionsBySlot = computed(() => Array.isArray(props.options) ? { default: props.options } : props.options)
</script>
