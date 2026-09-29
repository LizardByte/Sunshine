<script setup>
import {
  getVirtualKeyCodeDescription,
  hasVirtualKeyCodeOption,
  isValidVirtualKeyCode,
  virtualKeyCodes,
} from './virtual_key_codes.js'

defineProps({
  id: {
    type: String,
    required: true,
  },
})

const model = defineModel({
  type: String,
  required: true,
})
</script>

<template>
  <select :id="id" class="form-select" :class="{ 'is-invalid': !isValidVirtualKeyCode(model) }"
          v-model="model" required>
    <option value="" disabled>{{ $t('config.keybindings_select') }}</option>
    <option v-if="model && !hasVirtualKeyCodeOption(model)" :value="model">
      {{ model }} ({{ getVirtualKeyCodeDescription(model) ?? $t('config.keybindings_custom') }})
    </option>
    <option v-for="keyCode in virtualKeyCodes" :key="keyCode.code" :value="keyCode.code">
      {{ keyCode.code }} ({{ keyCode.description }})
    </option>
  </select>
  <div class="invalid-feedback">{{ $t('config.keybindings_invalid') }}</div>
</template>
