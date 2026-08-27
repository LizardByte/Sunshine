<script setup>
import { useConfigTab } from '@/composables/useConfigTab'
import { $tp } from '@/utils/platform-i18n'
import PlatformLayout from '@/components/PlatformLayout.vue'
import AdapterNameSelector from '@/components/configs/tabs/audiovideo/AdapterNameSelector.vue'
import DisplayOutputSelector from '@/components/configs/tabs/audiovideo/DisplayOutputSelector.vue'
import DisplayDeviceOptions from "@/components/configs/tabs/audiovideo/DisplayDeviceOptions.vue";
import DisplayModesSettings from "@/components/configs/tabs/audiovideo/DisplayModesSettings.vue";
import Checkbox from "@/components/Checkbox.vue";

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
  "audio_sink": "",
  "virtual_sink": "",
  "stream_audio": "enabled",
  "install_steam_audio_drivers": "enabled",
  "adapter_name": "",
  "output_name": "",
  "dd_configuration_option": "disabled",
  "dd_resolution_option": "auto",
  "dd_manual_resolution": "",
  "dd_refresh_rate_option": "auto",
  "dd_manual_refresh_rate": "",
  "dd_hdr_option": "auto",
  "dd_wa_hdr_toggle_delay": 0,
  "dd_config_revert_delay": 3000,
  "dd_config_revert_on_disconnect": "disabled",
  "dd_mode_remapping": { "mixed": [], "resolution_only": [], "refresh_rate_only": [] },
  "max_bitrate": 0,
  "minimum_fps_target": 0
}
</script>

<template>
  <div id="audio-video" class="config-page">
    <!-- Audio Sink -->
    <div class="mb-3">
      <label for="audio_sink" class="form-label">{{ $t('config.audio_sink') }}</label>
      <input type="text" class="form-control" id="audio_sink"
        :placeholder="$tp('config.audio_sink_placeholder', 'alsa_output.pci-0000_09_00.3.analog-stereo')"
        v-model="config.audio_sink" />
      <div class="form-text">
        {{ $tp('config.audio_sink_desc') }}<br>
        <PlatformLayout>
          <template #windows>
            <pre>tools\audio-info.exe</pre>
          </template>
          <template #freebsd>
            <pre>pacmd list-sinks | grep "name:"</pre>
            <pre>pactl info | grep Source</pre>
          </template>
          <template #linux>
            <pre>pacmd list-sinks | grep "name:"</pre>
            <pre>pactl info | grep Source</pre>
          </template>
          <template #macos>
            <a href="https://github.com/mattingalls/Soundflower" target="_blank">Soundflower</a><br>
            <a href="https://github.com/ExistentialAudio/BlackHole" target="_blank">BlackHole</a>.
          </template>
        </PlatformLayout>
      </div>
    </div>


    <PlatformLayout>
      <template #windows>
        <!-- Virtual Sink -->
        <div class="mb-3">
          <label for="virtual_sink" class="form-label">{{ $t('config.virtual_sink') }}</label>
          <input type="text" class="form-control" id="virtual_sink" :placeholder="$t('config.virtual_sink_placeholder')"
            v-model="config.virtual_sink" />
          <div class="form-text">{{ $t('config.virtual_sink_desc') }}</div>
        </div>

        <!-- Install Steam Audio Drivers -->
        <Checkbox class="mb-3" id="install_steam_audio_drivers" locale-prefix="config"
          v-model="config.install_steam_audio_drivers" default="true"></Checkbox>
      </template>
    </PlatformLayout>

    <!-- Disable Audio -->
    <Checkbox class="mb-3" id="stream_audio" locale-prefix="config" v-model="config.stream_audio" default="true">
    </Checkbox>

    <AdapterNameSelector :platform="platform" :config="config" />

    <DisplayOutputSelector :platform="platform" :config="config" />

    <DisplayDeviceOptions :platform="platform" :config="config" />

    <!-- Display Modes -->
    <DisplayModesSettings :platform="platform" :config="config" />

  </div>
</template>
