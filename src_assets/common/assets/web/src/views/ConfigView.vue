<template>
  <div class="my-4">
    <h1>{{ $t('config.configuration') }}</h1>
    <p>{{ $t('config.configuration_desc') }}</p>
  </div>

  <!-- Search toolbar -->
  <div class="toolbar config-search-toolbar mb-3">
    <ConfigSearch :options="getAllConfigOptions" @select="onSearchSelect" />
  </div>

  <div class="form" v-if="config">
    <div class="config-layout">
      <!-- Sidebar navigation -->
      <nav class="config-nav">
        <ul class="nav config-nav-list">
          <li class="nav-item" v-for="tab in tabs.general" :key="tab.id">
            <a class="nav-link" :class="{ active: tab.id === currentTab }" href="#" @click="currentTab = tab.id">
              <component :is="tab.icon" :size="18" class="icon"></component>
              {{ tab.name }}
            </a>
          </li>
        </ul>
        <template v-if="tabs.encoders.length">
          <div class="config-nav-heading">{{ $t('config.encoders') }}</div>
          <ul class="nav config-nav-list">
            <li class="nav-item" v-for="tab in tabs.encoders" :key="tab.id">
              <a class="nav-link" :class="{ active: tab.id === currentTab }" href="#" @click="currentTab = tab.id">
                <component :is="tab.icon" :size="18" class="icon"></component>
                {{ tab.name }}
              </a>
            </li>
          </ul>
        </template>

        <div class="config-actions">
          <button type="button" class="btn btn-primary" @click="save">
            <save :size="18" class="icon"></save>
            {{ $t('_common.save') }}
          </button>
          <button type="button" class="btn btn-success" @click="apply" v-if="saved && !restarted">
            <check :size="18" class="icon"></check>
            {{ $t('_common.apply') }}
          </button>
        </div>
      </nav>

      <!-- Tab content -->
      <div class="config-content">
        <StatusAlert v-if="saved && !restarted" success :success-message="$t('config.apply_note')" alert-class="mb-4" />
        <StatusAlert v-if="restarted" success :success-message="$t('config.restart_note')" alert-class="mb-4" />

        <!-- General Tab -->
        <general ref="general" v-show="currentTab === 'general'" :config="config" :platform="platform"></general>

        <!-- Input Tab -->
        <inputs ref="input" v-show="currentTab === 'input'" :config="config" :platform="platform"></inputs>

        <!-- Audio/Video Tab -->
        <audio-video ref="av" v-show="currentTab === 'av'" :config="config" :platform="platform"></audio-video>

        <!-- Network Tab -->
        <network ref="network" v-show="currentTab === 'network'" :config="config" :platform="platform"></network>

        <!-- Files Tab -->
        <files ref="files" v-show="currentTab === 'files'" :config="config" :platform="platform"></files>

        <!-- Advanced Tab -->
        <advanced ref="advanced" v-show="currentTab === 'advanced'" :config="config" :platform="platform"></advanced>

        <container-encoders ref="containerEncoders" :current-tab="currentTab" :config="config"
          :platform="platform"></container-encoders>
      </div>
    </div>
  </div>
</template>

<script>
import { computed } from 'vue'
import ConfigSearch from '@/components/ConfigSearch.vue'
import General from '@/components/configs/tabs/General.vue'
import Inputs from '@/components/configs/tabs/Inputs.vue'
import Network from '@/components/configs/tabs/Network.vue'
import Files from '@/components/configs/tabs/Files.vue'
import Advanced from '@/components/configs/tabs/Advanced.vue'
import AudioVideo from '@/components/configs/tabs/AudioVideo.vue'
import ContainerEncoders from '@/components/configs/tabs/ContainerEncoders.vue'
import StatusAlert from '@/components/StatusAlert.vue'
import { apiFetch } from '@/utils/fetch_utils'
import {
  Check,
  Cpu,
  FileCog,
  Gamepad2,
  Gpu,
  Network as NetworkIcon,
  Save,
  Settings,
  Sliders,
  Volume2,
} from '@lucide/vue'

export default {
  components: {
    ConfigSearch,
    General,
    Inputs,
    Network,
    Files,
    Advanced,
    // They will be accessible via audio-video, container-encoders only.
    AudioVideo,
    ContainerEncoders,
    StatusAlert,
    // icons
    Check,
    Save,
  },
  data() {
    return {
      platform: "",
      saved: false,
      restarted: false,
      config: null,
      currentTab: "general",
      // True once config is fetched. Allows getAllConfigOptions to recompute after $refs exist.
      refsReady: false,
      tabs: {
        general: [
          { id: "general", name: "General", icon: Settings },
          { id: "input", name: "Input", icon: Gamepad2 },
          { id: "av", name: "Audio/Video", icon: Volume2 },
          { id: "network", name: "Network", icon: NetworkIcon },
          { id: "files", name: "Config Files", icon: FileCog },
          { id: "advanced", name: "Advanced", icon: Sliders },
        ],
        encoders: [
          { id: "nv", name: "NVIDIA NVENC Encoder", icon: Gpu, excludedIn: ["macos"] },
          { id: "qsv", name: "Intel QuickSync Encoder", icon: Gpu, excludedIn: ["freebsd", "linux", "macos"] },
          { id: "amd", name: "AMD AMF Encoder", icon: Gpu, excludedIn: ["freebsd", "linux", "macos"] },
          { id: "vt", name: "VideoToolbox Encoder", icon: Gpu, excludedIn: ["windows", "freebsd", "linux"] },
          { id: "vaapi", name: "VA-API Encoder", icon: Gpu, excludedIn: ["windows", "macos"] },
          { id: "vulkan", name: "Vulkan Encoder", icon: Gpu, excludedIn: ["windows", "macos"] },
          { id: "sw", name: "Software Encoder", icon: Cpu, excludedIn: [] },
        ],
      },
    };
  },
  provide() {
    return {
      platform: computed(() => this.platform),
    }
  },
  computed: {
    getAllConfigOptions() {
      const options = [];

      if (!this.refsReady) {
        return options;
      }

      const pushOptions = (tabOptions, tabName, tabId) => {
        Object.keys(tabOptions).forEach(key => {
          options.push({
            key: key,
            label: this.$t(`config.${key}`),
            tab: tabName,
            tabId: tabId
          });
        });
      };

      this.tabs.general.forEach(tab => {
        if (this.$refs[tab.id]) {
          pushOptions(this.$refs[tab.id].getOwnConfigOptions(), tab.name, tab.id);
        }
      });

      if (this.$refs.containerEncoders) {
        const encoderOptionsById = this.$refs.containerEncoders.getOptionsById();
        this.tabs.encoders.forEach(tab => {
          if (encoderOptionsById[tab.id]) {
            pushOptions(encoderOptionsById[tab.id], tab.name, tab.id);
          }
        });
      }

      return options;
    },
  },
  created() {
    fetch("./api/config")
      .then((r) => r.json())
      .then((r) => {
        this.config = r;
        this.platform = this.config.platform;

        this.tabs.encoders = this.tabs.encoders.filter(el => !el.excludedIn.includes(this.platform));

        // remove values we don't want in the config file
        delete this.config.platform;
        delete this.config.status;
        delete this.config.version;

        // Parse the special options before population if available
        ["dd_mode_remapping", "global_prep_cmd"].forEach(key => {
          if (this.config.hasOwnProperty(key)) {
            this.config[key] = JSON.parse(this.config[key]);
          }
        });

        // Wait for the resulting v-if="config" mount to actually land before
        // flipping this - that's what guarantees $refs are populated by then.
        this.$nextTick(() => {
          this.refsReady = true;
        });
      });
  },
  methods: {
    save() {
      this.saved = false;
      this.restarted = false;

      // collect each tab's current values via its own getOwnConfigOptions()
      let config = {};
      this.tabs.general.forEach(tab => {
        Object.assign(config, this.$refs[tab.id].getOwnConfigOptions());
      });
      Object.assign(config, this.$refs.containerEncoders.getOptions());

      return apiFetch("./api/config", {
        method: "POST",
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(config),
      }).then((r) => {
        if (r.status === 200) {
          this.saved = true
          return this.saved
        }
        else {
          return false
        }
      });
    },
    apply() {
      this.saved = this.restarted = false;
      let saved = this.save();

      saved.then((result) => {
        if (result === true) {
          this.restarted = true;
          setTimeout(() => {
            this.saved = this.restarted = false;
          }, 5000);
          apiFetch("./api/restart", {
            method: "POST",
            headers: {
              "Content-Type": "application/json"
            }
          });
        }
      });
    },
    onSearchSelect(option) {
      // Clear any previous highlight
      document.querySelectorAll('.config-search-highlight').forEach(el => {
        el.classList.remove('config-search-highlight');
      });

      if (option.tabId !== this.currentTab) {
        this.currentTab = option.tabId;
      }

      // Wait for the tab to render before locating the element
      this.$nextTick(() => {
        const element = document.getElementById(option.key);
        const container = element ? element.closest('.mb-3') : null;
        if (!container) {
          return;
        }

        container.scrollIntoView({ behavior: 'smooth', block: 'center' });
        container.classList.add('config-search-highlight');
        setTimeout(() => {
          container.classList.remove('config-search-highlight');
        }, 3000);
      });
    },
  },
}
</script>
