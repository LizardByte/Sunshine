<template>
  <div class="my-4">
    <h1>{{ $t('apps.applications_title') }}<span v-if="apps.length"> ({{ appCountLabel }})</span></h1>
    <p>{{ $t('apps.applications_desc') }}</p>
  </div>

  <!-- Actions toolbar -->
  <div class="toolbar apps-toolbar d-flex flex-wrap justify-content-between align-items-center gap-2 mb-3">
    <!-- Left side actions -->
    <div class="d-flex align-items-center gap-2">
      <!-- Add new application -->
      <button type="button" class="btn btn-primary" @click="newApp">
        <layers-plus :size="18" class="icon"></layers-plus>
        {{ $t('apps.add_new') }}
      </button>
    </div>
    <!-- Right side actions -->
    <div class="d-flex align-items-center gap-2" v-if="apps && apps.length > 0">
      <!-- Sort by name toggle -->
      <button class="btn btn-outline-secondary text-nowrap" type="button" @click="toggleSort"
        :class="{ active: sortMode !== 'default' }" :aria-pressed="sortMode !== 'default'"
        :title="$t('apps.sort_by_name') + ': ' + sortModeLabel">
        <arrow-up-down v-if="sortMode === 'default'" :size="16" class="icon me-1"></arrow-up-down>
        <arrow-up v-else-if="sortMode === 'asc'" :size="16" class="icon me-1"></arrow-up>
        <arrow-down v-else :size="16" class="icon me-1"></arrow-down>
        {{ $t('apps.sort_by_name') }}
      </button>
      <!-- Search box -->
      <div class="input-group">
        <input type="text" class="form-control" v-model="searchQuery" :placeholder="$t('apps.search_placeholder')"
          :aria-label="$t('apps.search_placeholder')" />
        <button v-if="searchQuery" class="btn btn-outline-secondary" type="button" @click="resetSearchQuery"
          :aria-label="$t('_common.close')">
          <x :size="16" class="icon"></x>
        </button>
        <span v-else class="input-group-text">
          <search :size="16" class="icon"></search>
        </span>
      </div>
    </div>
  </div>

  <!-- Apps Grid -->
  <div class="row g-3" v-if="displayedApps.length > 0">
    <div class="col-12 col-sm-6 col-md-4 col-lg-3" v-for="{ app, index } in displayedApps" :key="index">
      <div class="card app-card h-100">
        <div class="app-poster-container">
          <img v-if="app['image-path']" :src="'/api/covers/' + index" class="app-poster" :alt="app.name"
            @error="handleImageError" />
          <div v-else class="app-poster-placeholder">
            <span class="app-initial">{{ app.name.charAt(0).toUpperCase() }}</span>
          </div>
          <div class="app-poster-overlay">
            <div v-if="app.cmd" class="app-overlay-row" :title="app.cmd">
              <terminal :size="14" class="icon me-1"></terminal>
              <span class="app-detail-text">{{ app.cmd }}</span>
            </div>
            <div v-if="app['working-dir']" class="app-overlay-row" :title="app['working-dir']">
              <folder :size="14" class="icon me-1"></folder>
              <span class="app-detail-text">{{ app['working-dir'] }}</span>
            </div>
            <div class="app-overlay-badges">
              <span v-if="app.elevated" class="badge app-flag-badge">{{ $t('apps.badge_admin') }}</span>
              <span v-if="app.detached && app.detached.length" class="badge app-flag-badge">{{ $t('apps.badge_detached')
              }}</span>
              <span v-if="app['prep-cmd'] && app['prep-cmd'].length" class="badge app-flag-badge">{{
                $t('apps.badge_app_prep') }}</span>
              <span v-if="app['exclude-global-prep-cmd']" class="badge app-flag-badge">{{
                $t('apps.badge_no_global_prep') }}</span>
              <span v-if="app['auto-detach']" class="badge app-flag-badge">{{ $t('apps.badge_auto_detach') }}</span>
            </div>
          </div>
        </div>
        <div class="card-body d-flex flex-column">
          <h5 class="card-title mb-3">{{ app.name }}</h5>
          <div class="mt-auto d-flex gap-2">
            <button type="button" class="btn btn-sm btn-primary flex-fill" @click="editApp(index)">
              <edit :size="16" class="icon"></edit>
              {{ $t('apps.edit') }}
            </button>
            <button type="button" class="btn btn-sm btn-danger" @click="showDeleteModal(index)">
              <trash-2 :size="16" class="icon"></trash-2>
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- No search results -->
  <div v-else-if="apps && apps.length > 0" class="card">
    <div class="card-body text-center py-5">
      <p class="text-muted">{{ $t('apps.no_search_results') }}</p>
    </div>
  </div>

  <!-- Empty State -->
  <div v-else class="card">
    <div class="card-body text-center py-5">
      <p class="text-muted">{{ $t('apps.no_applications') }}</p>
    </div>
  </div>

  <!-- Edit / Add Application modal -->
  <Modal v-model="editModalOpen" dialog-class="modal-xl modal-dialog-scrollable modal-fullscreen-lg-down"
    backdrop="static" :keyboard="false">
    <template #title>{{ editModalTitle }}</template>
    <template v-if="editForm">
      <!-- Application Name -->
      <FormGroup field-id="appName" :label="$t('apps.app_name')" :description="$t('apps.app_name_desc')">
        <input type="text" class="form-control" id="appName" v-model="editForm.name" />
      </FormGroup>
      <!-- output -->
      <FormGroup field-id="appOutput" :label="$t('apps.output_name')" :description="$t('apps.output_desc')">
        <div class="input-group">
          <input type="text" class="form-control monospace" id="appOutput" v-model="editForm.output" />
          <button class="btn btn-secondary" type="button"
            @click="openFileBrowser('any', 'file_browser.select_file', editForm.output, { field: 'output' })">
            <folder-open :size="18" class="icon"></folder-open>
          </button>
        </div>
      </FormGroup>
      <!-- prep-cmd -->
      <Checkbox class="mb-3" id="excludeGlobalPrep" label="apps.global_prep_name" desc="apps.global_prep_desc"
        v-model="editForm['exclude-global-prep-cmd']" default="true" inverse-values></Checkbox>
      <PrepCommandList v-model="editForm['prep-cmd']" :platform="platform" :label="$t('apps.cmd_prep_name')"
        :description="$t('apps.cmd_prep_desc')" :add-label="$t('apps.add_cmds')" />
      <!-- detached -->
      <div class="mb-3">
        <div class="form-label">{{ $t('apps.detached_cmds') }}</div>
        <div class="form-text">
          {{ $t('apps.detached_cmds_desc') }}<br>
          <b>{{ $t('_common.note') }}</b> {{ $t('apps.detached_cmds_note') }}
        </div>
        <div v-for="(_, index) in editForm.detached" :key="index" class="d-flex align-items-center gap-2 my-2">
          <input type="text" v-model="editForm.detached[index]" class="form-control monospace"
            :aria-label="$t('apps.detached_cmds')">
          <button type="button" class="btn btn-secondary btn-sm" @click="browseDetached(index)">
            <folder-open :size="14" class="icon"></folder-open>
          </button>
          <button type="button" class="btn btn-danger btn-sm" @click="editForm.detached.splice(index, 1)">
            <trash-2 :size="16" class="icon"></trash-2>
          </button>
        </div>
        <div class="d-flex justify-content-start mb-3 mt-3">
          <button type="button" class="btn btn-success" @click="addDetached">
            <plus :size="18" class="icon"></plus>
            {{ $t('apps.detached_cmds_add') }}
          </button>
        </div>
      </div>
      <!-- command -->
      <FormGroup field-id="appCmd" :label="$t('apps.cmd')">
        <div class="input-group">
          <input type="text" class="form-control monospace" id="appCmd" v-model="editForm.cmd" />
          <button class="btn btn-secondary" type="button"
            @click="openFileBrowser('executable', 'file_browser.select_executable', editForm.cmd, { field: 'cmd' })">
            <folder-open :size="18" class="icon"></folder-open>
          </button>
        </div>
        <div class="form-text">
          {{ $t('apps.cmd_desc') }}<br>
          <b>{{ $t('_common.note') }}</b> {{ $t('apps.cmd_note') }}
        </div>
      </FormGroup>
      <!-- working dir -->
      <FormGroup field-id="appWorkingDir" :label="$t('apps.working_dir')" :description="$t('apps.working_dir_desc')">
        <div class="input-group">
          <input type="text" class="form-control monospace" id="appWorkingDir" v-model="editForm['working-dir']" />
          <button class="btn btn-secondary" type="button"
            @click="openFileBrowser('directory', 'file_browser.select_directory', editForm['working-dir'], { field: 'working-dir' })">
            <folder-open :size="18" class="icon"></folder-open>
          </button>
        </div>
      </FormGroup>
      <!-- elevation -->
      <Checkbox v-if="platform === 'windows'" class="mb-3" id="appElevation" label="_common.run_as"
        desc="apps.run_as_desc" v-model="editForm.elevated" default="false"></Checkbox>
      <!-- auto-detach -->
      <Checkbox class="mb-3" id="autoDetach" label="apps.auto_detach" desc="apps.auto_detach_desc"
        v-model="editForm['auto-detach']" default="true"></Checkbox>
      <!-- wait for all processes -->
      <Checkbox class="mb-3" id="waitAll" label="apps.wait_all" desc="apps.wait_all_desc" v-model="editForm['wait-all']"
        default="true"></Checkbox>
      <!-- exit timeout -->
      <FormGroup field-id="exitTimeout" :label="$t('apps.exit_timeout')" :description="$t('apps.exit_timeout_desc')">
        <input type="number" class="form-control monospace" id="exitTimeout" v-model="editForm['exit-timeout']" min="0"
          placeholder="5" />
      </FormGroup>
      <FormGroup field-id="appImagePath" :label="$t('apps.image')" :description="$t('apps.image_desc')">
        <div class="input-group">
          <input type="text" class="form-control monospace" id="appImagePath" v-model="editForm['image-path']" />
          <button class="btn btn-secondary" type="button"
            @click="openFileBrowser('file', 'file_browser.select_file', editForm['image-path'], { field: 'image-path' })">
            <folder-open :size="18" class="icon"></folder-open>
          </button>
          <button class="btn btn-secondary" type="button" @click="showCoverFinder">
            <search :size="18" class="icon"></search>
            {{ $t('apps.find_cover') }}
          </button>
        </div>
      </FormGroup>
      <div class="env-hint alert alert-info">
        <div class="form-text">
          <h4>{{ $t('apps.env_vars_about') }}</h4>
          {{ $t('apps.env_vars_desc') }}
        </div>
        <table class="env-table">
          <thead>
            <tr>
              <th scope="col"><b>{{ $t('apps.env_var_name') }}</b></th>
              <th scope="col"><b></b></th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td style="font-family: monospace">SUNSHINE_APP_ID</td>
              <td>{{ $t('apps.env_app_id') }}</td>
            </tr>
            <tr>
              <td style="font-family: monospace">SUNSHINE_APP_NAME</td>
              <td>{{ $t('apps.env_app_name') }}</td>
            </tr>
            <tr>
              <td style="font-family: monospace">SUNSHINE_CLIENT_WIDTH</td>
              <td>{{ $t('apps.env_client_width') }}</td>
            </tr>
            <tr>
              <td style="font-family: monospace">SUNSHINE_CLIENT_HEIGHT</td>
              <td>{{ $t('apps.env_client_height') }}</td>
            </tr>
            <tr>
              <td style="font-family: monospace">SUNSHINE_CLIENT_FPS</td>
              <td>{{ $t('apps.env_client_fps') }}</td>
            </tr>
            <tr>
              <td style="font-family: monospace">SUNSHINE_CLIENT_HDR</td>
              <td>{{ $t('apps.env_client_hdr') }}</td>
            </tr>
            <tr>
              <td style="font-family: monospace">SUNSHINE_CLIENT_GCMAP</td>
              <td>{{ $t('apps.env_client_gcmap') }}</td>
            </tr>
            <tr>
              <td style="font-family: monospace">SUNSHINE_CLIENT_HOST_AUDIO</td>
              <td>{{ $t('apps.env_client_host_audio') }}</td>
            </tr>
            <tr>
              <td style="font-family: monospace">SUNSHINE_CLIENT_ENABLE_SOPS</td>
              <td>{{ $t('apps.env_client_enable_sops') }}</td>
            </tr>
            <tr>
              <td style="font-family: monospace">SUNSHINE_CLIENT_AUDIO_CONFIGURATION</td>
              <td>{{ $t('apps.env_client_audio_config') }}</td>
            </tr>
          </tbody>
        </table>
        <div class="form-text" v-if="platform === 'windows'"><b>{{ $t('apps.env_qres_example') }}</b>
          <pre>cmd /C &lt;{{ $t('apps.env_qres_path') }}&gt;\QRes.exe /X:%SUNSHINE_CLIENT_WIDTH% /Y:%SUNSHINE_CLIENT_HEIGHT% /R:%SUNSHINE_CLIENT_FPS%</pre>
        </div>
        <div class="form-text" v-else-if="platform === 'freebsd' || platform === 'linux'"><b>{{
          $t('apps.env_xrandr_example') }}</b>
          <pre>sh -c "xrandr --output HDMI-1 --mode \"${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}\" --rate ${SUNSHINE_CLIENT_FPS}"</pre>
        </div>
        <div class="form-text" v-else-if="platform === 'macos'"><b>{{ $t('apps.env_displayplacer_example') }}</b>
          <pre>sh -c "displayplacer "id:&lt;screenId&gt; res:${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT} hz:${SUNSHINE_CLIENT_FPS} scaling:on origin:(0,0) degree:0""</pre>
        </div>
        <div class="form-text"><a :href="`${documentationBaseUrl}/md_docs_2app__examples.html`" target="_blank">{{
          $t('_common.see_more') }}</a></div>
      </div>
    </template>
    <template #footer>
      <button type="button" @click="editModalOpen = false" class="btn btn-secondary">
        <x :size="18" class="icon"></x>
        {{ $t('_common.cancel') }}
      </button>
      <button type="button" class="btn btn-primary" @click="save">
        <save :size="18" class="icon"></save>
        {{ $t('_common.save') }}
      </button>
    </template>
  </Modal>

  <!-- Cover Finder modal -->
  <Modal v-model="coverFinderModalOpen" dialog-class="modal-xl modal-dialog-scrollable modal-fullscreen-md-down"
    stacked>
    <template #title>
      <span v-if="coverSearching">{{ $t('apps.searching_covers') }}</span>
      <span v-else-if="coverCandidates.length > 0">{{ $t('apps.covers_found') }} ({{ coverCandidates.length }})</span>
      <span v-else>{{ $t('apps.no_covers_found') }}</span>
    </template>
    <div class="mb-3">
      <div class="input-group">
        <input type="text" class="form-control" v-model="coverSearchQuery" :placeholder="editForm?.name"
          :aria-label="$t('_common.search')" @keyup.enter="performCoverSearch" />
        <button class="btn btn-primary" type="button" @click="performCoverSearch">
          <search :size="18" class="icon"></search>
          {{ $t('_common.search') }}
        </button>
      </div>
      <div class="form-text mt-2">
        <b>{{ $t('_common.note') }}</b> {{ $t('apps.cover_search_hint') }}
        <a href="https://www.igdb.com/" target="_blank" rel="noopener noreferrer">IGDB</a>
      </div>
    </div>
    <div class="cover-results" :class="{ busy: coverFinderBusy }">
      <div class="row">
        <div v-if="coverSearching" class="col-12 col-sm-6 col-lg-4 mb-3">
          <div class="cover-container">
            <LoadingSpinner :label="$t('apps.loading')" />
          </div>
        </div>
        <div v-for="cover in coverCandidates" :key="cover.url" class="col-12 col-sm-6 col-lg-3 mb-3"
          @click="useCover(cover)">
          <div class="cover-container result">
            <img class="rounded" :src="cover.url" :alt="cover.name" />
          </div>
          <div class="d-block text-nowrap text-center text-truncate">
            {{ cover.name }}
          </div>
        </div>
      </div>
    </div>
    <template #footer>
      <button type="button" class="btn btn-secondary" @click="coverFinderModalOpen = false">
        <x :size="18" class="icon"></x>
        {{ $t('_common.cancel') }}
      </button>
    </template>
  </Modal>

  <!-- Delete confirmation modal -->
  <Modal v-model="deleteModalOpen" dialog-class="modal-dialog-centered">
    <template #title>{{ $t('apps.delete_title') }}</template>
    <i18n-t v-if="deleteTarget" keypath="apps.delete_confirm" tag="span">
      <template #name><strong>{{ deleteTarget.name }}</strong></template>
    </i18n-t>
    <template #footer>
      <button type="button" class="btn btn-secondary" @click="deleteModalOpen = false">
        <x :size="18" class="icon"></x>
        {{ $t('_common.cancel') }}
      </button>
      <button type="button" class="btn btn-danger" @click="confirmDelete">
        <trash-2 :size="18" class="icon"></trash-2>
        {{ $t('apps.delete') }}
      </button>
    </template>
  </Modal>

  <!-- Shared file browser modal -->
  <FileBrowserModal v-model:open="fileBrowserOpen" :type="fileBrowserType" :title="fileBrowserTitle"
    :start-path="fileBrowserStartPath" @confirm="onFileBrowserConfirm" />
</template>

<script>
import { toRaw } from 'vue'
import Modal from '@/components/Modal.vue'
import FormGroup from '@/components/FormGroup.vue'
import LoadingSpinner from '@/components/LoadingSpinner.vue'
import Checkbox from '@/components/Checkbox.vue'
import FileBrowserModal from '@/components/FileBrowserModal.vue'
import PrepCommandList from '@/components/PrepCommandList.vue'
import { apiFetch } from '@/utils/fetch_utils'
import SunshineVersion from '@/utils/sunshine_version'
import {
  ArrowDown,
  ArrowUp,
  ArrowUpDown,
  Edit,
  Folder,
  FolderOpen,
  LayersPlus,
  Plus,
  Save,
  Search,
  Terminal,
  Trash2,
  X,
} from '@lucide/vue'

function getSearchBucket(name) {
  let bucket = name.substring(0, Math.min(name.length, 2)).toLowerCase().replaceAll(/[^a-z\d]/g, '');
  if (!bucket) {
    return '@';
  }
  return bucket;
}

export default {
  components: {
    Modal,
    FormGroup,
    LoadingSpinner,
    Checkbox,
    FileBrowserModal,
    PrepCommandList,
    ArrowDown,
    ArrowUp,
    ArrowUpDown,
    Edit,
    Folder,
    FolderOpen,
    LayersPlus,
    Plus,
    Save,
    Search,
    Terminal,
    Trash2,
    X,
  },
  data() {
    return {
      apps: [],
      editForm: null,
      coverSearching: false,
      coverFinderBusy: false,
      coverCandidates: [],
      coverSearchQuery: "",
      platform: "",
      searchQuery: "",
      sortMode: "default",
      deleteTarget: null,
      deleteModalOpen: false,
      editModalOpen: false,
      coverFinderModalOpen: false,
      fileBrowserOpen: false,
      fileBrowserType: "any",
      fileBrowserTitle: "",
      fileBrowserStartPath: "",
      fileBrowserTarget: null,
      version: null,
      githubVersion: null,
    };
  },
  computed: {
    installedVersionNotStable() {
      if (!this.githubVersion || !this.version) {
        return false;
      }
      return this.version.isGreater(this.githubVersion);
    },
    documentationBaseUrl() {
      const docsVersion = this.installedVersionNotStable ? 'master' : 'latest'
      return `https://docs.lizardbyte.dev/projects/sunshine/${docsVersion}`
    },
    displayedApps() {
      let list = this.apps.map((app, index) => ({ app, index }));

      const query = this.searchQuery.trim().toLowerCase();
      if (query) {
        list = list.filter(({ app }) =>
          (app.name || "").toLowerCase().includes(query)
        );
      }

      if (this.sortMode !== "default") {
        // Apps can be created without a name, so we compare name || ""
        list.sort((a, b) => {
          const result = (a.app.name || "").localeCompare(
            b.app.name || "", undefined, { sensitivity: "base" }
          );
          // localeCompare returns 0 if the strings are equal, a negative value if a < b, and a positive value if a > b
          return this.sortMode === "asc"
            ? result
            : -result;
        });
      }

      return list;
    },
    sortModeLabel() {
      switch (this.sortMode) {
        case "asc":
          return this.$t("apps.sort_ascending");
        case "desc":
          return this.$t("apps.sort_descending");
        default:
          return this.$t("apps.sort_default");
      }
    },
    appCountLabel() {
      const total = this.apps.length;
      const shown = this.displayedApps.length;
      return shown === total
        ? `${total}`
        : `${shown} / ${total}`;
    },
    editModalTitle() {
      if (!this.editForm) {
        return "";
      }
      const action = this.editForm.index === -1
        ? this.$t("apps.add_new")
        : this.$t("apps.edit");

      return this.editForm.name
        ? `${action}: ${this.editForm.name}`
        : action;
    },
  },
  created() {
    this.loadApps();

    fetch("./api/config")
      .then(r => r.json())
      .then(r => {
        this.platform = r.platform;
        this.version = new SunshineVersion(null, r.version);
      });

    fetch("https://api.github.com/repos/LizardByte/Sunshine/releases/latest")
      .then((r) => r.json())
      .then((r) => this.githubVersion = new SunshineVersion(r, null))
      .catch((e) => console.error(e));
  },
  methods: {
    newApp() {
      this.editForm = {
        name: "",
        output: "",
        cmd: "",
        index: -1,
        "exclude-global-prep-cmd": false,
        elevated: false,
        "auto-detach": true,
        "wait-all": true,
        "exit-timeout": 5,
        "prep-cmd": [],
        detached: [],
        "image-path": ""
      };
      this.editModalOpen = true;
    },
    editApp(id) {
      this.editForm = structuredClone(toRaw(this.apps[id]));
      this.editForm.index = id;
      if (this.editForm["prep-cmd"] === undefined)
        this.editForm["prep-cmd"] = [];
      if (this.editForm["detached"] === undefined)
        this.editForm["detached"] = [];
      if (this.editForm["exclude-global-prep-cmd"] === undefined)
        this.editForm["exclude-global-prep-cmd"] = false;
      if (this.editForm["elevated"] === undefined && this.platform === 'windows') {
        this.editForm["elevated"] = false;
      }
      if (this.editForm["auto-detach"] === undefined) {
        this.editForm["auto-detach"] = true;
      }
      if (this.editForm["wait-all"] === undefined) {
        this.editForm["wait-all"] = true;
      }
      if (this.editForm["exit-timeout"] === undefined) {
        this.editForm["exit-timeout"] = 5;
      }
      this.editModalOpen = true;
    },
    showDeleteModal(id) {
      this.deleteTarget = { index: id, name: this.apps[id].name };
      this.deleteModalOpen = true;
    },
    loadApps() {
      return fetch("./api/apps")
        .then((r) => r.json())
        .then((r) => {
          this.apps = r.apps;
        });
    },
    confirmDelete() {
      apiFetch("./api/apps/" + this.deleteTarget.index, {
        method: "DELETE",
        headers: {
          "Content-Type": "application/json"
        },
      }).then((r) => {
        if (r.status === 200) document.location.reload();
      });
    },
    addDetached() {
      this.editForm.detached.push("");
    },
    showCoverFinder() {
      // Reset search state
      this.coverCandidates = [];
      this.coverSearchQuery = "";

      this.coverFinderModalOpen = true;

      // Perform initial search with app name
      this.performCoverSearch();
    },
    performCoverSearch() {
      this.coverSearching = true;
      this.coverCandidates = [];

      // Use search query if provided, otherwise fall back to app name
      const searchTerm = this.coverSearchQuery.trim() || this.editForm["name"].toString();

      function searchCovers(name) {
        if (!name) {
          return Promise.resolve([]);
        }
        let searchName = name.replaceAll(/\s+/g, '.').toLowerCase();

        // Use raw.githubusercontent.com to avoid CORS issues as we migrate the CNAME
        let dbUrl = "https://raw.githubusercontent.com/LizardByte/GameDB/gh-pages";
        let bucket = getSearchBucket(name);
        return fetch(`${dbUrl}/buckets/${bucket}.json`).then(function (r) {
          if (!r.ok) throw new Error("Failed to search covers");
          return r.json();
        }).then(maps => Promise.all(Object.keys(maps).map(id => {
          let item = maps[id];
          if (item.name.replaceAll(/\s+/g, '.').toLowerCase().startsWith(searchName)) {
            return fetch(`${dbUrl}/games/${id}.json`).then(function (r) {
              return r.json();
            }).catch(() => null);
          }
          return null;
        }).filter(Boolean)))
          .then(results => results
            .filter(item => item && item.cover && item.cover.url)
            .map(game => {
              const thumb = game.cover.url;
              const dotIndex = thumb.lastIndexOf('.');
              const slashIndex = thumb.lastIndexOf('/');
              if (dotIndex < 0 || slashIndex < 0) {
                return null;
              }
              const slug = thumb.substring(slashIndex + 1, dotIndex);
              return {
                name: game.name,
                key: `igdb_${game.id}`,
                url: `https://images.igdb.com/igdb/image/upload/t_cover_big/${slug}.jpg`,
                saveUrl: `https://images.igdb.com/igdb/image/upload/t_cover_big_2x/${slug}.png`,
              }
            }).filter(Boolean));
      }

      searchCovers(searchTerm)
        .then(list => this.coverCandidates = list)
        .finally(() => this.coverSearching = false);
    },
    useCover(cover) {
      this.coverFinderBusy = true;
      apiFetch("./api/covers/upload", {
        method: "POST",
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          key: cover.key,
          url: cover.saveUrl,
        })
      }).then(r => {
        if (!r.ok) throw new Error("Failed to download covers");
        return r.json();
      }).then(body => {
        this.editForm["image-path"] = body.path;
        this.coverFinderModalOpen = false;
      })
        .finally(() => this.coverFinderBusy = false);
    },
    openFileBrowser(type, titleKey, startPath, target) {
      this.fileBrowserType = type;
      this.fileBrowserTitle = this.$t(titleKey);
      this.fileBrowserStartPath = startPath || '';
      this.fileBrowserTarget = target;
      this.fileBrowserOpen = true;
    },
    onFileBrowserConfirm(path) {
      const target = this.fileBrowserTarget;
      if (target.field === 'detached') {
        this.editForm.detached[target.index] = path;
      } else {
        this.editForm[target.field] = path;
      }
    },
    browseDetached(index) {
      const current = this.editForm.detached[index] || '';
      this.openFileBrowser('executable', 'file_browser.select_executable', current, { field: 'detached', index });
    },
    save() {
      this.editForm["image-path"] = this.editForm["image-path"].toString().replaceAll('"', '');
      apiFetch("./api/apps", {
        method: "POST",
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(this.editForm),
      }).then((r) => {
        if (r.status === 200) document.location.reload();
      });
    },
    handleImageError(event) {
      // Hide the broken image and show placeholder instead
      event.target.style.display = 'none';
      const placeholder = event.target.nextElementSibling;
      if (placeholder && placeholder.classList.contains('app-poster-placeholder')) {
        placeholder.style.display = 'flex';
      }
    },
    resetSearchQuery() {
      this.searchQuery = "";
    },
    toggleSort() {
      // Sorting goes default -> ascending -> descending -> default
      if (this.sortMode === "default")
        this.sortMode = "asc";
      else if (this.sortMode === "asc")
        this.sortMode = "desc";
      else
        this.sortMode = "default";
    },
  },
}
</script>
