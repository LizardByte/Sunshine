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

const RC_OPTIONS = [
  { value: 'auto', label: t('_common.auto') },
  { value: 'avbr', label: t('config.vaapi_rc_avbr') },
  { value: 'vbr', label: t('config.vaapi_rc_vbr') },
  { value: 'cbr', label: t('config.vaapi_rc_cbr') },
  { value: 'cqp', label: t('config.vaapi_rc_cqp') },
  { value: 'icq', label: t('config.vaapi_rc_icq') },
  { value: 'qvbr', label: t('config.vaapi_rc_qvbr') },
]

const QUALITY_OPTIONS = [
  { value: 'auto', label: t('_common.auto') },
  { value: 'speed', label: t('config.vaapi_quality_speed') },
  { value: 'balanced', label: t('config.vaapi_quality_balanced') },
  { value: 'quality', label: t('config.vaapi_quality_quality') },
]
</script>

<script>
export const OPTIONS = {
  "vaapi_blbrc": "disabled",
  "vaapi_quality": "auto",
  "vaapi_rc": "auto",
  "vaapi_strict_rc_buffer": "disabled",
}
</script>

<template>
  <div id="vaapi-encoder" class="config-page">
    <!-- VAAPI Rate Control group options -->
    <div class="mb-3 accordion">
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button" type="button" data-bs-toggle="collapse"
                  data-bs-target="#panelsStayOpen-collapseOne">
            {{ $t('config.vaapi_rc_group') }}
          </button>
        </h2>
        <div id="panelsStayOpen-collapseOne" class="accordion-collapse collapse show"
             aria-labelledby="panelsStayOpen-headingOne">
          <div class="accordion-body">
            <!-- VAAPI Rate Control -->
            <Select id="vaapi_rc" v-model="config.vaapi_rc" :label="$t('config.vaapi_rc')"
              :desc="$t('config.vaapi_rc_desc')" :options="RC_OPTIONS" />

            <!-- BLBRC -->
            <Checkbox class="mb-3"
                      id="vaapi_blbrc"
                      locale-prefix="config"
                      v-model="config.vaapi_blbrc"
                      default="false"
            ></Checkbox>

            <!-- Strict RC Buffer -->
            <Checkbox class="mb-3"
                      id="vaapi_strict_rc_buffer"
                      locale-prefix="config"
                      v-model="config.vaapi_strict_rc_buffer"
                      default="false"
            ></Checkbox>
          </div>
        </div>
      </div>
    </div>

    <!-- VAAPI Quality group options -->
    <div class="mb-3 accordion">
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button" type="button" data-bs-toggle="collapse"
                  data-bs-target="#panelsStayOpen-collapseTwo">
            {{ $t('config.vaapi_quality_group') }}
          </button>
        </h2>
        <div id="panelsStayOpen-collapseTwo" class="accordion-collapse collapse show"
             aria-labelledby="panelsStayOpen-headingTwo">
          <div class="accordion-body">
            <!-- VAAPI Quality -->
            <Select id="vaapi_quality" v-model="config.vaapi_quality" :label="$t('config.vaapi_quality')"
              :desc="$t('config.vaapi_quality_desc')" :options="QUALITY_OPTIONS" />
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>

</style>
