<script setup>
import { ref } from 'vue'
import { $tp } from '../../../platform-i18n'
import PlatformLayout from '../../../PlatformLayout.vue'

const props = defineProps([
  'platform',
  'config'
])

const config = ref(props.config)
const outputNamePlaceholder = (props.platform === 'windows') ? '{de9bb7e2-186e-505b-9e93-f48793333810}' : '0'
</script>

<template>
  <div class="mb-3">
    <label for="output_name" class="form-label">{{ $t('config.output_name') }}</label>
    <input type="text" class="form-control" id="output_name" :placeholder="outputNamePlaceholder"
           v-model="config.output_name"/>
    <div class="form-text">
      {{ $tp('config.output_name_desc') }}<br>
      <PlatformLayout :platform="platform">
        <template #windows>
          <pre style="white-space: pre-line;">
            <b>&nbsp;&nbsp;{</b>
            <b>&nbsp;&nbsp;&nbsp;&nbsp;"device_id": "{de9bb7e2-186e-505b-9e93-f48793333810}"</b>
            <b>&nbsp;&nbsp;&nbsp;&nbsp;"display_name": "\\\\.\\DISPLAY1"</b>
            <b>&nbsp;&nbsp;&nbsp;&nbsp;"friendly_name": "ROG PG279Q"</b>
            <b>&nbsp;&nbsp;&nbsp;&nbsp;...</b>
            <b>&nbsp;&nbsp;}</b>
          </pre>
        </template>
        <template #freebsd>
          <pre style="white-space: pre-line;">
            Info: Detecting displays
            Info: Detected display: DVI-D-0 (id: 0) connected: false
            Info: Detected display: HDMI-0 (id: 1) connected: true
            Info: Detected display: DP-0 (id: 2) connected: true
            Info: Detected display: DP-1 (id: 3) connected: false
            Info: Detected display: DVI-D-1 (id: 4) connected: false
          </pre>
        </template>
        <template #linux>
          <pre style="white-space: pre-line;">
            Info: Detecting displays
            Info: Detected display: DVI-D-0 (id: 0) connected: false
            Info: Detected display: HDMI-0 (id: 1) connected: true
            Info: Detected display: DP-0 (id: 2) connected: true
            Info: Detected display: DP-1 (id: 3) connected: false
            Info: Detected display: DVI-D-1 (id: 4) connected: false
          </pre>
        </template>
        <template #macos>
          <pre style="white-space: pre-line;">
            Info: Detecting displays
            Info: Detected display: Monitor-0 (id: 3) connected: true
            Info: Detected display: Monitor-1 (id: 2) connected: true
          </pre>
        </template>
      </PlatformLayout>
    </div>
  </div>
</template>
