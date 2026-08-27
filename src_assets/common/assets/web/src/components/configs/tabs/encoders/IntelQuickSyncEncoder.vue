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
  { value: 'veryfast', label: t('config.qsv_preset_veryfast') },
  { value: 'faster', label: t('config.qsv_preset_faster') },
  { value: 'fast', label: t('config.qsv_preset_fast') },
  { value: 'medium', label: t('config.qsv_preset_medium') },
  { value: 'slow', label: t('config.qsv_preset_slow') },
  { value: 'slower', label: t('config.qsv_preset_slower') },
  { value: 'slowest', label: t('config.qsv_preset_slowest') },
]

const CODER_OPTIONS = [
  { value: 'auto', label: t('config.ffmpeg_auto') },
  { value: 'cabac', label: t('config.coder_cabac') },
  { value: 'cavlc', label: t('config.coder_cavlc') },
]
</script>

<script>
export const OPTIONS = {
  "qsv_preset": "medium",
  "qsv_coder": "auto",
  "qsv_slow_hevc": "disabled",
}
</script>

<template>
  <div id="intel-quicksync-encoder" class="config-page">
    <!-- QuickSync Preset -->
    <Select id="qsv_preset" v-model="config.qsv_preset" :label="$t('config.qsv_preset')" :options="PRESET_OPTIONS" />

    <!-- QuickSync Coder (H264) -->
    <Select id="qsv_coder" v-model="config.qsv_coder" :label="$t('config.qsv_coder')" :options="CODER_OPTIONS" />

    <!-- Allow Slow HEVC Encoding -->
    <Checkbox class="mb-3" id="qsv_slow_hevc" locale-prefix="config" v-model="config.qsv_slow_hevc" default="false">
    </Checkbox>
  </div>
</template>
