<template>
  <Modal :model-value="open" @update:model-value="$emit('update:open', $event)"
    dialog-class="modal-lg modal-dialog-scrollable modal-fullscreen-md-down" stacked>
    <template #title>{{ title || $t('file_browser.title') }}</template>
    <!-- Path input -->
    <div class="input-group mb-2">
      <input type="text" class="form-control monospace" v-model="typedPath" @input="onTypedInput"
        :aria-label="$t('file_browser.path')" @keyup.enter="navigate(typedPath)" />
      <button class="btn btn-secondary" type="button" @click="navigate(typedPath)">
        <arrow-right :size="16" class="icon"></arrow-right>
      </button>
    </div>
    <!-- Up button -->
    <div class="mb-2">
      <button class="btn btn-sm btn-outline-secondary" type="button" :disabled="loading || parentPath === currentPath"
        @click="navigateUp">
        <folder-up :size="16" class="icon me-1"></folder-up>
        {{ $t('file_browser.up') }}
      </button>
    </div>
    <!-- Error -->
    <div v-if="error" class="alert alert-danger py-2 small">{{ error }}</div>
    <!-- Loading -->
    <div v-if="loading" class="text-center py-3">
      <LoadingSpinner :label="$t('_common.loading')" small />
    </div>
    <!-- Entries -->
    <div v-else class="list-group" style="max-height: 400px; overflow-y: auto;">
      <div v-if="entries.length === 0" class="list-group-item text-muted text-center">
        {{ $t('file_browser.empty') }}
      </div>
      <button v-for="entry in entries" :key="entry.path" type="button"
        class="list-group-item list-group-item-action d-flex align-items-center py-1"
        :class="{ active: selectedPath === entry.path }" @click="selectEntry(entry)"
        @dblclick="activateEntry(entry)">
        <hard-drive v-if="!currentPath && entry.type === 'directory'" :size="16"
          class="icon me-2 flex-shrink-0"></hard-drive>
        <folder v-else-if="entry.type === 'directory'" :size="16" class="icon me-2 flex-shrink-0 text-warning"></folder>
        <file-text v-else :size="16" class="icon me-2 flex-shrink-0"></file-text>
        <span class="text-truncate">{{ entry.name }}</span>
      </button>
    </div>
    <template #footer>
      <div class="flex-grow-1 text-muted small text-truncate" v-if="selectedPath">
        <code>{{ selectedPath }}</code>
      </div>
      <button type="button" class="btn btn-secondary" @click="$emit('update:open', false)">
        <x :size="16" class="icon me-1"></x>
        {{ $t('_common.cancel') }}
      </button>
      <button type="button" class="btn btn-primary" @click="confirm" :disabled="!selectedPath && !typedPath">
        <check :size="16" class="icon me-1"></check>
        {{ $t('file_browser.select') }}
      </button>
    </template>
  </Modal>
</template>

<script>
import Modal from '@/components/Modal.vue'
import LoadingSpinner from '@/components/LoadingSpinner.vue'
import {
  ArrowRight,
  Check,
  FileText,
  Folder,
  FolderUp,
  HardDrive,
  X,
} from '@lucide/vue'

export default {
  components: {
    Modal,
    LoadingSpinner,
    ArrowRight,
    Check,
    FileText,
    Folder,
    FolderUp,
    HardDrive,
    X,
  },
  props: {
    open: { type: Boolean, default: false },
    type: { type: String, default: 'any' },
    title: { type: String, default: '' },
    startPath: { type: String, default: '' },
  },
  emits: ['update:open', 'confirm'],
  data() {
    return {
      currentPath: "",
      parentPath: "",
      entries: [],
      loading: false,
      error: "",
      selectedPath: "",
      typedPath: "",
    };
  },
  watch: {
    open(isOpen) {
      if (isOpen) {
        this.error = '';
        this.selectedPath = this.startPath || '';
        this.typedPath = this.startPath || '';
        this.navigate(this.startPath || '');
      }
    },
  },
  methods: {
    confirm() {
      const path = this.selectedPath || this.typedPath;
      if (path) {
        this.$emit('confirm', path);
        this.$emit('update:open', false);
      }
    },
    navigate(path) {
      this.loading = true;
      this.error = '';
      const params = new URLSearchParams({ type: this.type });
      if (path) params.set('path', path);
      fetch(`./api/browse?${params.toString()}`)
        .then(r => r.ok ? r.json() : r.json().then(e => { throw new Error(e.error || 'Browse failed'); }))
        .then(data => {
          this.currentPath = data.path ?? '';
          this.parentPath = data.parent ?? '';
          this.entries = data.entries ?? [];
          this.typedPath = data.path ?? '';
          this.selectedPath = this.type === 'directory' ? (data.path ?? '') : '';
        })
        .catch(err => { this.error = err.message; })
        .finally(() => { this.loading = false; });
    },
    navigateUp() {
      this.navigate(this.parentPath);
    },
    selectEntry(entry) {
      if (entry.type === 'directory') {
        this.navigate(entry.path);
      } else {
        this.selectedPath = entry.path;
        this.typedPath = entry.path;
      }
    },
    activateEntry(entry) {
      if (entry.type === 'directory') {
        this.navigate(entry.path);
      } else {
        this.selectedPath = entry.path;
        this.typedPath = entry.path;
        this.confirm();
      }
    },
    onTypedInput() {
      this.selectedPath = this.typedPath;
    },
  },
}
</script>
