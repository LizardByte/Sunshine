<template>
  <div class="mb-3">
    <label class="form-label">{{ label }}</label>
    <div class="form-text">{{ description }}</div>

    <!-- Column headers (only once there's at least one row) -->
    <div class="row g-2 align-items-center mx-0 my-2" v-if="modelValue.length > 0">
      <div class="col-12 col-md text-muted small text-uppercase fw-semibold ps-3 d-flex align-items-center">
        <play :size="14" class="icon me-1"></play>{{ $t('_common.do_cmd') }}
      </div>
      <div class="col-12 col-md text-muted small text-uppercase fw-semibold ps-3 d-flex align-items-center">
        <rotate-ccw :size="14" class="icon me-1"></rotate-ccw>{{ $t('_common.undo_cmd') }}
      </div>
      <div class="col-auto text-muted small text-uppercase fw-semibold d-flex align-items-center"
        v-if="platform === 'windows'">
        <shield :size="14" class="icon me-1"></shield>{{ $t('_common.run_as') }}
      </div>
      <div class="col-auto" style="height: 0;">
        <div class="invisible">
          <button type="button" class="btn btn-danger btn-sm" tabindex="-1" aria-hidden="true">
            <trash-2 :size="16" class="icon"></trash-2>
          </button>
        </div>
      </div>
    </div>

    <div v-for="(cmd, index) in modelValue" :key="index" class="row g-2 align-items-center mx-0 my-2">
      <div class="col-12 col-md">
        <div class="input-group">
          <span class="input-group-text">
            <play :size="14" class="icon"></play>
          </span>
          <input type="text" class="form-control monospace" v-model="cmd.do" :placeholder="$t('_common.do_cmd')"
            :aria-label="$t('_common.do_cmd')" />
          <button class="btn btn-secondary" type="button" @click="browse(index, 'do')">
            <folder-open :size="14" class="icon"></folder-open>
          </button>
        </div>
      </div>
      <div class="col-12 col-md">
        <div class="input-group">
          <span class="input-group-text"><rotate-ccw :size="14" class="icon"></rotate-ccw></span>
          <input type="text" class="form-control monospace" v-model="cmd.undo" :placeholder="$t('_common.undo_cmd')"
            :aria-label="$t('_common.undo_cmd')" />
          <button class="btn btn-secondary" type="button" @click="browse(index, 'undo')">
            <folder-open :size="14" class="icon"></folder-open>
          </button>
        </div>
      </div>
      <div class="col-auto" v-if="platform === 'windows'">
        <Checkbox :id="'prep-cmd-admin-' + index" label="_common.elevated" desc="" v-model="cmd.elevated"></Checkbox>
      </div>
      <div class="col-auto">
        <button type="button" class="btn btn-danger btn-sm" @click="removeCmd(index)">
          <trash-2 :size="16" class="icon"></trash-2>
        </button>
      </div>
    </div>

    <div class="d-flex justify-content-start my-3">
      <button type="button" class="btn btn-success" @click="addCmd">
        <plus :size="18" class="icon"></plus>
        {{ addLabel }}
      </button>
    </div>

    <FileBrowserModal v-model:open="browserOpen" type="executable" :title="$t('file_browser.select_executable')"
      :start-path="browserStartPath" @confirm="onBrowserConfirm" />
  </div>
</template>

<script>
import Checkbox from '@/components/Checkbox.vue'
import FileBrowserModal from '@/components/FileBrowserModal.vue'
import {
  FolderOpen,
  Play,
  Plus,
  RotateCcw,
  Shield,
  Trash2,
} from '@lucide/vue'

export default {
  components: {
    Checkbox,
    FileBrowserModal,
    FolderOpen,
    Play,
    Plus,
    RotateCcw,
    Shield,
    Trash2,
  },
  props: {
    modelValue: { type: Array, default: () => [] },
    platform: String,
    label: String,
    description: String,
    addLabel: String,
  },
  emits: ['update:modelValue'],
  data() {
    return {
      browserOpen: false,
      browserTargetIndex: null,
      browserTargetField: null,
    };
  },
  computed: {
    browserStartPath() {
      if (this.browserTargetIndex === null) {
        return '';
      }
      return this.modelValue[this.browserTargetIndex][this.browserTargetField] || '';
    },
  },
  methods: {
    addCmd() {
      let template = {
        do: "",
        undo: "",
      };

      if (this.platform === 'windows') {
        template = { ...template, elevated: false };
      }
      this.modelValue.push(template);
    },
    removeCmd(index) {
      this.modelValue.splice(index, 1);
    },
    browse(index, field) {
      this.browserTargetIndex = index;
      this.browserTargetField = field;
      this.browserOpen = true;
    },
    onBrowserConfirm(path) {
      this.modelValue[this.browserTargetIndex][this.browserTargetField] = path;
    },
  },
}
</script>
