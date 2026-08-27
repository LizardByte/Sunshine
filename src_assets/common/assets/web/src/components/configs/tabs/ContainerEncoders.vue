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

const nv = ref(null)
const qsv = ref(null)
const amd = ref(null)
const vt = ref(null)
const vaapi = ref(null)
const vulkan = ref(null)
const sw = ref(null)

const tabRefsById = { nv, qsv, amd, vt, vaapi, vulkan, sw }

function getOptions() {
  const result = {};
  Object.keys(tabRefsById).forEach(id => {
    const tabRef = tabRefsById[id];
    if (tabRef.value) {
      Object.assign(result, tabRef.value.getOwnConfigOptions());
    }
  });
  return result;
}

// Same as getOptions(), but keeps each encoder's options grouped under its own tab id.
function getOptionsById() {
  const result = {};
  Object.keys(tabRefsById).forEach(id => {
    const tabRef = tabRefsById[id];
    if (tabRef.value) {
      result[id] = tabRef.value.getOwnConfigOptions();
    }
  });
  return result;
}

defineExpose({ getOptions, getOptionsById })
</script>

<template>

  <!-- NVIDIA NVENC Encoder Tab -->
  <NvidiaNvencEncoder ref="nv" v-show="currentTab === 'nv'" :platform="platform" :config="config" />

  <!-- Intel QuickSync Encoder Tab -->
  <IntelQuickSyncEncoder ref="qsv" v-show="currentTab === 'qsv'" :platform="platform" :config="config" />

  <!-- AMD AMF Encoder Tab -->
  <AmdAmfEncoder ref="amd" v-show="currentTab === 'amd'" :platform="platform" :config="config" />

  <!-- VideoToolbox Encoder Tab -->
  <VideotoolboxEncoder ref="vt" v-show="currentTab === 'vt'" :platform="platform" :config="config" />

  <!-- VAAPI Encoder Tab -->
  <VAAPIEncoder ref="vaapi" v-show="currentTab === 'vaapi'" :platform="platform" :config="config" />

  <!-- Vulkan Encoder Tab -->
  <VulkanEncoder ref="vulkan" v-show="currentTab === 'vulkan'" :platform="platform" :config="config" />

  <!-- Software Encoder Tab -->
  <SoftwareEncoder ref="sw" v-show="currentTab === 'sw'" :platform="platform" :config="config" />

</template>
