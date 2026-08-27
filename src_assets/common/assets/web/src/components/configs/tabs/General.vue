<script setup>
import Checkbox from '@/components/Checkbox.vue'
import PrepCommandList from '@/components/PrepCommandList.vue'
import Select from '@/components/Select.vue'
import { useConfigTab } from '@/composables/useConfigTab'
import { useI18n } from 'vue-i18n'
import { SUPPORTED_LOCALES } from '@/utils/locale'

const { t } = useI18n()

const LOG_LEVEL_OPTIONS = [
  { value: 0, label: t('config.min_log_level_0') },
  { value: 1, label: t('config.min_log_level_1') },
  { value: 2, label: t('config.min_log_level_2') },
  { value: 3, label: t('config.min_log_level_3') },
  { value: 4, label: t('config.min_log_level_4') },
  { value: 5, label: t('config.min_log_level_5') },
  { value: 6, label: t('config.min_log_level_6') },
]

const props = defineProps({
  platform: String,
  config: {
    type: Object,
    default: () => structuredClone(OPTIONS)
  }
})

const { config, getOwnConfigOptions } = useConfigTab(props.config, OPTIONS)

defineExpose({ getOwnConfigOptions })
</script>

<script>
export const OPTIONS = {
  "locale": "en",
  "sunshine_name": "",
  "min_log_level": 2,
  "global_prep_cmd": [],
  "notify_pre_releases": "disabled",
  "system_tray": "enabled",
}
</script>

<template>
  <div id="general" class="config-page">
    <!-- Locale -->
    <div class="mb-3">
      <label for="locale" class="form-label">{{ $t('config.locale') }}</label>
      <select id="locale" class="form-select" v-model="config.locale">
        <option v-for="loc in SUPPORTED_LOCALES" :key="loc.value" :value="loc.value">{{ loc.label }}</option>
      </select>
      <div class="form-text">{{ $t('config.locale_desc') }}</div>
    </div>

    <!-- Sunshine Name -->
    <div class="mb-3">
      <label for="sunshine_name" class="form-label">{{ $t('config.sunshine_name') }}</label>
      <input type="text" class="form-control" id="sunshine_name" placeholder="Sunshine"
        v-model="config.sunshine_name" />
      <div class="form-text">{{ $t('config.sunshine_name_desc') }}</div>
    </div>

    <!-- Log Level -->
    <Select id="min_log_level" v-model="config.min_log_level" :label="$t('config.min_log_level')"
      :desc="$t('config.min_log_level_desc')" :options="LOG_LEVEL_OPTIONS" />

    <!-- Global Prep Commands -->
    <PrepCommandList v-model="config.global_prep_cmd" :platform="platform" :label="$t('config.global_prep_cmd')"
      :description="$t('config.global_prep_cmd_desc')" :add-label="$t('config.add')" />

    <!-- Notify Pre-Releases -->
    <Checkbox class="mb-3" id="notify_pre_releases" locale-prefix="config" v-model="config.notify_pre_releases"
      default="false"></Checkbox>

    <!-- Enable system tray -->
    <Checkbox class="mb-3" id="system_tray" locale-prefix="config" v-model="config.system_tray" default="true">
    </Checkbox>
  </div>
</template>
