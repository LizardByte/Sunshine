<script setup>
import { ref } from 'vue'
import { $tp } from '../../../platform-i18n'
import PlatformLayout from '../../../PlatformLayout.vue'

const props = defineProps([
  'platform',
  'config',
  'displays'
])

const config = ref(props.config)
const outputNamePlaceholder = (props.platform === 'windows') ? '{de9bb7e2-186e-505b-9e93-f48793333810}' : '4531345'
</script>

<template>
  <div class="mb-3">
    <label for="output_name" class="form-label">{{ $tp('config.output_name') }}</label>
    <input type="text" class="form-control" id="output_name" :placeholder="outputNamePlaceholder"
           v-model="config.output_name"/>
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
        </template>
      </PlatformLayout>
    </div>
  </div>
</template>
