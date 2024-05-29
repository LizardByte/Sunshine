<script setup>
import { ref } from 'vue'
import PlatformLayout from '../../PlatformLayout.vue'

const props = defineProps([
  'platform',
  'config'
])

const config = ref(props.config)
</script>

<template>
  <div id="input" class="config-page">
    <!-- Enable Gamepad Input -->
    <div class="mb-3">
      <label for="controller" class="form-label">{{ $t('config.controller') }}</label>
      <select id="controller" class="form-select" v-model="config.controller">
        <option value="disabled">{{ $t('_common.disabled') }}</option>
        <option value="enabled">{{ $t('_common.enabled') }}</option>
      </select>
      <div class="form-text">{{ $t('config.controller_desc') }}</div>
    </div>

    <!-- Emulated Gamepad Type -->
    <div class="mb-3" v-if="config.controller === 'enabled' && platform !== 'macos'">
      <label for="gamepad" class="form-label">{{ $t('config.gamepad') }}</label>
      <select id="gamepad" class="form-select" v-model="config.gamepad">
        <option value="auto">{{ $t('_common.auto') }}</option>

        <PlatformLayout :platform="platform">
          <template #linux>
            <option value="ds5">{{ $t("config.gamepad_ds5") }}</option>
            <option value="switch">{{ $t("config.gamepad_switch") }}</option>
            <option value="xone">{{ $t("config.gamepad_xone") }}</option>
          </template>
          
          <template #windows>
            <option value="ds4">{{ $t('config.gamepad_ds4') }}</option>
            <option value="x360">{{ $t('config.gamepad_x360') }}</option>
          </template>
        </PlatformLayout>
      </select>
      <div class="form-text">{{ $t('config.gamepad_desc') }}</div>
    </div>

    <div class="accordion" v-if="config.gamepad === 'ds4'">
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button" type="button" data-bs-toggle="collapse"
                  data-bs-target="#panelsStayOpen-collapseOne">
            {{ $t('config.gamepad_ds4_manual') }}
          </button>
        </h2>
        <div id="panelsStayOpen-collapseOne" class="accordion-collapse collapse show"
             aria-labelledby="panelsStayOpen-headingOne">
          <div class="accordion-body">
            <div>
              <label for="ds4_back_as_touchpad_click" class="form-label">{{ $t('config.ds4_back_as_touchpad_click') }}</label>
              <select id="ds4_back_as_touchpad_click" class="form-select"
                      v-model="config.ds4_back_as_touchpad_click">
                <option value="disabled">{{ $t('_common.disabled') }}</option>
                <option value="enabled">{{ $t('_common.enabled_def') }}</option>
              </select>
              <div class="form-text">{{ $t('config.ds4_back_as_touchpad_click_desc') }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>
    <div class="accordion" v-if="config.controller === 'enabled' && config.gamepad === 'auto' && platform === 'windows'">
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button" type="button" data-bs-toggle="collapse"
                  data-bs-target="#panelsStayOpen-collapseOne">
            {{ $t('config.gamepad_auto') }}
          </button>
        </h2>
        <div id="panelsStayOpen-collapseOne" class="accordion-collapse collapse show"
             aria-labelledby="panelsStayOpen-headingOne">
          <div class="accordion-body">
            <div>
              <label for="motion_as_ds4" class="form-label">{{ $t('config.motion_as_ds4') }}</label>
              <select id="motion_as_ds4" class="form-select"
                      v-model="config.motion_as_ds4">
                <option value="disabled">{{ $t('_common.disabled') }}</option>
                <option value="enabled">{{ $t('_common.enabled_def') }}</option>
              </select>
              <div class="form-text">{{ $t('config.motion_as_ds4_desc') }}</div>
            </div>
            <div>
              <label for="touchpad_as_ds4" class="form-label">{{ $t('config.touchpad_as_ds4') }}</label>
              <select id="touchpad_as_ds4" class="form-select"
                      v-model="config.touchpad_as_ds4">
                <option value="disabled">{{ $t('_common.disabled') }}</option>
                <option value="enabled">{{ $t('_common.enabled_def') }}</option>
              </select>
              <div class="form-text">{{ $t('config.touchpad_as_ds4_desc') }}</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Home/Guide Button Emulation Timeout -->
    <div class="mb-3" v-if="config.controller === 'enabled'">
      <label for="back_button_timeout" class="form-label">{{ $t('config.back_button_timeout') }}</label>
      <input type="text" class="form-control" id="back_button_timeout" placeholder="-1"
             v-model="config.back_button_timeout" />
      <div class="form-text">{{ $t('config.back_button_timeout_desc') }}</div>
    </div>

    <!-- Enable Keyboard Input -->
    <hr>
    <div class="mb-3">
      <label for="keyboard" class="form-label">{{ $t('config.keyboard') }}</label>
      <select id="keyboard" class="form-select" v-model="config.keyboard">
        <option value="disabled">{{ $t('_common.disabled') }}</option>
        <option value="enabled">{{ $t('_common.enabled_def') }}</option>
      </select>
      <div class="form-text">{{ $t('config.keyboard_desc') }}</div>
    </div>

    <!-- Key Repeat Delay-->
    <div class="mb-3" v-if="config.keyboard === 'enabled' && platform === 'windows'">
      <label for="key_repeat_delay" class="form-label">{{ $t('config.key_repeat_delay') }}</label>
      <input type="text" class="form-control" id="key_repeat_delay" placeholder="500"
             v-model="config.key_repeat_delay" />
      <div class="form-text">{{ $t('config.key_repeat_delay_desc') }}</div>
    </div>

    <!-- Key Repeat Frequency-->
    <div class="mb-3" v-if="config.keyboard === 'enabled' && platform === 'windows'">
      <label for="key_repeat_frequency" class="form-label">{{ $t('config.key_repeat_frequency') }}</label>
      <input type="text" class="form-control" id="key_repeat_frequency" placeholder="24.9"
             v-model="config.key_repeat_frequency" />
      <div class="form-text">{{ $t('config.key_repeat_frequency_desc') }}</div>
    </div>

    <!-- Always send scancodes -->
    <div class="mb-3" v-if="config.keyboard === 'enabled' && platform === 'windows'">
      <label for="always_send_scancodes" class="form-label">{{ $t('config.always_send_scancodes') }}</label>
      <select id="always_send_scancodes" class="form-select" v-model="config.always_send_scancodes">
        <option value="disabled">{{ $t('_common.disabled') }}</option>
        <option value="enabled">{{ $t('_common.enabled_def') }}</option>
      </select>
      <div class="form-text">{{ $t('config.always_send_scancodes_desc') }}</div>
    </div>

    <!-- Mapping Key AltRight to Key Windows -->
    <div class="mb-3" v-if="config.keyboard === 'enabled'">
      <label for="key_rightalt_to_key_win" class="form-label">{{ $t('config.key_rightalt_to_key_win') }}</label>
      <select id="key_rightalt_to_key_win" class="form-select" v-model="config.key_rightalt_to_key_win">
        <option value="disabled">{{ $t('_common.disabled') }}</option>
        <option value="enabled">{{ $t('_common.enabled_def') }}</option>
      </select>
      <div class="form-text">{{ $t('config.key_rightalt_to_key_win_desc') }}</div>
    </div>

    <!-- Enable Mouse Input -->
    <hr>
    <div class="mb-3">
      <label for="mouse" class="form-label">{{ $t('config.mouse') }}</label>
      <select id="mouse" class="form-select" v-model="config.mouse">
        <option value="disabled">{{ $t('_common.disabled') }}</option>
        <option value="enabled">{{ $t('_common.enabled_def') }}</option>
      </select>
      <div class="form-text">{{ $t('config.mouse_desc') }}</div>
    </div>

    <!-- High resolution scrolling support -->
    <div class="mb-3" v-if="config.mouse === 'enabled'">
      <label for="high_resolution_scrolling" class="form-label">{{ $t('config.high_resolution_scrolling') }}</label>
      <select id="high_resolution_scrolling" class="form-select" v-model="config.high_resolution_scrolling">
        <option value="disabled">{{ $t('_common.disabled') }}</option>
        <option value="enabled">{{ $t('_common.enabled_def') }}</option>
      </select>
      <div class="form-text">{{ $t('config.high_resolution_scrolling_desc') }}</div>
    </div>

    <!-- Native pen/touch support -->
    <div class="mb-3" v-if="config.mouse === 'enabled'">
      <label for="native_pen_touch" class="form-label">{{ $t('config.native_pen_touch') }}</label>
      <select id="native_pen_touch" class="form-select" v-model="config.native_pen_touch">
        <option value="disabled">{{ $t('_common.disabled') }}</option>
        <option value="enabled">{{ $t('_common.enabled_def') }}</option>
      </select>
      <div class="form-text">{{ $t('config.native_pen_touch_desc') }}</div>
    </div>

  </div>
</template>

<style scoped>

</style>
