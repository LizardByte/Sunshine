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
  <div id="videotoolbox-encoder" class="config-page">
    <!-- Presets -->
    <div class="mb-3">
      <label for="vt_coder" class="form-label">{{ $t('config.vt_coder') }}</label>
      <select id="vt_coder" class="form-select" v-model="config.vt_coder">
        <option value="auto">{{ $t('config.ffmpeg_auto') }}</option>
        <option value="cabac">{{ $t('config.coder_cabac') }}</option>
        <option value="cavlc">{{ $t('config.coder_cavlc') }}</option>
      </select>
    </div>
    <div class="mb-3">
      <label for="vt_software" class="form-label">{{ $t('config.vt_software') }}</label>
      <select id="vt_software" class="form-select" v-model="config.vt_software">
        <option value="auto">{{ $t('_common.auto') }}</option>
        <option value="disabled">{{ $t('_common.disabled') }}</option>
        <option value="allowed">{{ $t('config.vt_software_allowed') }}</option>
        <option value="forced">{{ $t('config.vt_software_forced') }}</option>
      </select>
    </div>
    <Checkbox class="mb-3"
              id="vt_realtime"
              desc=""
              locale-prefix="config"
              v-model="config.vt_realtime"
              default="true"
    ></Checkbox>
    <div class="mb-3">
      <label for="prores_profile" class="form-label">{{ $t('config.prores_profile') }}</label>
      <select id="prores_profile" class="form-select" v-model="config.prores_profile">
        <option value="proxy">{{ $t('config.prores_profile_proxy') }}</option>
        <option value="lt">{{ $t('config.prores_profile_lt') }}</option>
        <option value="standard">{{ $t('config.prores_profile_standard') }}</option>
        <option value="hq">{{ $t('config.prores_profile_hq') }}</option>
        <option value="4444">{{ $t('config.prores_profile_4444') }}</option>
        <option value="xq">{{ $t('config.prores_profile_xq') }}</option>
      </select>
      <div class="form-text">{{ $t('config.prores_profile_desc') }}</div>
    </div>
  </div>
</template>

<style scoped>

</style>
