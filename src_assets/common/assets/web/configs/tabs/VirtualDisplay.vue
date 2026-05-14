<script setup>
import { ref, onMounted, computed, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { Plus, Trash2 } from 'lucide-vue-next'
import PlatformLayout from '../../PlatformLayout.vue'
const { t } = useI18n()

const props = defineProps({
  platform: String,
  config: Object,
})

const vddStatus = ref({
  initialized: false,
  driver_ok: false,
  driver_version: '(unknown)',
  displays: [],
  display_count: 0,
})

const isLoading = ref(false)
const errorMsg = ref('')
const successMsg = ref('')

const isWin = computed(() => props.platform === 'windows')

const customWidth = ref(1920)
const customHeight = ref(1080)
const customHz = ref(144)

const presets = [
  { label: '720p', w: 1280, h: 720 },
  { label: '1080p', w: 1920, h: 1080 },
  { label: '1440p', w: 2560, h: 1440 },
  { label: '4K', w: 3840, h: 2160 },
]

const isValidResolution = computed(() => {
  return customWidth.value >= 320 && customWidth.value <= 7680 &&
         customHeight.value >= 240 && customHeight.value <= 4320 &&
         customHz.value >= 30 && customHz.value <= 240
})

watch(() => props.config, (cfg) => {
  if (cfg) {
    customWidth.value = Number.parseInt(cfg.vdd_width) || 1920
    customHeight.value = Number.parseInt(cfg.vdd_height) || 1080
    customHz.value = Number.parseInt(cfg.vdd_refresh_rate) || 144
  }
}, { immediate: true })

const fetchStatus = async () => {
  if (!isWin.value) return
  isLoading.value = true
  try {
    const resp = await fetch('/api/vdd/status')
    const data = await resp.json()
    if (data.status) {
      vddStatus.value = data
    }
  } catch (e) {
    errorMsg.value = t('config.vdd_fetch_error') + ': ' + e.message
  } finally {
    isLoading.value = false
  }
}

const addDisplay = async () => {
  if (!isWin.value) return
  errorMsg.value = ''
  successMsg.value = ''
  isLoading.value = true

  try {
    const body = JSON.stringify({
      width: customWidth.value,
      height: customHeight.value,
      hz: customHz.value,
    })
    const resp = await fetch('/api/vdd/add', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body,
    })
    const data = await resp.json()
    if (data.success) {
      successMsg.value = `${t('config.vdd_add_display')} (${customWidth.value}x${customHeight.value}@${customHz.value}Hz)`
      await fetchStatus()
    } else {
      errorMsg.value = data.error || t('config.vdd_add_error')
    }
  } catch (e) {
    errorMsg.value = t('config.vdd_add_error') + ': ' + e.message
  } finally {
    isLoading.value = false
  }
}

const applyPreset = (preset) => {
  customWidth.value = preset.w
  customHeight.value = preset.h
}

const removeDisplay = async (index) => {
  errorMsg.value = ''
  successMsg.value = ''
  try {
    const resp = await fetch('/api/vdd/remove', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ index }),
    })
    const data = await resp.json()
    if (data.success) {
      successMsg.value = `${t('config.vdd_remove')} #${index}`
      await fetchStatus()
    } else {
      errorMsg.value = data.error || t('config.vdd_remove_error')
    }
  } catch (e) {
    errorMsg.value = t('config.vdd_remove_error') + ': ' + e.message
  }
}

const removeAllDisplays = async () => {
  errorMsg.value = ''
  successMsg.value = ''
  try {
    const resp = await fetch('/api/vdd/remove-all', { method: 'POST' })
    const data = await resp.json()
    if (data.success) {
      successMsg.value = t('config.vdd_remove_all_success')
      await fetchStatus()
    } else {
      errorMsg.value = data.error || t('config.vdd_remove_all_error')
    }
  } catch (e) {
    errorMsg.value = t('config.vdd_remove_all_error') + ': ' + e.message
  }
}

onMounted(() => {
  if (isWin.value) {
    fetchStatus()
  }
})
</script>

<template>
  <PlatformLayout :platform="platform">
    <template #windows>
      <div class="mb-3 accordion">
        <div class="accordion-item">
          <h2 class="accordion-header">
            <button class="accordion-button" type="button" data-bs-toggle="collapse"
                    data-bs-target="#vdd-collapse">
              {{ $t('config.vdd_tab_title') }}
            </button>
          </h2>
          <div id="vdd-collapse" class="accordion-collapse collapse show">
            <div class="accordion-body">

              <!-- Status alerts -->
              <div class="alert alert-info" v-if="!vddStatus.initialized">
                {{ $t('config.vdd_not_initialized') }}
              </div>

              <div class="alert alert-danger" v-if="errorMsg">
                {{ errorMsg }}
              </div>

              <div class="alert alert-success" v-if="successMsg">
                {{ successMsg }}
              </div>

              <!-- VDD Settings -->
              <div class="row mb-3">
                <div class="col-md-6">
                  <p class="form-label mb-0">{{ $t('config.vdd_driver_status') }}</p>
                  <p class="mb-0">
                    <span :class="vddStatus.driver_ok ? 'text-success' : 'text-danger'">
                      {{ vddStatus.driver_ok ? $t('config.vdd_connected') : $t('config.vdd_not_connected') }}
                    </span>
                    <span class="ms-2 text-muted">v{{ vddStatus.driver_version }}</span>
                  </p>
                </div>
                <div class="col-md-6">
                  <p class="form-label mb-0">{{ $t('config.vdd_active_displays') }}</p>
                  <p class="mb-0">
                    <strong>{{ vddStatus.display_count }}</strong>
                    <button class="btn btn-sm btn-outline-secondary ms-2" @click="fetchStatus" :disabled="isLoading">
                      {{ $t('config.vdd_refresh') }}
                    </button>
                  </p>
                </div>
              </div>

              <!-- Resolution presets -->
              <div class="mb-2">
                <p class="form-label mb-0">{{ $t('config.vdd_presets') }}</p>
                <div class="d-flex gap-1 flex-wrap">
                  <button v-for="p in presets" :key="p.label"
                          class="btn btn-sm btn-outline-secondary"
                          @click="applyPreset(p)">
                    {{ p.label }}
                  </button>
                </div>
              </div>

              <!-- Custom resolution inputs -->
              <div class="row mb-3 g-2 align-items-end">
                <div class="col-auto">
                  <label class="form-label" for="vdd-width">{{ $t('config.vdd_input_width') }}</label>
                  <input id="vdd-width" type="number" class="form-control form-control-sm"
                         v-model.number="customWidth" min="320" max="7680" style="width: 90px">
                </div>
                <div class="col-auto">
                  <label class="form-label" for="vdd-height">{{ $t('config.vdd_input_height') }}</label>
                  <input id="vdd-height" type="number" class="form-control form-control-sm"
                         v-model.number="customHeight" min="240" max="4320" style="width: 90px">
                </div>
                <div class="col-auto">
                  <label class="form-label" for="vdd-hz">{{ $t('config.vdd_input_hz') }}</label>
                  <input id="vdd-hz" type="number" class="form-control form-control-sm"
                         v-model.number="customHz" min="30" max="240" style="width: 80px">
                </div>
              </div>

              <!-- Validation hint -->
              <div class="text-danger small mb-2" v-if="!isValidResolution">
                {{ $t('config.vdd_validation_hint') }}
              </div>

              <!-- Controls -->
              <div class="d-flex gap-2 mb-3">
                <button class="btn btn-success" @click="addDisplay"
                        :disabled="isLoading || !vddStatus.initialized || !isValidResolution">
                  <Plus :size="16" class="me-1" /> {{ $t('config.vdd_add_display') }} ({{ customWidth }}x{{ customHeight }}@{{ customHz }})
                </button>
                <button class="btn btn-warning" @click="removeAllDisplays"
                        :disabled="isLoading || vddStatus.display_count === 0">
                  <Trash2 :size="16" class="me-1" /> {{ $t('config.vdd_remove_all') }}
                </button>
              </div>

              <!-- Display List -->
              <div v-if="vddStatus.displays.length > 0" class="mb-3">
                <h6>{{ $t('config.vdd_virtual_displays') }}</h6>
                <table class="table table-sm">
                  <thead>
                    <tr>
                      <th>{{ $t('config.vdd_col_index') }}</th>
                      <th>{{ $t('config.vdd_col_device') }}</th>
                      <th>{{ $t('config.vdd_col_resolution') }}</th>
                      <th>{{ $t('config.vdd_col_refresh') }}</th>
                      <th>{{ $t('config.vdd_col_action') }}</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr v-for="display in vddStatus.displays" :key="display.index">
                      <td>{{ display.index }}</td>
                      <td><code>{{ display.device_name }}</code></td>
                      <td>{{ display.width }}×{{ display.height }}</td>
                      <td>{{ display.hz }} Hz</td>
                      <td>
                        <button class="btn btn-sm btn-danger"
                                @click="removeDisplay(display.index)"
                                :disabled="isLoading">
                          <Trash2 :size="14" /> {{ $t('config.vdd_remove') }}
                        </button>
                      </td>
                    </tr>
                  </tbody>
                </table>
              </div>

              <div v-else-if="vddStatus.initialized" class="text-muted">
                {{ $t('config.vdd_no_displays') }}
              </div>

              <!-- Install driver hint -->
              <div class="mt-3 alert alert-secondary small">
                <strong>{{ $t('config.vdd_driver_installation') }}</strong><br>
                {{ $t('config.vdd_driver_install_text') }}
                <a href="https://builds.parsec.app/vdd/parsec-vdd-0.45.0.0.exe" target="_blank">here</a>
                {{ $t('config.vdd_requires_admin') }}
              </div>
            </div>
          </div>
        </div>
      </div>
    </template>
    <template #freebsd></template>
    <template #linux>
      <div class="alert alert-info">
        {{ $t('config.vdd_only_windows') }}
      </div>
    </template>
    <template #macos>
      <div class="alert alert-info">
        {{ $t('config.vdd_only_windows') }}
      </div>
    </template>
  </PlatformLayout>
</template>
