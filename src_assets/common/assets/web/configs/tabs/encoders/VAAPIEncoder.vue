<script setup>
import { ref } from 'vue'

const props = defineProps([
  'platform',
  'config',
])

const config = ref(props.config)
</script>

<template>
  <div id="vaapi-encoder" class="config-page">
    <!-- Strict RC Buffer -->
    <div class="mb-3">
      <label for="vaapi_strict_rc_buffer" class="form-label">{{ $t('config.vaapi_strict_rc_buffer') }}</label>
      <select id="vaapi_strict_rc_buffer" class="form-select" v-model="config.vaapi_strict_rc_buffer">
        <option value="enabled">{{ $t('_common.enabled') }}</option>
        <option value="disabled">{{ $t('_common.disabled_def') }}</option>
      </select>
      <div class="form-text">{{ $t('config.vaapi_strict_rc_buffer_desc') }}</div>
    </div>
  </div>
</template>

<style scoped>

</style>
