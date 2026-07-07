<script setup>
import { ref } from 'vue'
import Checkbox from "../../../Checkbox.vue";

const props = defineProps([
  'platform',
  'config',
])

const config = ref(props.config)
</script>

<template>
  <div id="vaapi-encoder" class="config-page">
    <!-- Quality -->
    <div class="mb-3">
      <label for="vaapi_quality" class="form-label">{{ $t('config.vaapi_quality') }}</label>
      <select id="vaapi_quality" class="form-select" v-model="config.vaapi_quality">
        <option value="0">{{ $t('_common.auto') }}</option>
        <option value="1">{{ $t('config.vaapi_quality_low') }}</option>
        <option value="2">{{ $t('config.vaapi_quality_med') }}</option>
        <option value="3">{{ $t('config.vaapi_quality_high') }}</option>
      </select>
      <div class="form-text">{{ $t('config.vaapi_quality_desc') }}</div>
    </div>
    <!-- Allow VBR rate control -->
    <Checkbox class="mb-3"
              id="vaapi_allow_vbr"
              locale-prefix="config"
              v-model="config.vaapi_allow_vbr"
              default="false"
    ></Checkbox>
    <!-- BLBRC -->
    <Checkbox class="mb-3"
              id="vaapi_blbrc"
              locale-prefix="config"
              v-model="config.vaapi_blbrc"
              default="false"
    ></Checkbox>
    <!-- Strict RC Buffer -->
    <Checkbox class="mb-3"
              id="vaapi_strict_rc_buffer"
              locale-prefix="config"
              v-model="config.vaapi_strict_rc_buffer"
              default="false"
    ></Checkbox>
  </div>
</template>

<style scoped>

</style>
