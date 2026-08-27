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

const CODER_OPTIONS = [
  { value: 'auto', label: t('config.ffmpeg_auto') },
  { value: 'cabac', label: t('config.coder_cabac') },
  { value: 'cavlc', label: t('config.coder_cavlc') },
]

const SOFTWARE_OPTIONS = [
  { value: 'auto', label: t('_common.auto') },
  { value: 'disabled', label: t('_common.disabled') },
  { value: 'allowed', label: t('config.vt_software_allowed') },
  { value: 'forced', label: t('config.vt_software_forced') },
]
</script>

<script>
export const OPTIONS = {
  "vt_coder": "auto",
  "vt_software": "auto",
  "vt_realtime": "enabled",
}
</script>

<template>
  <div id="videotoolbox-encoder" class="config-page">
    <!-- Presets -->
    <Select id="vt_coder" v-model="config.vt_coder" :label="$t('config.vt_coder')" :options="CODER_OPTIONS" />
    <Select id="vt_software" v-model="config.vt_software" :label="$t('config.vt_software')"
      :options="SOFTWARE_OPTIONS" />
    <Checkbox class="mb-3"
              id="vt_realtime"
              desc=""
              locale-prefix="config"
              v-model="config.vt_realtime"
              default="true"
    ></Checkbox>
  </div>
</template>

<style scoped>

</style>
