<script setup>
import Select from '@/components/Select.vue'
import { useConfigTab } from '@/composables/useConfigTab'
import { useI18n } from 'vue-i18n'

const { t } = useI18n()

const props = defineProps({
  platform: String,
  config: {
    type: Object,
    default: () => structuredClone(OPTIONS)
  },
  global_prep_cmd: Array
})

const { config, getOwnConfigOptions } = useConfigTab(props.config, OPTIONS)

defineExpose({ getOwnConfigOptions })

const HEVC_MODE_OPTIONS = [
  { value: '0', label: t('config.hevc_mode_0') },
  { value: '1', label: t('config.hevc_mode_1') },
  { value: '2', label: t('config.hevc_mode_2') },
  { value: '3', label: t('config.hevc_mode_3') },
]

const AV1_MODE_OPTIONS = [
  { value: '0', label: t('config.av1_mode_0') },
  { value: '1', label: t('config.av1_mode_1') },
  { value: '2', label: t('config.av1_mode_2') },
  { value: '3', label: t('config.av1_mode_3') },
]

const CAPTURE_OPTIONS = {
  freebsd: [
    { value: 'wlr', label: 'wlroots' },
    { value: 'x11', label: 'X11' },
    { value: 'portal', label: 'XDG Portal' },
  ],
  linux: [
    { value: 'nvfbc', label: 'NvFBC' },
    { value: 'wlr', label: 'wlroots' },
    { value: 'kms', label: 'KMS' },
    { value: 'x11', label: 'X11' },
    { value: 'kwin', label: 'KWin Screencast' },
    { value: 'portal', label: 'XDG Portal' },
  ],
  windows: [
    { value: 'ddx', label: 'Desktop Duplication API' },
    { value: 'wgc', label: `Windows.Graphics.Capture ${t('_common.beta')}` },
  ],
}

const ENCODER_OPTIONS = {
  windows: [
    { value: 'nvenc', label: 'NVIDIA NVENC' },
    { value: 'quicksync', label: 'Intel QuickSync' },
    { value: 'amdvce', label: 'AMD AMF/VCE' },
  ],
  freebsd: [
    { value: 'vulkan', label: 'Vulkan' },
    { value: 'vaapi', label: 'VA-API' },
  ],
  linux: [
    { value: 'nvenc', label: 'NVIDIA NVENC' },
    { value: 'vaapi', label: 'VA-API' },
    { value: 'vulkan', label: 'Vulkan' },
  ],
  macos: [
    { value: 'videotoolbox', label: 'VideoToolbox' },
  ],
}
</script>

<script>
export const OPTIONS = {
  "fec_percentage": 20,
  "qp": 28,
  "min_threads": 2,
  "hevc_mode": 0,
  "av1_mode": 0,
  "capture": "",
  "encoder": "",
}
</script>

<template>
  <div class="config-page">
    <!-- FEC Percentage -->
    <div class="mb-3">
      <label for="fec_percentage" class="form-label">{{ $t('config.fec_percentage') }}</label>
      <input type="text" class="form-control" id="fec_percentage" placeholder="20" v-model="config.fec_percentage" />
      <div class="form-text">{{ $t('config.fec_percentage_desc') }}</div>
    </div>

    <!-- Quantization Parameter -->
    <div class="mb-3">
      <label for="qp" class="form-label">{{ $t('config.qp') }}</label>
      <input type="number" class="form-control" id="qp" placeholder="28" v-model="config.qp" />
      <div class="form-text">{{ $t('config.qp_desc') }}</div>
    </div>

    <!-- Min Threads -->
    <div class="mb-3">
      <label for="min_threads" class="form-label">{{ $t('config.min_threads') }}</label>
      <input type="number" class="form-control" id="min_threads" placeholder="2" min="1" v-model="config.min_threads" />
      <div class="form-text">{{ $t('config.min_threads_desc') }}</div>
    </div>

    <!-- HEVC Support -->
    <Select id="hevc_mode" v-model="config.hevc_mode" :label="$t('config.hevc_mode')"
      :desc="$t('config.hevc_mode_desc')" :options="HEVC_MODE_OPTIONS" />

    <!-- AV1 Support -->
    <Select id="av1_mode" v-model="config.av1_mode" :label="$t('config.av1_mode')" :desc="$t('config.av1_mode_desc')"
      :options="AV1_MODE_OPTIONS" />

    <!-- Capture -->
    <Select v-if="platform !== 'macos'" id="capture" v-model="config.capture" :label="$t('config.capture')"
      :desc="$t('config.capture_desc')" :options="CAPTURE_OPTIONS">
      <template #prepend>
        <option value="">{{ $t('_common.autodetect') }}</option>
      </template>
    </Select>

    <!-- Encoder -->
    <Select id="encoder" v-model="config.encoder" :label="$t('config.encoder')" :desc="$t('config.encoder_desc')"
      :options="ENCODER_OPTIONS">
      <template #prepend>
        <option value="">{{ $t('_common.autodetect') }}</option>
      </template>
      <template #append>
        <option value="software">{{ $t('config.encoder_software') }}</option>
      </template>
    </Select>

  </div>
</template>
