<script setup>
import { ref } from 'vue'
import { $tp } from '../../../platform-i18n'
import PlatformLayout from '../../../PlatformLayout.vue'

const props = defineProps({
  platform: String,
  config: Object,
  display_mode_remapping: Array
})

const config = ref(props.config)
const display_mode_remapping = ref(props.display_mode_remapping)

// TODO: Sample for use in PR #2032
function getRemappingType()
{
  // Assuming here that at least one setting is set to "automatic"
  if (config.value.resolution_change !== 'automatic') {
    return "refresh_rate_only";
  }
  if (config.value.refresh_rate_change !== 'automatic') {
    return "resolution_only";
  }
  return "";
}

function addRemapping(type) {
  let template = {
    type: type,
    received_resolution: "",
    received_fps: "",
    final_resolution: "",
    final_refresh_rate: "",
  };

  display_mode_remapping.value.push(template);
}
</script>

<template>
  <div class="mb-3">
    <!-- TODO: Implement on PR #2032 -->
  </div>
</template>
