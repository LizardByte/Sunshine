<script setup>
import { ref } from 'vue'
import Checkbox from "../../../Checkbox.vue";

const props = defineProps([
  'platform',
  'config',
])

const config = ref(props.config)
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
            <div class="mb-3">
              <label for="vaapi_rc" class="form-label">{{ $t('config.vaapi_rc') }}</label>
              <select id="vaapi_rc" class="form-select" v-model="config.vaapi_rc">
                <option value="auto">{{ $t('auto') }}</option>
                <option value="avbr">{{ $t('config.vaapi_rc_avbr') }}</option>
                <option value="vbr">{{ $t('config.vaapi_rc_vbr') }}</option>
                <option value="cbr">{{ $t('config.vaapi_rc_cbr') }}</option>
                <option value="cqp">{{ $t('config.vaapi_rc_cqp') }}</option>
                <option value="icq">{{ $t('config.vaapi_rc_icq') }}</option>
                <option value="qvbr">{{ $t('config.vaapi_rc_qvbr') }}</option>
              </select>
              <div class="form-text">{{ $t('config.vaapi_rc_desc') }}</div>
            </div>

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
            <div class="mb-3">
              <label for="vaapi_quality" class="form-label">{{ $t('config.vaapi_quality') }}</label>
              <select id="vaapi_quality" class="form-select" v-model="config.vaapi_quality">
                <option value="auto">{{ $t('auto') }}</option>
                <option value="speed">{{ $t('config.vaapi_quality_speed') }}</option>
                <option value="balanced">{{ $t('config.vaapi_quality_balanced') }}</option>
                <option value="quality">{{ $t('config.vaapi_quality_quality') }}</option>
              </select>
              <div class="form-text">{{ $t('config.vaapi_quality_desc') }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>

</style>
