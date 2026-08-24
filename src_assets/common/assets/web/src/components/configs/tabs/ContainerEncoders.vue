<script setup>
import { ref } from 'vue'
import NvidiaNvencEncoder from '@/components/configs/tabs/encoders/NvidiaNvencEncoder.vue'
import IntelQuickSyncEncoder from '@/components/configs/tabs/encoders/IntelQuickSyncEncoder.vue'
import AmdAmfEncoder from '@/components/configs/tabs/encoders/AmdAmfEncoder.vue'
import VideotoolboxEncoder from '@/components/configs/tabs/encoders/VideotoolboxEncoder.vue'
import SoftwareEncoder from '@/components/configs/tabs/encoders/SoftwareEncoder.vue'
import VAAPIEncoder from '@/components/configs/tabs/encoders/VAAPIEncoder.vue'
import VulkanEncoder from '@/components/configs/tabs/encoders/VulkanEncoder.vue'

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

  <!-- VAAPI Encoder Tab -->
  <VAAPIEncoder
      v-if="currentTab === 'vaapi'"
      :platform="platform"
      :config="config"
  />

  <!-- Vulkan Encoder Tab -->
  <VulkanEncoder
      v-if="currentTab === 'vulkan'"
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
