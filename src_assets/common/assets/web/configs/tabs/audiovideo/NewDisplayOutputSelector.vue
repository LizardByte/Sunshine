<script setup>
import {computed, ref} from 'vue'
import { $tp } from '../../../platform-i18n'
import PlatformLayout from '../../../PlatformLayout.vue'
import LegacyDisplayOutputSelector from "./LegacyDisplayOutputSelector.vue";

const props = defineProps([
  'platform',
  'config',
  'displays'
])

const config = ref(props.config)
const outputNamePlaceholder = (props.platform === 'windows') ? '{de9bb7e2-186e-505b-9e93-f48793333810}' : '4531345'

const selectedDisplay = computed(() => getSelected(config.output_name))

function getSelected(id) {
  for (const display of props.displays) {
    if (display.id === id) {
      return display
    }
  }
  return null
}

function isPrimary(display) {
  const origin = display.current_settings.origin
  return origin.x === 0 && origin.y === 0;
}

function resolution(display) {
  const resolution = display.current_settings.mode.resolution
  return `${resolution.width}x${resolution.height}`;
}
</script>

<template>
  <LegacyDisplayOutputSelector
      v-if="!displays || platform === 'linux'"
      :platform="platform"
      :config="config"
  />

  <div class="mb-3" v-if="displays">
    <label for="output_name" class="form-label">{{ $tp('config.output_name') }}</label>
    <select id="output_name" class="form-select" v-model="config.output_name">
      <option value="">Default</option>
      <option v-for="display in displays" :value="display.name">
        {{ display.id }}: {{ display.name }} {{ resolution(display) }}<template v-if='isPrimary(display)'>, primary</template>
      </option>
    </select>
    <div class="form-text">
      <p style="white-space: pre-line">{{ $tp('config.output_name_desc') }}</p>
      <PlatformLayout :platform="platform">
        <template #windows>
          <b>&nbsp;&nbsp;&nbsp;&nbsp;DEVICE ID: {de9bb7e2-186e-505b-9e93-f48793333810}</b><br>
          <b>&nbsp;&nbsp;&nbsp;&nbsp;DISPLAY NAME: \\.\DISPLAY1</b><br>
          <b>&nbsp;&nbsp;&nbsp;&nbsp;FRIENDLY NAME: ROG PG279Q</b><br>
          <b>&nbsp;&nbsp;&nbsp;&nbsp;DEVICE STATE: PRIMARY</b><br>
          <b>&nbsp;&nbsp;&nbsp;&nbsp;HDR STATE: UNKNOWN</b>
        </template>
        <template #linux>
        </template>
        <template #macos>
          <template v-if="selectedDisplay">
            <b>&nbsp;&nbsp;&nbsp;&nbsp;DEVICE ID: {{ selectedDisplay.id }}</b><br>
            <b>&nbsp;&nbsp;&nbsp;&nbsp;FRIENDLY NAME: {{ selectedDisplay.friendly_name }}</b><br>
            <b>&nbsp;&nbsp;&nbsp;&nbsp;DEVICE STATE: {{ isPrimary(selectedDisplay) }}</b><br>
          </template>
        </template>
      </PlatformLayout>
    </div>
  </div>
</template>
