<script setup>
import { ref } from 'vue'
import { $tp } from '../../../platform-i18n'
import PlatformLayout from '../../../PlatformLayout.vue'

const props = defineProps({
  platform: String,
  config: Object,
  display_mode_remapping: Array
})

const config = ref(props.config)
const display_mode_remapping = ref(props.display_mode_remapping)

// TODO: Sample for use in PR #2032
function getRemappingType() {
  // Assuming here that at least one setting is set to "automatic"
  if (config.value.resolution_change !== 'automatic') {
    return "refresh_rate_only";
  }
  if (config.value.refresh_rate_change !== 'automatic') {
    return "resolution_only";
  }
  return "";
}

function addRemapping(type) {
  let template = {
    type: type,
    received_resolution: "",
    received_fps: "",
    final_resolution: "",
    final_refresh_rate: "",
  };

  display_mode_remapping.value.push(template);
}
</script>

<template>
  <PlatformLayout :platform="platform">
    <template #windows>
      <div class="mb-3 accordion">
        <div class="accordion-item">
          <h2 class="accordion-header">
            <button class="accordion-button" type="button" data-bs-toggle="collapse"
              data-bs-target="#panelsStayOpen-collapseOne">
              {{ $tp('config.display_device_options') }}
            </button>
          </h2>
          <div id="panelsStayOpen-collapseOne" class="accordion-collapse collapse show"
            aria-labelledby="panelsStayOpen-headingOne">
            <div class="accordion-body">
              <div class="mb-3">
                <label class="form-label">
                  {{ $tp('config.display_device_options_note') }}
                </label>
                <div class="form-text">
                  <p style="white-space: pre-line">{{ $tp('config.display_device_options_note_desc') }}</p>
                </div>
              </div>


              <!-- Device display preparation -->
              <div class="mb-3">
                <label for="display_device_prep" class="form-label">
                  {{ $tp('config.display_device_prep') }}
                </label>
                <select id="display_device_prep" class="form-select" v-model="config.display_device_prep">
                  <option value="no_operation">{{ $tp('config.display_device_prep_no_operation') }}</option>
                  <option value="ensure_active">{{ $tp('config.display_device_prep_ensure_active') }}</option>
                  <option value="ensure_primary">{{ $tp('config.display_device_prep_ensure_primary') }}</option>
                  <option value="ensure_only_display">{{ $tp('config.display_device_prep_ensure_only_display') }}
                  </option>
                </select>
              </div>

              <!-- Resolution change -->
              <div class="mb-3">
                <label for="resolution_change" class="form-label">
                  {{ $tp('config.resolution_change') }}
                </label>
                <select id="resolution_change" class="form-select" v-model="config.resolution_change">
                  <option value="no_operation">{{ $tp('config.resolution_change_no_operation') }}</option>
                  <option value="automatic">{{ $tp('config.resolution_change_automatic') }}</option>
                  <option value="manual">{{ $tp('config.resolution_change_manual') }}</option>
                </select>
                <div class="form-text"
                  v-if="config.resolution_change === 'automatic' || config.resolution_change === 'manual'">
                  {{ $tp('config.resolution_change_ogs_desc') }}
                </div>

                <!-- Manual resolution -->
                <div class="mt-2 ps-4" v-if="config.resolution_change === 'manual'">
                  <div class="form-text">
                    {{ $tp('config.resolution_change_manual_desc') }}
                  </div>
                  <input type="text" class="form-control" id="manual_resolution" placeholder="2560x1440"
                    v-model="config.manual_resolution" />
                </div>
              </div>

              <!-- Refresh rate change -->
              <div class="mb-3">
                <label for="refresh_rate_change" class="form-label">
                  {{ $tp('config.refresh_rate_change') }}
                </label>
                <select id="refresh_rate_change" class="form-select" v-model="config.refresh_rate_change">
                  <option value="no_operation">{{ $tp('config.refresh_rate_change_no_operation') }}</option>
                  <option value="automatic">{{ $tp('config.refresh_rate_change_automatic') }}</option>
                  <option value="manual">{{ $tp('config.refresh_rate_change_manual_desc') }}</option>
                </select>

                <!-- Manual refresh rate -->
                <div class="mt-2 ps-4" v-if="config.refresh_rate_change === 'manual'">
                  <div class="form-text">
                    {{ $tp('config.refresh_rate_change_manual_desc') }}
                  </div>
                  <input type="text" class="form-control" id="manual_refresh_rate" placeholder="59.95"
                    v-model="config.manual_refresh_rate" />
                </div>
              </div>

              <!-- HDR preparation -->
              <div class="mb-3">
                <label for="hdr_prep" class="form-label">
                  {{ $tp('config.hdr_prep') }}
                </label>
                <select id="hdr_prep" class="form-select" v-model="config.hdr_prep">
                  <option value="no_operation">{{ $tp('config.hdr_prep_no_operation') }}</option>
                  <option value="automatic">{{ $tp('config.hdr_prep_automatic') }}</option>
                </select>
              </div>

              <!-- Display mode remapping -->
              <div class="mb-3"
                v-if="config.resolution_change === 'automatic' || config.refresh_rate_change === 'automatic'">
                <label for="display_mode_remapping" class="form-label">
                  {{ $tp('config.display_mode_remapping') }}
                </label>
                <div id="display_mode_remapping" class="d-flex flex-column">
                  <div class="form-text">
                    <p style="white-space: pre-line">{{ $tp('config.display_mode_remapping_desc') }}</p>
                    <p v-if="getRemappingType() === ''" style="white-space: pre-line">{{
                      $tp('config.display_mode_remapping_default_mode_desc') }}</p>
                    <p v-if="getRemappingType() === 'resolution_only'" style="white-space: pre-line">{{
                      $tp('config.display_mode_remapping_resolution_only_mode_desc') }}</p>
                  </div>

                  <!--
                  Note: this 3-way persistence below may seem stupid, but is necessary, because if you have something like:

                  WIDTH_1xHEIGHT_1x60 -> OTHER_WIDTHxOTHER_HEIGHTx120
                  WIDTH_2xHEIGHT_2x60 -> OTHER_WIDTHxOTHER_HEIGHTx90
                  WIDTH_1xHEIGHT_1x30 -> OTHER_WIDTHxOTHER_HEIGHTx60
                        
                  after removing 1 component it becomes nonsense:

                  60 -> 120
                  60 -> 90
                  30 -> 60

                  and if the user wants the second entry, he would have to remove the first one, which would be valid in other config combination.
                  -->
                  <table class="table"
                    v-if="display_mode_remapping.filter((value) => value.type === getRemappingType()).length > 0">
                    <thead>
                      <tr>
                        <th scope="col" v-if="getRemappingType() !== 'refresh_rate_only'">{{
                          $tp('config.display_mode_remapping_received_resolution') }}</th>
                        <th scope="col" v-if="getRemappingType() !== 'resolution_only'">{{
                          $tp('config.display_mode_remapping_received_fps') }}</th>
                        <th scope="col" v-if="getRemappingType() !== 'refresh_rate_only'">{{
                          $tp('config.display_mode_remapping_final_resolution') }}</th>
                        <th scope="col" v-if="getRemappingType() !== 'resolution_only'">{{
                          $tp('config.display_mode_remapping_final_refresh_rate') }}</th>
                        <th scope="col"></th>
                      </tr>
                    </thead>
                    <tbody>
                      <tr v-for="(c, i) in display_mode_remapping">
                        <template v-if="c.type === '' && c.type === getRemappingType()">
                          <td>
                            <input type="text" class="form-control monospace" v-model="c.received_resolution"
                              :placeholder="`1920x1080 (${$t('config.display_mode_remapping_optional')})`" />
                          </td>
                          <td>
                            <input type="text" class="form-control monospace" v-model="c.received_fps"
                              :placeholder="`60 (${$t('config.display_mode_remapping_optional')})`" />
                          </td>
                          <td>
                            <input type="text" class="form-control monospace" v-model="c.final_resolution"
                              :placeholder="`2560x1440 (${$t('config.display_mode_remapping_optional')})`" />
                          </td>
                          <td>
                            <input type="text" class="form-control monospace" v-model="c.final_refresh_rate"
                              :placeholder="`119.95 (${$t('config.display_mode_remapping_optional')})`" />
                          </td>
                          <td>
                            <button class="btn btn-danger" @click="display_mode_remapping.splice(i, 1)">
                              <i class="fas fa-trash"></i>
                            </button>
                          </td>
                        </template>
                        <template v-if="c.type === 'resolution_only' && c.type === getRemappingType()">
                          <td>
                            <input type="text" class="form-control monospace" v-model="c.received_resolution"
                              placeholder="1920x1080" />
                          </td>
                          <td>
                            <input type="text" class="form-control monospace" v-model="c.final_resolution"
                              placeholder="2560x1440" />
                          </td>
                          <td>
                            <button class="btn btn-danger" @click="display_mode_remapping.splice(i, 1)">
                              <i class="fas fa-trash"></i>
                            </button>
                          </td>
                        </template>
                        <template v-if="c.type === 'refresh_rate_only' && c.type === getRemappingType()">
                          <td>
                            <input type="text" class="form-control monospace" v-model="c.received_fps"
                              placeholder="60" />
                          </td>
                          <td>
                            <input type="text" class="form-control monospace" v-model="c.final_refresh_rate"
                              placeholder="119.95" />
                          </td>
                          <td>
                            <button class="btn btn-danger" @click="display_mode_remapping.splice(i, 1)">
                              <i class="fas fa-trash"></i>
                            </button>
                          </td>
                        </template>
                      </tr>
                    </tbody>
                  </table>
                  <button class="ms-0 mt-2 btn btn-success" style="margin: 0 auto"
                    @click="addRemapping(getRemappingType())">
                    &plus; Add
                  </button>
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
