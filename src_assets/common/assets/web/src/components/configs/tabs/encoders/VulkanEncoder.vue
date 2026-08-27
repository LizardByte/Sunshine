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

const TUNE_OPTIONS = [
  { value: '0', label: t('_common.auto') },
  { value: '1', label: t('config.vk_tune_hq') },
  { value: '2', label: t('config.vk_tune_ll') },
  { value: '3', label: t('config.vk_tune_ull') },
]

const RC_MODE_OPTIONS = [
  { value: '0', label: t('_common.auto') },
  { value: '1', label: t('config.vk_rc_cqp') },
  { value: '2', label: t('config.vk_rc_cbr') },
  { value: '4', label: t('config.vk_rc_vbr') },
]
</script>

<script>
export const OPTIONS = {
  "vk_tune": 2,
  "vk_rc_mode": 2,
}
</script>

<template>
  <div id="vulkan-encoder" class="config-page">
    <!-- Tuning -->
    <Select id="vk_tune" v-model="config.vk_tune" :label="$t('config.vk_tune')" :desc="$t('config.vk_tune_desc')"
      :options="TUNE_OPTIONS" />

    <!-- Rate Control -->
    <Select id="vk_rc_mode" v-model="config.vk_rc_mode" :label="$t('config.vk_rc_mode')"
      :desc="$t('config.vk_rc_mode_desc')" :options="RC_MODE_OPTIONS" />
  </div>
</template>
