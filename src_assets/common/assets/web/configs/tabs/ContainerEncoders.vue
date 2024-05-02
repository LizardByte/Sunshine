<script setup>
import { ref } from 'vue'
import NvidiaNvencEncoder from './encoders/NvidiaNvencEncoder.vue'
import IntelQuickSyncEncoder from './encoders/IntelQuickSyncEncoder.vue'
import AmdAmfEncoder from './encoders/AmdAmfEncoder.vue'
import VideotoolboxEncoder from './encoders/VideotoolboxEncoder.vue'
import SoftwareEncoder from './encoders/SoftwareEncoder.vue'

const props = defineProps([
  'platform',
  'config',
  'currentTab'
])

const config = ref(props.config)
</script>

<template>

  <!-- NVIDIA NVENC Encoder Tab -->
  <NvidiaNvencEncoder
      v-if="currentTab === 'nv'"
      :platform="platform"
      :config="config"
  />

  <!-- Intel QuickSync Encoder Tab -->
  <IntelQuickSyncEncoder
      v-if="currentTab === 'qsv'"
      :platform="platform"
      :config="config"
  />

  <!-- AMD AMF Encoder Tab -->
  <AmdAmfEncoder
      v-if="currentTab === 'amd'"
      :platform="platform"
      :config="config"
  />

  <!-- VideoToolbox Encoder Tab -->
  <VideotoolboxEncoder
      v-if="currentTab === 'vt'"
      :platform="platform"
      :config="config"
  />

  <!-- Software Encoder Tab -->
  <SoftwareEncoder
      v-if="currentTab === 'sw'"
      :platform="platform"
      :config="config"
  />

</template>

<style scoped>
</style>
