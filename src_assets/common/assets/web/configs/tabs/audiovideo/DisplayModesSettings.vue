<script setup>
import { ref } from 'vue'
const props = defineProps({
  platform: String,
  config: Object,
})
const config = ref(props.config)
</script>

<template>
  <!--max_bitrate-->
  <div class="mb-3">
    <label for="max_bitrate" class="form-label">{{ $t("config.max_bitrate") }}</label>
    <input type="number" class="form-control" id="max_bitrate" placeholder="0" v-model="config.max_bitrate" />
    <div class="form-text">{{ $t("config.max_bitrate_desc") }}</div>
  </div>

  <!--minimum_fps_target-->
  <div class="mb-3">
    <label for="minimum_fps_target" class="form-label">{{ $t("config.minimum_fps_target") }}</label>
    <input type="number" min="0" max="1000" class="form-control" id="minimum_fps_target" placeholder="0" v-model="config.minimum_fps_target" />
    <div class="form-text">{{ $t("config.minimum_fps_target_desc") }}</div>
  </div>

  <!--source_aspect_ratio-->
  <div class="mb-3">
    <label for="source_aspect_ratio" class="form-label">{{ $t("config.source_aspect_ratio") }}</label>
    <input type="text" class="form-control" id="source_aspect_ratio" list="source_aspect_ratio_choices" placeholder="client" v-model="config.source_aspect_ratio" />
    <datalist id="source_aspect_ratio_choices">
      <option value="client" />
      <option value="display" />
      <option value="stream" />
      <option value="4:3" />
      <option value="16:10" />
      <option value="16:9" />
      <option value="21:9" />
      <option value="32:9" />
    </datalist>
    <div class="form-text">{{ $t("config.source_aspect_ratio_desc") }}</div>
  </div>
</template>

<style scoped>
.ms-item {
  background-color: var(--bs-dark-bg-subtle);
  font-size: 12px;
  font-weight: bold;
}
</style>
