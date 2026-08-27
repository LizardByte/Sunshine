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

const USAGE_OPTIONS = [
  { value: 'transcoding', label: t('config.amd_usage_transcoding') },
  { value: 'webcam', label: t('config.amd_usage_webcam') },
  { value: 'lowlatency_high_quality', label: t('config.amd_usage_lowlatency_high_quality') },
  { value: 'lowlatency', label: t('config.amd_usage_lowlatency') },
  { value: 'ultralowlatency', label: t('config.amd_usage_ultralowlatency') },
]

const RC_OPTIONS = [
  { value: 'cbr', label: t('config.amd_rc_cbr') },
  { value: 'cqp', label: t('config.amd_rc_cqp') },
  { value: 'vbr_latency', label: t('config.amd_rc_vbr_latency') },
  { value: 'vbr_peak', label: t('config.amd_rc_vbr_peak') },
]

const QUALITY_OPTIONS = [
  { value: 'speed', label: t('config.amd_quality_speed') },
  { value: 'balanced', label: t('config.amd_quality_balanced') },
  { value: 'quality', label: t('config.amd_quality_quality') },
]

const CODER_OPTIONS = [
  { value: 'auto', label: t('config.ffmpeg_auto') },
  { value: 'cabac', label: t('config.coder_cabac') },
  { value: 'cavlc', label: t('config.coder_cavlc') },
]
</script>

<script>
export const OPTIONS = {
  "amd_usage": "ultralowlatency",
  "amd_rc": "vbr_latency",
  "amd_enforce_hrd": "disabled",
  "amd_max_au_size": "",
  "amd_quality": "balanced",
  "amd_preanalysis": "disabled",
  "amd_vbaq": "enabled",
  "amd_coder": "auto",
}
</script>

<template>
  <div id="amd-amf-encoder" class="config-page">
    <!-- AMF Usage -->
    <Select id="amd_usage" v-model="config.amd_usage" :label="$t('config.amd_usage')"
      :desc="$t('config.amd_usage_desc')" :options="USAGE_OPTIONS" />

    <!-- AMD Rate Control group options -->
    <div class="mb-3 accordion">
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button" type="button" data-bs-toggle="collapse"
            data-bs-target="#panelsStayOpen-collapseOne">
            {{ $t('config.amd_rc_group') }}
          </button>
        </h2>
        <div id="panelsStayOpen-collapseOne" class="accordion-collapse collapse show"
          aria-labelledby="panelsStayOpen-headingOne">
          <div class="accordion-body">
            <!-- AMF Rate Control -->
            <Select id="amd_rc" v-model="config.amd_rc" :label="$t('config.amd_rc')" :desc="$t('config.amd_rc_desc')"
              :options="RC_OPTIONS" />

            <!-- AMF HRD Enforcement -->
            <Checkbox class="mb-3" id="amd_enforce_hrd" locale-prefix="config" v-model="config.amd_enforce_hrd"
              default="false"></Checkbox>

            <!-- AMF Max AU Size -->
            <div class="mb-3">
              <label for="amd_max_au_size" class="form-label">{{ $t('config.amd_max_au_size') }}</label>
              <input type="number" class="form-control" id="amd_max_au_size" placeholder="-1" min="-1" max="2147483647"
                v-model="config.amd_max_au_size" />
              <div class="form-text">{{ $t('config.amd_max_au_size_desc') }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- AMF Quality group options -->
    <div class="mb-3 accordion">
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button" type="button" data-bs-toggle="collapse"
            data-bs-target="#panelsStayOpen-collapseTwo">
            {{ $t('config.amd_quality_group') }}
          </button>
        </h2>
        <div id="panelsStayOpen-collapseTwo" class="accordion-collapse collapse show"
          aria-labelledby="panelsStayOpen-headingTwo">
          <div class="accordion-body">
            <!-- AMF Quality -->
            <Select id="amd_quality" v-model="config.amd_quality" :label="$t('config.amd_quality')"
              :desc="$t('config.amd_quality_desc')" :options="QUALITY_OPTIONS" />

            <!-- AMD Preanalysis -->
            <Checkbox class="mb-3" id="amd_preanalysis" locale-prefix="config" v-model="config.amd_preanalysis"
              default="false"></Checkbox>

            <!-- AMD VBAQ -->
            <Checkbox class="mb-3" id="amd_vbaq" locale-prefix="config" v-model="config.amd_vbaq" default="true">
            </Checkbox>

            <!-- AMF Coder (H264) -->
            <Select id="amd_coder" v-model="config.amd_coder" :label="$t('config.amd_coder')"
              :desc="$t('config.amd_coder_desc')" :options="CODER_OPTIONS" />
          </div>
        </div>
      </div>
    </div>
  </div>
</template>
