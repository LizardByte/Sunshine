<script setup>
import Checkbox from "@/components/Checkbox.vue";
import Select from '@/components/Select.vue'
import { useConfigTab } from '@/composables/useConfigTab'
import { useI18n } from 'vue-i18n'

const { t } = useI18n()

const props = defineProps({
  platform: String,
  config: {
    type: Object,
    default: () => structuredClone(OPTIONS)
  }
})

const { config, getOwnConfigOptions } = useConfigTab(props.config, OPTIONS)

defineExpose({ getOwnConfigOptions })

const PRESET_OPTIONS = [
  { value: '1', label: `P1 ${t('config.nvenc_preset_1')}` },
  { value: '2', label: 'P2' },
  { value: '3', label: 'P3' },
  { value: '4', label: 'P4' },
  { value: '5', label: 'P5' },
  { value: '6', label: 'P6' },
  { value: '7', label: `P7 ${t('config.nvenc_preset_7')}` },
]

const SPLIT_ENCODE_OPTIONS = [
  { value: 'disabled', label: t('_common.disabled') },
  { value: 'driver_decides', label: t('config.nvenc_split_encode_driver_decides_def') },
  { value: 'enabled', label: t('_common.enabled') },
]

const TWOPASS_OPTIONS = [
  { value: 'disabled', label: t('config.nvenc_twopass_disabled') },
  { value: 'quarter_res', label: t('config.nvenc_twopass_quarter_res') },
  { value: 'full_res', label: t('config.nvenc_twopass_full_res') },
]
</script>

<script>
export const OPTIONS = {
  "nvenc_preset": 1,
  "nvenc_twopass": "quarter_res",
  "nvenc_spatial_aq": "disabled",
  "nvenc_vbv_increase": 0,
  "nvenc_realtime_hags": "enabled",
  "nvenc_split_encode": "driver_decides",
  "nvenc_latency_over_power": "enabled",
  "nvenc_opengl_vulkan_on_dxgi": "enabled",
  "nvenc_h264_cavlc": "disabled",
}
</script>

<template>
  <div id="nvidia-nvenc-encoder" class="config-page">
    <!-- Performance preset -->
    <Select id="nvenc_preset" v-model="config.nvenc_preset" :label="$t('config.nvenc_preset')"
      :desc="$t('config.nvenc_preset_desc')" :options="PRESET_OPTIONS" />

    <!-- Split frame encoding -->
    <Select v-if="platform === 'windows'" id="nvenc_split_encode" v-model="config.nvenc_split_encode"
      :label="$t('config.nvenc_split_encode')" :desc="$t('config.nvenc_split_encode_desc')"
      :options="SPLIT_ENCODE_OPTIONS" />

    <!-- Two-pass mode -->
    <Select id="nvenc_twopass" v-model="config.nvenc_twopass" :label="$t('config.nvenc_twopass')"
      :desc="$t('config.nvenc_twopass_desc')" :options="TWOPASS_OPTIONS" />

    <!-- Spatial AQ -->
    <Checkbox class="mb-3" id="nvenc_spatial_aq" locale-prefix="config" v-model="config.nvenc_spatial_aq"
      default="false"></Checkbox>

    <!-- Single-frame VBV/HRD percentage increase -->
    <div class="mb-3">
      <label for="nvenc_vbv_increase" class="form-label">{{ $t('config.nvenc_vbv_increase') }}</label>
      <input type="number" min="0" max="400" class="form-control" id="nvenc_vbv_increase" placeholder="0"
        v-model="config.nvenc_vbv_increase" />
      <div class="form-text">
        <div>{{ $t('config.nvenc_vbv_increase_desc') }}</div>
        <div class="mt-1"><a href="https://en.wikipedia.org/wiki/Video_buffering_verifier">VBV/HRD</a></div>
      </div>
    </div>

    <!-- Miscellaneous options -->
    <div class="mb-3 accordion">
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button" type="button" data-bs-toggle="collapse"
            data-bs-target="#panelsStayOpen-collapseOne">
            {{ $t('config.misc') }}
          </button>
        </h2>
        <div id="panelsStayOpen-collapseOne" class="accordion-collapse collapse show"
          aria-labelledby="panelsStayOpen-headingOne">
          <div class="accordion-body">
            <!-- NVENC Realtime HAGS priority -->
            <Checkbox v-if="platform === 'windows'" class="mb-3" id="nvenc_realtime_hags" locale-prefix="config"
              v-model="config.nvenc_realtime_hags" default="true">
              <br>
              <br>
              <a href="https://devblogs.microsoft.com/directx/hardware-accelerated-gpu-scheduling/">HAGS</a>
            </Checkbox>

            <!-- Prefer lower encoding latency over power savings -->
            <Checkbox v-if="platform === 'windows'" class="mb-3" id="nvenc_latency_over_power" locale-prefix="config"
              v-model="config.nvenc_latency_over_power" default="true"></Checkbox>

            <!-- Present OpenGL/Vulkan on top of DXGI -->
            <Checkbox v-if="platform === 'windows'" class="mb-3" id="nvenc_opengl_vulkan_on_dxgi" locale-prefix="config"
              v-model="config.nvenc_opengl_vulkan_on_dxgi" default="true"></Checkbox>

            <!-- NVENC H264 CAVLC -->
            <Checkbox class="mb-3" id="nvenc_h264_cavlc" locale-prefix="config" v-model="config.nvenc_h264_cavlc"
              default="false"></Checkbox>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>
