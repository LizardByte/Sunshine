<script setup>
import { ref } from 'vue'
import { $tp } from '../../../platform-i18n'
import PlatformLayout from '../../../PlatformLayout.vue'

const props = defineProps({
  platform: String,
  config: Object
})

const config = ref(props.config)
</script>

<template>
  <PlatformLayout :platform="platform">
    <template #windows>
      <div class="mb-3 accordion">
        <div class="accordion-item">
          <h2 class="accordion-header">
            <button class="accordion-button" type="button" data-bs-toggle="collapse"
                    data-bs-target="#panelsStayOpen-collapseOne">
              {{ $t('config.dd_options_header') }}
            </button>
          </h2>
          <div id="panelsStayOpen-collapseOne" class="accordion-collapse collapse show"
               aria-labelledby="panelsStayOpen-headingOne">
            <div class="accordion-body">

              <!-- Configuration option -->
              <div class="mb-3">
                <label for="dd_configuration_option" class="form-label">
                  {{ $t('config.dd_config_label') }}
                </label>
                <select id="dd_configuration_option" class="form-select" v-model="config.dd_configuration_option">
                  <option value="disabled">{{ $t('config.dd_config_disabled') }}</option>
                  <option value="verify_only">{{ $t('config.dd_config_verify_only') }}</option>
                  <option value="ensure_active">{{ $t('config.dd_config_ensure_active') }}</option>
                  <option value="ensure_primary">{{ $t('config.dd_config_ensure_primary') }}</option>
                  <option value="ensure_only_display">{{ $t('config.dd_config_ensure_only_display') }}</option>
                </select>
              </div>

              <!-- Resolution option -->
              <div class="mb-3" v-if="config.dd_configuration_option !== 'disabled'">
                <label for="dd_resolution_option" class="form-label">
                  {{ $t('config.dd_resolution_option') }}
                </label>
                <select id="dd_resolution_option" class="form-select" v-model="config.dd_resolution_option">
                  <option value="disabled">{{ $t('config.dd_resolution_option_disabled') }}</option>
                  <option value="automatic">{{ $t('config.dd_resolution_option_automatic') }}</option>
                  <option value="manual">{{ $t('config.dd_resolution_option_manual') }}</option>
                </select>
                <div class="form-text"
                     v-if="config.dd_resolution_option === 'automatic' || config.dd_resolution_option === 'manual'">
                  {{ $t('config.dd_resolution_option_ogs_desc') }}
                </div>

                <!-- Manual resolution -->
                <div class="mt-2 ps-4" v-if="config.dd_resolution_option === 'manual'">
                  <div class="form-text">
                    {{ $t('config.dd_resolution_option_manual_desc') }}
                  </div>
                  <input type="text" class="form-control" id="dd_manual_resolution" placeholder="2560x1440"
                         v-model="config.dd_manual_resolution" />
                </div>
              </div>

              <!-- Refresh rate option -->
              <div class="mb-3" v-if="config.dd_configuration_option !== 'disabled'">
                <label for="dd_refresh_rate_option" class="form-label">
                  {{ $t('config.dd_refresh_rate_option') }}
                </label>
                <select id="dd_refresh_rate_option" class="form-select" v-model="config.dd_refresh_rate_option">
                  <option value="disabled">{{ $t('config.dd_refresh_rate_option_disabled') }}</option>
                  <option value="automatic">{{ $t('config.dd_refresh_rate_option_automatic') }}</option>
                  <option value="manual">{{ $t('config.dd_refresh_rate_option_manual') }}</option>
                </select>

                <!-- Manual refresh rate -->
                <div class="mt-2 ps-4" v-if="config.dd_refresh_rate_option === 'manual'">
                  <div class="form-text">
                    {{ $t('config.dd_refresh_rate_option_manual_desc') }}
                  </div>
                  <input type="text" class="form-control" id="dd_manual_refresh_rate" placeholder="59.9558"
                         v-model="config.dd_manual_refresh_rate" />
                </div>
              </div>

              <!-- HDR option -->
              <div class="mb-3" v-if="config.dd_configuration_option !== 'disabled'">
                <label for="dd_hdr_option" class="form-label">
                  {{ $t('config.dd_hdr_option') }}
                </label>
                <select id="dd_hdr_option" class="mb-3 form-select" v-model="config.dd_hdr_option">
                  <option value="disabled">{{ $t('config.dd_hdr_option_disabled') }}</option>
                  <option value="automatic">{{ $t('config.dd_hdr_option_automatic') }}</option>
                </select>
                <div class="form-check">
                  <!-- HDR toggle -->
                  <label for="dd_wa_hdr_toggle" class="form-check-label">{{ $tp('config.dd_wa_hdr_toggle') }}</label>
                  <input type="checkbox" class="form-check-input" id="dd_wa_hdr_toggle" true-value="true" false-value="false" v-model="config.dd_wa_hdr_toggle" />
                  <div class="form-text">{{ $tp('config.dd_wa_hdr_toggle_desc') }}</div>
                </div>
              </div>

              <!-- Config revert delay -->
              <div class="mb-3" v-if="config.dd_configuration_option !== 'disabled'">
                <label for="dd_config_revert_delay" class="form-label">
                  {{ $t('config.dd_config_revert_delay') }}
                </label>
                <input type="text" class="form-control" id="dd_config_revert_delay" placeholder="10000"
                       v-model="config.dd_config_revert_delay" />
                <div class="form-text">
                  {{ $t('config.dd_config_revert_delay_desc') }}
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </template>
    <template #linux>
    </template>
    <template #macos>
    </template>
  </PlatformLayout>
</template>
