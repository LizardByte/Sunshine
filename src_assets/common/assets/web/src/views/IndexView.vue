<template>
  <h1 class="my-4">{{ $t('index.welcome') }}</h1>
  <p>{{ $t('index.description') }}</p>

  <!-- Fatal Errors Alert -->
  <AlertBox v-if="fancyLogs.find(x => x.level === 'Fatal')" variant="danger" class="my-4"
    :action="{ to: { path: '/troubleshooting', hash: '#logs' }, icon: FileText, label: 'View Logs' }">
    <div v-html="$t('index.startup_errors')"></div>
    <template #body>
      <ul class="mb-3">
        <li v-for="v in fancyLogs.filter(x => x.level === 'Fatal')">{{ v.value }}</li>
      </ul>
    </template>
  </AlertBox>

  <!-- libvirtualhid Warning -->
  <AlertBox
    v-if="platform === 'windows' && controllerEnabled && virtualhid && (!virtualhid.installed || !virtualhid.version_compatible)"
    variant="warning" class="my-4"
    :action="{ to: { path: '/troubleshooting', hash: '#virtualhid' }, icon: Wrench, label: $t('index.fix_now') }">
    <div v-if="!virtualhid.installed">
      <p class="mb-1"><strong>{{ $t('index.virtualhid_not_installed_title') }}</strong></p>
      <p class="mb-0">{{ $t('index.virtualhid_not_installed_desc') }}</p>
    </div>
    <div v-else-if="!virtualhid.version_compatible">
      <p class="mb-1"><strong>{{ $t('index.virtualhid_outdated_title') }}</strong></p>
      <p class="mb-0">{{ $t('index.virtualhid_outdated_desc', {
        version: virtualhid.version, supported_versions:
          virtualhid.supported_versions
      }) }}</p>
    </div>
  </AlertBox>

  <AlertBox
    v-if="platform === 'windows' && controllerEnabled && virtualhid && vigembus && !virtualhid.installed && vigembus.installed"
    variant="warning" class="my-4">
    <p class="mb-1"><strong>{{ $t('index.virtualhid_missing_vigembus_installed_title') }}</strong></p>
    <p class="mb-0">{{ $t('index.virtualhid_missing_vigembus_installed_desc') }}</p>
  </AlertBox>

  <!-- Virtual HID Driver license warning -->
  <AlertBox v-if="platform === 'windows' && controllerEnabled && virtualhidLicense && !virtualhidLicense.licensed"
    variant="warning" class="my-4"
    :action="{ to: { path: '/troubleshooting', hash: '#virtualhid-license' }, icon: Wrench, label: $t('index.fix_now') }">
    <p class="mb-1"><strong>{{ $t('index.virtualhid_license_required_title') }}</strong></p>
    <p class="mb-0">{{ $t('index.virtualhid_license_required_desc') }}</p>
  </AlertBox>

  <!-- Version -->
  <div class="card my-4">
    <div class="card-body" v-if="version">
      <h2>Version {{ version.version }}</h2>

      <div v-if="loading" class="my-3">
        {{ $t('index.loading_latest') }}
      </div>

      <AlertBox v-if="buildVersionIsDirty" variant="success" compact class="my-3" :icon="Package">
        {{ $t('index.version_dirty') }} 🌇
      </AlertBox>

      <AlertBox v-if="installedVersionNotStable" variant="info" compact class="my-3">
        {{ $t('index.installed_version_not_stable') }}
      </AlertBox>
      <AlertBox
        v-else-if="(!preReleaseBuildAvailable || !notifyPreReleases) && !stableBuildAvailable && !buildVersionIsDirty"
        variant="success" compact class="my-3">
        {{ $t('index.version_latest') }}
      </AlertBox>

      <div v-for="release in releaseAnnouncements" :key="release.key" class="alert alert-warning my-3">
        <!-- header row -->
        <div class="d-flex align-items-center justify-content-between gap-3 flex-wrap mb-3">
          <div class="d-flex align-items-center gap-3 flex-wrap">
            <alert-circle :size="18" class="icon"></alert-circle>
            <span>{{ $t(release.labelKey) }}</span>
            <h5 class="mb-0">{{ release.version.release.name }}</h5>
          </div>
          <div class="d-flex align-items-center gap-2 flex-shrink-0">
            <a class="btn btn-success" :href="release.version.release.html_url" target="_blank">
              <download :size="18" class="icon"></download>
              {{ $t('index.download') }}
            </a>
          </div>
        </div>
        <div class="accordion release-notes-accordion">
          <div class="accordion-item">
            <h2 class="accordion-header">
              <button type="button" class="accordion-button collapsed release-notes-toggle" data-bs-toggle="collapse"
                :data-bs-target="`#release-notes-${release.key}`" aria-expanded="false"
                :aria-controls="`release-notes-${release.key}`">
                {{ $t('index.release_notes') }}
                <chevron-left :size="18" class="icon ms-auto"></chevron-left>
              </button>
            </h2>
            <!-- body row (full width, collapsible + scrollable) -->
            <div class="accordion-collapse collapse" :id="`release-notes-${release.key}`">
              <div class="accordion-body">
                <div class="markdown-body release-notes" v-html="convertMarkdownToHtml(release.version.release.body)">
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- Resources -->
  <div class="my-4">
    <ResourceCard :installed-version-not-stable="installedVersionNotStable"></ResourceCard>
  </div>
</template>

<script>
import { marked } from 'marked'
import AlertBox from '../components/AlertBox.vue'
import ResourceCard from '../components/ResourceCard.vue'
import SunshineVersion from '../utils/sunshine_version'
import {
  AlertCircle,
  ChevronLeft,
  FileText,
  Wrench,
  Package,
  Download
} from '@lucide/vue'

marked.setOptions({
  breaks: true,
  gfm: true,
  headerIds: true,
  mangle: false,
  sanitize: false
});

export default {
  components: {
    AlertBox,
    ResourceCard,
    AlertCircle,
    ChevronLeft,
    Download
  },
  data() {
    return {
      FileText,
      Wrench,
      Package,
      version: null,
      githubVersion: null,
      notifyPreReleases: false,
      preReleaseVersion: null,
      loading: true,
      logs: null,
      platform: "",
      controllerEnabled: false,
      virtualhid: null,
      virtualhidLicense: null,
      vigembus: null,
    }
  },
  async created() {
    try {
      let config = await fetch("./api/config").then((r) => r.json());
      this.notifyPreReleases = config.notify_pre_releases;
      this.platform = config.platform;
      this.controllerEnabled = config.controller !== "disabled";
      this.version = new SunshineVersion(null, config.version);
      this.githubVersion = new SunshineVersion(await fetch("https://api.github.com/repos/LizardByte/Sunshine/releases/latest").then((r) => r.json()), null);
      this.preReleaseVersion = new SunshineVersion((await fetch("https://api.github.com/repos/LizardByte/Sunshine/releases").then((r) => r.json())).find(release => release.prerelease), null);

      // Fetch virtual input driver status only on Windows when controller is enabled
      if (this.platform === 'windows' && this.controllerEnabled) {
        try {
          const virtualInputStatus = await fetch("./api/virtual-input/status").then((r) => r.json());
          this.virtualhid = virtualInputStatus.virtualhid;
          this.vigembus = virtualInputStatus.vigembus;
        } catch (e) {
          console.error("Failed to fetch virtual input driver status:", e);
        }
        try {
          this.virtualhidLicense = await fetch("./api/virtual-input/license").then((r) => r.json());
        } catch (e) {
          console.error("Failed to fetch Virtual HID Driver license status:", e);
        }
      }
    } catch (e) {
      console.error(e);
    }
    try {
      this.logs = (await fetch("./api/logs").then(r => r.text()))
    } catch (e) {
      console.error(e);
    }
    this.loading = false;
  },
  computed: {
    installedVersionNotStable() {
      if (!this.githubVersion || !this.version) {
        return false;
      }
      return this.version.isGreater(this.githubVersion);
    },
    stableBuildAvailable() {
      if (!this.githubVersion || !this.version) {
        return false;
      }
      return this.githubVersion.isGreater(this.version);
    },
    preReleaseBuildAvailable() {
      if (!this.preReleaseVersion || !this.githubVersion || !this.version) {
        return false;
      }
      return this.preReleaseVersion.isGreater(this.version) && this.preReleaseVersion.isGreater(this.githubVersion);
    },
    buildVersionIsDirty() {
      return this.version.version?.split(".").length === 5 &&
        this.version.version.indexOf("dirty") !== -1
    },
    releaseAnnouncements() {
      const list = [];
      if (this.notifyPreReleases && this.preReleaseBuildAvailable) {
        list.push({ key: 'pre-release', version: this.preReleaseVersion, labelKey: 'index.new_pre_release' });
      }
      if (this.stableBuildAvailable) {
        list.push({ key: 'stable', version: this.githubVersion, labelKey: 'index.new_stable' });
      }
      return list;
    },
    /** Parse the text errors, calculating the text, the timestamp and the level */
    fancyLogs() {
      if (!this.logs) return [];
      let regex = /(\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}]):\s/g;
      let rawLogLines = (this.logs.split(regex)).splice(1);
      let logLines = []
      for (let i = 0; i < rawLogLines.length; i += 2) {
        logLines.push({ timestamp: rawLogLines[i], level: rawLogLines[i + 1].split(":")[0], value: rawLogLines[i + 1] });
      }
      return logLines;
    }
  },
  methods: {
    convertMarkdownToHtml(markdown) {
      if (!markdown) return '';
      return marked.parse(markdown);
    }
  }
}
</script>
