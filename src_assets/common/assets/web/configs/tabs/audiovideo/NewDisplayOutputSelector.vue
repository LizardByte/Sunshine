<script setup>
import { ref, computed } from "vue";
import { $tp } from "../../../platform-i18n";
import PlatformLayout from "../../../PlatformLayout.vue";

const props = defineProps(["platform", "config", "displays"]);

const config = ref(props.config);
// const outputNamePlaceholder =
//   props.platform === "windows"
//     ? "{de9bb7e2-186e-505b-9e93-f48793333810}"
//     : "4531345";

// "DISPLAY NAME: \\\\.\\DISPLAY1\nFRIENDLY NAME: F32D80U\nDEVICE STATE: PRIMARY\nHDR STATE: ENABLED"
const displayDevices = computed(() =>
  config.value.display_devices.map(({ device_id, data = "" }) => ({
    id: device_id,
    name: data
      .replace(
        /.*?(DISPLAY\d+)?\nFRIENDLY NAME: (.*[^\n])*?\n.*\n.*/g,
        "$2 ($1)"
      )
      .replace("()", ""),
  }))
);
</script>

<template>
  <div class="mb-3">
    <label for="output_name" class="form-label">{{
      $t("config.output_name_windows")
    }}</label>
    <select id="output_name" class="form-select" v-model="config.output_name">
      <option value="">{{ $t("_common.autodetect") }}</option>
      <option
        v-for="device in displayDevices"
        :value="device.id"
        :key="device.id"
      >
        {{ device.name }}
      </option>
    </select>
    <div class="form-text">
      <p style="white-space: pre-line">{{ $tp("config.output_name_desc") }}</p>
      <PlatformLayout :platform="platform">
        <template #windows></template>
        <template #linux> </template>
        <template #macos> </template>
      </PlatformLayout>
    </div>
  </div>
  <div class="mb-3" v-if="platform === 'linux' || platform === 'macos'">
    <label for="output_name" class="form-label">{{
      $t("config.output_name_unix")
    }}</label>
    <input
      type="text"
      class="form-control"
      id="output_name"
      placeholder="0"
      v-model="config.output_name"
    />
    <div class="form-text">
      {{ $t("config.output_name_desc_unix") }}<br />
      <br />
      <pre style="white-space: pre-line" v-if="platform === 'linux'">
              Info: Detecting displays
              Info: Detected display: DVI-D-0 (id: 0) connected: false
              Info: Detected display: HDMI-0 (id: 1) connected: true
              Info: Detected display: DP-0 (id: 2) connected: true
              Info: Detected display: DP-1 (id: 3) connected: false
              Info: Detected display: DVI-D-1 (id: 4) connected: false
            </pre
      >
      <pre style="white-space: pre-line" v-if="platform === 'macos'">
              Info: Detecting displays
              Info: Detected display: Monitor-0 (id: 3) connected: true
              Info: Detected display: Monitor-1 (id: 2) connected: true
            </pre
      >
    </div>
  </div>
</template>
