<script setup>
import { ref } from 'vue'
import { $tp } from '../../../platform-i18n'
import PlatformLayout from '../../../PlatformLayout.vue'

const props = defineProps([
  'platform',
  'config',
])
const config = ref(props.config)
</script>

<template>
  <!--min_fps_factor-->
  <div class="mb-3">
    <label for="qp" class="form-label">{{ $t('config.min_fps_factor') }}</label>
    <input type="number" min="1" max="3" class="form-control" id="min_fps_factor" placeholder="1" v-model="config.min_fps_factor" />
    <div class="form-text">{{ $t('config.min_fps_factor_desc') }}</div>
  </div>

  <!--max_bitrate-->
  <div class="mb-3">
    <label for="max_bitrate" class="form-label">{{ $t("config.max_bitrate") }}</label>
    <input type="number" class="form-control" id="max_bitrate" placeholder="0" v-model="config.max_bitrate" />
    <div class="form-text">{{ $t("config.max_bitrate_desc") }}</div>
  </div>
</template>

<style scoped>
.ms-item {
  background-color: var(--bs-dark-bg-subtle);
  font-size: 12px;
  font-weight: bold;
}
</style>
