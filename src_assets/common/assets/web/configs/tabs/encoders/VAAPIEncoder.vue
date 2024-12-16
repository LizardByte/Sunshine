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
    <div class="form-check">
      <label for="vaapi_strict_rc_buffer" class="form-label">{{ $t('config.vaapi_strict_rc_buffer') }}<div class="mt-0 form-text">{{ $t('_common.disabled_def_cbox') }}</div></label>
      <input type="checkbox" class="form-check-input" id="vaapi_strict_rc_buffer" v-model="config.vaapi_strict_rc_buffer" true-value="enabled" false-value="disabled" />
      <div class="form-text">{{ $t('config.vaapi_strict_rc_buffer_desc') }}</div>
    </div>
  </div>
</template>

<style scoped>

</style>
