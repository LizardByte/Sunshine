<script setup>
import { computed } from 'vue'
import {
  Info,
  TriangleAlert,
} from '@lucide/vue'
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

const defaultMoonlightPort = 47989

const { config, getOwnConfigOptions } = useConfigTab(props.config, OPTIONS)

defineExpose({ getOwnConfigOptions })
const effectivePort = computed(() => +config.value?.port ?? defaultMoonlightPort)

const ADDRESS_FAMILY_OPTIONS = [
  { value: 'ipv4', label: t('config.address_family_ipv4') },
  { value: 'both', label: t('config.address_family_both') },
]

const ORIGIN_WEB_UI_ALLOWED_OPTIONS = [
  { value: 'pc', label: t('config.origin_web_ui_allowed_pc') },
  { value: 'lan', label: t('config.origin_web_ui_allowed_lan') },
  { value: 'wan', label: t('config.origin_web_ui_allowed_wan') },
]

const LAN_ENCRYPTION_MODE_OPTIONS = [
  { value: '0', label: t('_common.disabled_def') },
  { value: '1', label: t('config.lan_encryption_mode_1') },
  { value: '2', label: t('config.lan_encryption_mode_2') },
]

const WAN_ENCRYPTION_MODE_OPTIONS = [
  { value: '0', label: t('_common.disabled') },
  { value: '1', label: t('config.wan_encryption_mode_1') },
  { value: '2', label: t('config.wan_encryption_mode_2') },
]
</script>

<script>
export const OPTIONS = {
  "upnp": "disabled",
  "address_family": "ipv4",
  "bind_address": "",
  "port": 47989,
  "origin_web_ui_allowed": "lan",
  "csrf_allowed_origins": "",
  "external_ip": "",
  "lan_encryption_mode": 0,
  "wan_encryption_mode": 1,
  "ping_timeout": 10000,
  "packetsize": 0,
}
</script>

<template>
  <div id="network" class="config-page">
    <!-- UPnP -->
    <Checkbox class="mb-3" id="upnp" locale-prefix="config" v-model="config.upnp" default="false"></Checkbox>

    <!-- Address family -->
    <Select id="address_family" v-model="config.address_family" :label="$t('config.address_family')"
      :desc="$t('config.address_family_desc')" :options="ADDRESS_FAMILY_OPTIONS" />

    <!-- Bind address -->
    <div class="mb-3">
      <label for="bind_address" class="form-label">{{ $t('config.bind_address') }}</label>
      <input type="text" class="form-control" id="bind_address" v-model="config.bind_address" />
      <div class="form-text">{{ $t('config.bind_address_desc') }}</div>
    </div>

    <!-- Port family -->
    <div class="mb-3">
      <label for="port" class="form-label">{{ $t('config.port') }}</label>
      <input type="number" min="1029" max="65514" class="form-control" id="port" :placeholder="defaultMoonlightPort"
        v-model="config.port" />
      <div class="form-text">{{ $t('config.port_desc') }}</div>
      <!-- Add warning if any port is less than 1024 -->
      <div class="alert alert-danger" v-if="(+effectivePort - 5) < 1024">
        <TriangleAlert :size="20" /> {{ $t('config.port_alert_1') }}
      </div>
      <!-- Add warning if any port is above 65535 -->
      <div class="alert alert-danger" v-if="(+effectivePort + 21) > 65535">
        <TriangleAlert :size="20" /> {{ $t('config.port_alert_2') }}
      </div>
      <!-- Create a port table for the various ports needed by Sunshine -->
      <table class="table">
        <thead>
          <tr>
            <th scope="col">{{ $t('config.port_protocol') }}</th>
            <th scope="col">{{ $t('config.port_port') }}</th>
            <th scope="col">{{ $t('config.port_note') }}</th>
          </tr>
        </thead>
        <tbody>
          <tr>
            <!-- HTTPS -->
            <td>{{ $t('config.port_tcp') }}</td>
            <td>{{+effectivePort - 5 }}</td>
            <td></td>
          </tr>
          <tr>
            <!-- HTTP -->
            <td>{{ $t('config.port_tcp') }}</td>
            <td>{{+effectivePort }}</td>
            <td>
              <div class="alert alert-primary" role="alert" v-if="+effectivePort !== defaultMoonlightPort">
                <Info :size="20" /> {{ $t('config.port_http_port_note') }}
              </div>
            </td>
          </tr>
          <tr>
            <!-- Web UI -->
            <td>{{ $t('config.port_tcp') }}</td>
            <td>{{+effectivePort + 1 }}</td>
            <td>{{ $t('config.port_web_ui') }}</td>
          </tr>
          <tr>
            <!-- RTSP -->
            <td>{{ $t('config.port_tcp') }}</td>
            <td>{{+effectivePort + 21 }}</td>
            <td></td>
          </tr>
          <tr>
            <!-- Video, Control, Audio -->
            <td>{{ $t('config.port_udp') }}</td>
            <td>{{+effectivePort + 9 }} - {{+effectivePort + 11 }}</td>
            <td></td>
          </tr>
          <!--            <tr>-->
          <!--              &lt;!&ndash; Mic &ndash;&gt;-->
          <!--              <td>UDP</td>-->
          <!--              <td>{{+effectivePort + 13}}</td>-->
          <!--              <td></td>-->
          <!--            </tr>-->
        </tbody>
      </table>
      <!-- add warning about exposing web ui to the internet -->
      <div class="alert alert-warning" v-if="config.origin_web_ui_allowed === 'wan'">
        <TriangleAlert :size="20" /> {{ $t('config.port_warning') }}
      </div>
    </div>

    <!-- Origin Web UI Allowed -->
    <Select id="origin_web_ui_allowed" v-model="config.origin_web_ui_allowed" :label="$t('config.origin_web_ui_allowed')"
      :desc="$t('config.origin_web_ui_allowed_desc')" :options="ORIGIN_WEB_UI_ALLOWED_OPTIONS" />

    <!-- CSRF Allowed Origins -->
    <div class="mb-3">
      <label for="csrf_allowed_origins" class="form-label">{{ $t('config.csrf_allowed_origins') }}</label>
      <input type="text" class="form-control" id="csrf_allowed_origins" v-model="config.csrf_allowed_origins" />
      <div class="form-text">{{ $t('config.csrf_allowed_origins_desc') }}</div>
    </div>

    <!-- External IP -->
    <div class="mb-3">
      <label for="external_ip" class="form-label">{{ $t('config.external_ip') }}</label>
      <input type="text" class="form-control" id="external_ip" placeholder="123.456.789.12"
        v-model="config.external_ip" />
      <div class="form-text">{{ $t('config.external_ip_desc') }}</div>
    </div>

    <!-- LAN Encryption Mode -->
    <Select id="lan_encryption_mode" v-model="config.lan_encryption_mode" :label="$t('config.lan_encryption_mode')"
      :desc="$t('config.lan_encryption_mode_desc')" :options="LAN_ENCRYPTION_MODE_OPTIONS" />

    <!-- WAN Encryption Mode -->
    <Select id="wan_encryption_mode" v-model="config.wan_encryption_mode" :label="$t('config.wan_encryption_mode')"
      :desc="$t('config.wan_encryption_mode_desc')" :options="WAN_ENCRYPTION_MODE_OPTIONS" />

    <!-- Ping Timeout -->
    <div class="mb-3">
      <label for="ping_timeout" class="form-label">{{ $t('config.ping_timeout') }}</label>
      <input type="text" class="form-control" id="ping_timeout" placeholder="10000" v-model="config.ping_timeout" />
      <div class="form-text">{{ $t('config.ping_timeout_desc') }}</div>
    </div>

    <!-- Packet Size Limit -->
    <div class="mb-3">
      <label for="packetsize" class="form-label">{{ $t('config.packetsize') }}</label>
      <input type="number" min="0" max="65535" class="form-control" id="packetsize" placeholder="0"
        v-model="config.packetsize" />
      <div class="form-text">{{ $t('config.packetsize_desc') }}</div>
    </div>

  </div>
</template>
