<script setup>
import { ref } from 'vue'

const props = defineProps([
  'platform',
  'config'
])

const config = ref(props.config)
</script>

<template>
  <div id="software-encoder" class="config-page">
    <div class="mb-3">
      <label for="sw_preset" class="form-label">{{ $t('config.sw_preset') }}</label>
      <select id="sw_preset" class="form-select" v-model="config.sw_preset">
        <option value="ultrafast">{{ $t('config.sw_preset_ultrafast') }}</option>
        <option value="superfast">{{ $t('config.sw_preset_superfast') }}</option>
        <option value="veryfast">{{ $t('config.sw_preset_veryfast') }}</option>
        <option value="faster">{{ $t('config.sw_preset_faster') }}</option>
        <option value="fast">{{ $t('config.sw_preset_fast') }}</option>
        <option value="medium">{{ $t('config.sw_preset_medium') }}</option>
        <option value="slow">{{ $t('config.sw_preset_slow') }}</option>
        <option value="slower">{{ $t('config.sw_preset_slower') }}</option>
        <option value="veryslow">{{ $t('config.sw_preset_veryslow') }}</option>
      </select>
      <div class="form-text">{{ $t('config.sw_preset_desc') }}</div>
    </div>

    <div class="mb-3">
      <label for="sw_tune" class="form-label">{{ $t('config.sw_tune') }}</label>
      <select id="sw_tune" class="form-select" v-model="config.sw_tune">
        <option value="film">{{ $t('config.sw_tune_film') }}</option>
        <option value="animation">{{ $t('config.sw_tune_animation') }}</option>
        <option value="grain">{{ $t('config.sw_tune_grain') }}</option>
        <option value="stillimage">{{ $t('config.sw_tune_stillimage') }}</option>
        <option value="fastdecode">{{ $t('config.sw_tune_fastdecode') }}</option>
        <option value="zerolatency">{{ $t('config.sw_tune_zerolatency') }}</option>
      </select>
      <div class="form-text">{{ $t('config.sw_tune_desc') }}</div>
    </div>
  </div>
</template>

<style scoped>

</style>
