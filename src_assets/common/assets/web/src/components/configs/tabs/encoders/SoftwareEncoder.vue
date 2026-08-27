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
  }
})

const { config, getOwnConfigOptions } = useConfigTab(props.config, OPTIONS)

defineExpose({ getOwnConfigOptions })

const PRESET_OPTIONS = [
  { value: 'ultrafast', label: t('config.sw_preset_ultrafast') },
  { value: 'superfast', label: t('config.sw_preset_superfast') },
  { value: 'veryfast', label: t('config.sw_preset_veryfast') },
  { value: 'faster', label: t('config.sw_preset_faster') },
  { value: 'fast', label: t('config.sw_preset_fast') },
  { value: 'medium', label: t('config.sw_preset_medium') },
  { value: 'slow', label: t('config.sw_preset_slow') },
  { value: 'slower', label: t('config.sw_preset_slower') },
  { value: 'veryslow', label: t('config.sw_preset_veryslow') },
]

const TUNE_OPTIONS = [
  { value: 'film', label: t('config.sw_tune_film') },
  { value: 'animation', label: t('config.sw_tune_animation') },
  { value: 'grain', label: t('config.sw_tune_grain') },
  { value: 'stillimage', label: t('config.sw_tune_stillimage') },
  { value: 'fastdecode', label: t('config.sw_tune_fastdecode') },
  { value: 'zerolatency', label: t('config.sw_tune_zerolatency') },
]
</script>

<script>
export const OPTIONS = {
  "sw_preset": "superfast",
  "sw_tune": "zerolatency",
}
</script>

<template>
  <div id="software-encoder" class="config-page">
    <Select id="sw_preset" v-model="config.sw_preset" :label="$t('config.sw_preset')"
      :desc="$t('config.sw_preset_desc')" :options="PRESET_OPTIONS" />

    <Select id="sw_tune" v-model="config.sw_tune" :label="$t('config.sw_tune')" :desc="$t('config.sw_tune_desc')"
      :options="TUNE_OPTIONS" />
  </div>
</template>

<style scoped>

</style>
