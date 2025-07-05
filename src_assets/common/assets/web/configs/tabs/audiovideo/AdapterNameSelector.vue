<script setup>
import { ref } from 'vue'
import { $tp } from '../../../platform-i18n'
import PlatformLayout from '../../../PlatformLayout.vue'

const props = defineProps([
  'platform',
  'config'
])

const config = ref(props.config)
</script>

<template>
  <div class="mb-3" v-if="platform !== 'macos'">
    <label for="adapter_name" class="form-label">{{ $t('config.adapter_name') }}</label>
    <input type="text" class="form-control" id="adapter_name"
           :placeholder="$tp('config.adapter_name_placeholder', '/dev/dri/renderD128')"
           v-model="config.adapter_name" />
    <div class="form-text">
      <PlatformLayout :platform="platform">
        <template #windows>
          {{ $t('config.adapter_name_desc_windows') }}<br>
          <pre>tools\dxgi-info.exe</pre>
        </template>
        <template #freebsd>
          {{ $t('config.adapter_name_desc_linux_1') }}<br>
          <pre>ls /dev/dri/renderD*  # {{ $t('config.adapter_name_desc_linux_2') }}</pre>
          <pre>
              vainfo --display drm --device /dev/dri/renderD129 | \
                grep -E "((VAProfileH264High|VAProfileHEVCMain|VAProfileHEVCMain10).*VAEntrypointEncSlice)|Driver version"
            </pre>
          {{ $t('config.adapter_name_desc_linux_3') }}<br>
          <i>VAProfileH264High   : VAEntrypointEncSlice</i>
        </template>
        <template #linux>
          {{ $t('config.adapter_name_desc_linux_1') }}<br>
          <pre>ls /dev/dri/renderD*  # {{ $t('config.adapter_name_desc_linux_2') }}</pre>
          <pre>
              vainfo --display drm --device /dev/dri/renderD129 | \
                grep -E "((VAProfileH264High|VAProfileHEVCMain|VAProfileHEVCMain10).*VAEntrypointEncSlice)|Driver version"
            </pre>
          {{ $t('config.adapter_name_desc_linux_3') }}<br>
          <i>VAProfileH264High   : VAEntrypointEncSlice</i>
        </template>
      </PlatformLayout>
    </div>
  </div>
</template>
