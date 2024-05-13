<script setup>
import { ref } from 'vue'

const props = defineProps([
  'platform',
  'config',
])

const config = ref(props.config)
</script>

<template>
  <div id="amd-amf-encoder" class="config-page">
    <!-- AMF Usage -->
    <div class="mb-3">
      <label for="amd_usage" class="form-label">{{ $t('config.amd_usage') }}</label>
      <select id="amd_usage" class="form-select" v-model="config.amd_usage">
        <option value="transcoding">{{ $t('config.amd_usage_transcoding') }}</option>
        <option value="webcam">{{ $t('config.amd_usage_webcam') }}</option>
        <option value="lowlatency_high_quality">{{ $t('config.amd_usage_lowlatency_high_quality') }}</option>
        <option value="lowlatency">{{ $t('config.amd_usage_lowlatency') }}</option>
        <option value="ultralowlatency">{{ $t('config.amd_usage_ultralowlatency') }}</option>
      </select>
      <div class="form-text">{{ $t('config.amd_usage_desc') }}</div>
    </div>

    <!-- AMD Rate Control group options -->
    <div class="accordion">
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
            <div class="mb-3">
              <label for="amd_rc" class="form-label">{{ $t('config.amd_rc') }}</label>
              <select id="amd_rc" class="form-select" v-model="config.amd_rc">
                <option value="cbr">{{ $t('config.amd_rc_cbr') }}</option>
                <option value="cqp">{{ $t('config.amd_rc_cqp') }}</option>
                <option value="vbr_latency">{{ $t('config.amd_rc_vbr_latency') }}</option>
                <option value="vbr_peak">{{ $t('config.amd_rc_vbr_peak') }}</option>
              </select>
              <div class="form-text">{{ $t('config.amd_rc_desc') }}</div>
            </div>

            <!-- AMF HRD Enforcement -->
            <div class="mb-3">
              <label for="amd_enforce_hrd" class="form-label">{{ $t('config.amd_enforce_hrd') }}</label>
              <select id="amd_enforce_hrd" class="form-select" v-model="config.amd_enforce_hrd">
                <option value="enabled">{{ $t('_common.enabled') }}</option>
                <option value="disabled">{{ $t('_common.disabled_def') }}</option>
              </select>
              <div class="form-text">{{ $t('config.amd_enforce_hrd_desc') }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- AMF Quality group options -->
    <div class="accordion">
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
            <div class="mb-3">
              <label for="amd_quality" class="form-label">{{ $t('config.amd_quality') }}</label>
              <select id="amd_quality" class="form-select" v-model="config.amd_quality">
                <option value="speed">{{ $t('config.amd_quality_speed') }}</option>
                <option value="balanced">{{ $t('config.amd_quality_balanced') }}</option>
                <option value="quality">{{ $t('config.amd_quality_quality') }}</option>
              </select>
              <div class="form-text">{{ $t('config.amd_quality_desc') }}</div>
            </div>

            <!-- AMD Preanalysis -->
            <div class="mb-3">
              <label for="amd_preanalysis" class="form-label">{{ $t('config.amd_preanalysis') }}</label>
              <select id="amd_preanalysis" class="form-select" v-model="config.amd_preanalysis">
                <option value="disabled">{{ $t('_common.disabled_def') }}</option>
                <option value="enabled">{{ $t('_common.enabled') }}</option>
              </select>
              <div class="form-text">{{ $t('config.amd_preanalysis_desc') }}</div>
            </div>

            <!-- AMD VBAQ -->
            <div class="mb-3">
              <label for="amd_vbaq" class="form-label">{{ $t('config.amd_vbaq') }}</label>
              <select id="amd_vbaq" class="form-select" v-model="config.amd_vbaq">
                <option value="disabled">{{ $t('_common.disabled') }}</option>
                <option value="enabled">{{ $t('_common.enabled_def') }}</option>
              </select>
              <div class="form-text">{{ $t('config.amd_vbaq_desc') }}</div>
            </div>

            <!-- AMF Coder (H264) -->
            <div class="mb-3">
              <label for="amd_coder" class="form-label">{{ $t('config.amd_coder') }}</label>
              <select id="amd_coder" class="form-select" v-model="config.amd_coder">
                <option value="auto">{{ $t('config.ffmpeg_auto') }}</option>
                <option value="cabac">{{ $t('config.coder_cabac') }}</option>
                <option value="cavlc">{{ $t('config.coder_cavlc') }}</option>
              </select>
              <div class="form-text">{{ $t('config.amd_coder_desc') }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>

</style>
