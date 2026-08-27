<template>
  <fieldset class="mb-3">
    <legend class="form-label h6">{{ label }}</legend>
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
  </fieldset>
</template>

<script setup>
import { computed, ref } from 'vue'
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

const props = defineProps({
  platform: String,
  label: String,
  description: String,
  addLabel: String,
})

const modelValue = defineModel({ type: Array, default: () => [] })

const browserOpen = ref(false)
const browserTargetIndex = ref(null)
const browserTargetField = ref(null)

const browserStartPath = computed(() => {
  if (browserTargetIndex.value === null) {
    return '';
  }
  return modelValue.value[browserTargetIndex.value][browserTargetField.value] || '';
})

function addCmd() {
  let template = {
    do: "",
    undo: "",
  };

  if (props.platform === 'windows') {
    template = { ...template, elevated: false };
  }
  modelValue.value = [...modelValue.value, template];
}

function removeCmd(index) {
  const updated = [...modelValue.value];
  updated.splice(index, 1);
  modelValue.value = updated;
}

function browse(index, field) {
  browserTargetIndex.value = index;
  browserTargetField.value = field;
  browserOpen.value = true;
}

function onBrowserConfirm(path) {
  const updated = [...modelValue.value];
  updated[browserTargetIndex.value] = { ...updated[browserTargetIndex.value], [browserTargetField.value]: path };
  modelValue.value = updated;
}
</script>
