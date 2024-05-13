<script setup>
import { ref } from 'vue'

const props = defineProps([
  'platform',
  'config'
])

const config = ref(props.config)
</script>

<template>
  <div id="files" class="config-page">
    <!-- Apps File -->
    <div class="mb-3">
      <label for="file_apps" class="form-label">{{ $t('config.file_apps') }}</label>
      <input type="text" class="form-control" id="file_apps" placeholder="apps.json" v-model="config.file_apps" />
      <div class="form-text">{{ $t('config.file_apps_desc') }}</div>
    </div>

    <!-- Credentials File -->
    <div class="mb-3">
      <label for="credentials_file" class="form-label">{{ $t('config.credentials_file') }}</label>
      <input type="text" class="form-control" id="credentials_file" placeholder="sunshine_state.json" v-model="config.credentials_file" />
      <div class="form-text">{{ $t('config.credentials_file_desc') }}</div>
    </div>

    <!-- Log Path -->
    <div class="mb-3">
      <label for="log_path" class="form-label">{{ $t('config.log_path') }}</label>
      <input type="text" class="form-control" id="log_path" placeholder="sunshine.log" v-model="config.log_path" />
      <div class="form-text">{{ $t('config.log_path_desc') }}</div>
    </div>

    <!-- Private Key -->
    <div class="mb-3">
      <label for="pkey" class="form-label">{{ $t('config.pkey') }}</label>
      <input type="text" class="form-control" id="pkey" placeholder="/dir/pkey.pem" v-model="config.pkey" />
      <div class="form-text">{{ $t('config.pkey_desc') }}</div>
    </div>

    <!-- Certificate -->
    <div class="mb-3">
      <label for="cert" class="form-label">{{ $t('config.cert') }}</label>
      <input type="text" class="form-control" id="cert" placeholder="/dir/cert.pem" v-model="config.cert" />
      <div class="form-text">{{ $t('config.cert_desc') }}</div>
    </div>

    <!-- State File -->
    <div class="mb-3">
      <label for="file_state" class="form-label">{{ $t('config.file_state') }}</label>
      <input type="text" class="form-control" id="file_state" placeholder="sunshine_state.json"
             v-model="config.file_state" />
      <div class="form-text">{{ $t('config.file_state_desc') }}</div>
    </div>

  </div>
</template>

<style scoped>

</style>
