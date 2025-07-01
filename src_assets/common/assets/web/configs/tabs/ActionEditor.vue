<template>
  <div class="action-editor">
    <form @submit.prevent="save">
      <!-- Action Name -->
      <div class="mb-3">
        <label for="actionName" class="form-label">{{ $t('event_actions.action_name') }}</label>
        <input 
          type="text" 
          class="form-control" 
          id="actionName" 
          v-model="localAction.name" 
          :placeholder="$t('event_actions.action_name_placeholder')"
          required
        />
      </div>

      <!-- Failure Policy -->
      <div class="mb-3">
        <label for="failurePolicy" class="form-label">{{ $t('event_actions.failure_policy') }}</label>
        <select class="form-select" id="failurePolicy" v-model="localAction.failure_policy">
          <option value="FAIL_FAST">{{ $t('event_actions.fail_fast') }}</option>
          <option value="CONTINUE_ON_FAILURE">{{ $t('event_actions.continue_on_failure') }}</option>
        </select>
        <div class="form-text">{{ $t('event_actions.failure_policy_desc') }}</div>
      </div>

      <!-- Do Commands Section -->
      <div class="mb-4">
        <h6>{{ $t('event_actions.do_commands') }}</h6>
        
        <!-- Do Stage Selection -->
        <div class="mb-3">
          <label for="doStage" class="form-label">{{ $t('event_actions.do_stage') }}</label>
          <select class="form-select" id="doStage" v-model="localAction.do_stage" @change="suggestUndoStage">
            <option value="">{{ $t('event_actions.select_stage') }}</option>
            <option v-for="stage in stagesOrder" :key="stage.stage" :value="stage.stage">
              {{ stage.name }} ({{ stage.stage }})
            </option>
          </select>
        </div>

        <!-- Do Commands List -->
        <div class="commands-section">
          <div v-for="(command, index) in localAction.do_commands" :key="'do-' + index" class="command-item compact d-flex align-items-center gap-2 mb-2 p-2 border rounded">
            <input
              type="text"
              class="form-control form-control-sm font-monospace flex-grow-1 me-1"
              :id="'doCmd' + index"
              v-model="command.cmd"
              :placeholder="$t('event_actions.command_placeholder')"
              required
              style="min-width: 120px;"
            />
            <div class="form-check form-check-inline m-0">
              <input
                class="form-check-input"
                type="checkbox"
                :id="'doElevated' + index"
                v-model="command.elevated"
              />
              <label class="form-check-label small" :for="'doElevated' + index">
                {{ $t('event_actions.elevated') }}
              </label>
            </div>
            <input
              type="number"
              class="form-control form-control-sm ms-1"
              :id="'doTimeout' + index"
              v-model="command.timeout_seconds"
              min="1"
              max="300"
              :placeholder="$t('event_actions.timeout')"
              style="width: 70px;"
            />
            <div class="form-check form-check-inline m-0">
              <input
                class="form-check-input"
                type="checkbox"
                :id="'doAsync' + index"
                v-model="command.async"
              />
              <label class="form-check-label small" :for="'doAsync' + index">
                {{ $t('event_actions.async') }}
              </label>
            </div>
            <div class="form-check form-check-inline m-0">
              <input
                class="form-check-input"
                type="checkbox"
                :id="'doIgnoreError' + index"
                v-model="command.ignore_error"
              />
              <label class="form-check-label small" :for="'doIgnoreError' + index">
                {{ $t('event_actions.ignore_error') }}
              </label>
            </div>
            <button type="button" class="btn btn-sm btn-outline-danger ms-1" @click="removeDoCommand(index)" title="{{$t('_common.remove')}}">
              <i class="fas fa-trash"></i>
            </button>
          </div>
          <button type="button" class="btn btn-sm btn-outline-primary mt-1" @click="addDoCommand">
            <i class="fas fa-plus"></i> {{ $t('event_actions.add_command') }}
          </button>
        </div>
      </div>

      <!-- Undo Commands Section -->
      <div class="mb-4">
        <h6>{{ $t('event_actions.undo_commands') }} <small class="text-muted">({{ $t('_common.optional') }})</small></h6>
        
        <!-- Undo Stage Selection -->
        <div class="mb-3">
          <label for="undoStage" class="form-label">{{ $t('event_actions.undo_stage') }}</label>
          <select class="form-select" id="undoStage" v-model="localAction.undo_stage">
            <option value="">{{ $t('event_actions.select_stage') }}</option>
            <option v-for="stage in stagesOrder" :key="stage.stage" :value="stage.stage">
              {{ stage.name }} ({{ stage.stage }})
            </option>
          </select>
          <div class="form-text">{{ $t('event_actions.undo_stage_desc') }}</div>
        </div>

        <!-- Undo Commands List -->
        <div class="commands-section" v-if="localAction.undo_stage">
          <div v-for="(command, index) in localAction.undo_commands" :key="'undo-' + index" class="command-item compact d-flex align-items-center gap-2 mb-2 p-2 border rounded bg-light">
            <input
              type="text"
              class="form-control form-control-sm font-monospace flex-grow-1 me-1"
              :id="'undoCmd' + index"
              v-model="command.cmd"
              :placeholder="$t('event_actions.undo_command_placeholder')"
              required
              style="min-width: 120px;"
            />
            <div class="form-check form-check-inline m-0">
              <input
                class="form-check-input"
                type="checkbox"
                :id="'undoElevated' + index"
                v-model="command.elevated"
              />
              <label class="form-check-label small" :for="'undoElevated' + index">
                {{ $t('event_actions.elevated') }}
              </label>
            </div>
            <input
              type="number"
              class="form-control form-control-sm ms-1"
              :id="'undoTimeout' + index"
              v-model="command.timeout_seconds"
              min="1"
              max="300"
              :placeholder="$t('event_actions.timeout')"
              style="width: 70px;"
            />
            <div class="form-check form-check-inline m-0">
              <input
                class="form-check-input"
                type="checkbox"
                :id="'undoAsync' + index"
                v-model="command.async"
              />
              <label class="form-check-label small" :for="'undoAsync' + index">
                {{ $t('event_actions.async') }}
              </label>
            </div>
            <div class="form-check form-check-inline m-0">
              <input
                class="form-check-input"
                type="checkbox"
                :id="'undoIgnoreError' + index"
                v-model="command.ignore_error"
                checked
              />
              <label class="form-check-label small" :for="'undoIgnoreError' + index">
                {{ $t('event_actions.ignore_error') }}
              </label>
            </div>
            <button type="button" class="btn btn-sm btn-outline-danger ms-1" @click="removeUndoCommand(index)" title="{{$t('_common.remove')}}">
              <i class="fas fa-trash"></i>
            </button>
          </div>
          <button type="button" class="btn btn-sm btn-outline-secondary mt-1" @click="addUndoCommand">
            <i class="fas fa-plus"></i> {{ $t('event_actions.add_undo_command') }}
          </button>
        </div>
      </div>

      <!-- Action Buttons -->
      <div class="d-flex gap-2">
        <button type="submit" class="btn btn-primary">
          <i class="fas fa-save"></i> {{ $t('_common.save') }}
        </button>
        <button type="button" class="btn btn-secondary" @click="cancel">
          {{ $t('_common.cancel') }}
        </button>
      </div>
    </form>
  </div>
</template>

<script setup>
import { ref, reactive, watch } from 'vue'

const props = defineProps({
  action: {
    type: Object,
    default: () => null
  },
  stagesOrder: {
    type: Array,
    default: () => []
  },
  stagePairs: {
    type: Object,
    default: () => ({})
  }
})

const emit = defineEmits(['save', 'cancel'])

// Local action state
const localAction = reactive({
  name: '',
  failure_policy: 'FAIL_FAST',
  do_stage: '',
  do_commands: [],
  undo_stage: '',
  undo_commands: []
})

// Initialize local action from props
if (props.action) {
  Object.assign(localAction, props.action)
} else {
  // Default for new action
  localAction.do_commands = [createEmptyCommand()]
}

function createEmptyCommand() {
  return {
    cmd: '',
    elevated: false,
    timeout_seconds: 30,
    ignore_error: false,
    async: false
  }
}

function addDoCommand() {
  localAction.do_commands.push(createEmptyCommand())
}

function removeDoCommand(index) {
  if (localAction.do_commands.length > 1) {
    localAction.do_commands.splice(index, 1)
  }
}

function addUndoCommand() {
  const undoCommand = createEmptyCommand()
  undoCommand.ignore_error = true // Default to true for undo commands
  localAction.undo_commands.push(undoCommand)
}

function removeUndoCommand(index) {
  localAction.undo_commands.splice(index, 1)
}

function suggestUndoStage() {
  if (localAction.do_stage && props.stagePairs[localAction.do_stage]) {
    localAction.undo_stage = props.stagePairs[localAction.do_stage]
  }
}

function save() {
  // Validate required fields
  if (!localAction.name.trim()) {
    alert('Please enter an action name')
    return
  }
  
  if (!localAction.do_stage) {
    alert('Please select a do stage')
    return
  }
  
  if (localAction.do_commands.length === 0 || !localAction.do_commands[0].cmd.trim()) {
    alert('Please enter at least one do command')
    return
  }
  
  // Filter out empty commands
  localAction.do_commands = localAction.do_commands.filter(cmd => cmd.cmd.trim())
  localAction.undo_commands = localAction.undo_commands.filter(cmd => cmd.cmd.trim())
  
  emit('save', { ...localAction })
}

function cancel() {
  emit('cancel')
}
</script>

<style scoped>
.action-editor {
  max-height: calc(90vh - 140px); /* Account for modal header and footer */
  overflow-y: auto;
}

.command-item {
  background-color: #f8f9fa;
  padding: 0.5rem 0.75rem !important;
  margin-bottom: 0.5rem !important;
  display: flex;
  align-items: center;
  gap: 0.5rem;
}
.command-item.bg-light {
  background-color: #e9ecef !important;
}
.command-item.compact input[type="text"],
.command-item.compact input[type="number"] {
  margin-bottom: 0 !important;
  padding-top: 0.15rem;
  padding-bottom: 0.15rem;
  font-size: 0.95em;
}
.command-item.compact .form-check-label {
  font-size: 0.85em;
  margin-left: 0.2em;
}
.command-item.compact .btn {
  padding: 0.15rem 0.5rem;
  font-size: 0.95em;
}
.font-monospace {
  font-family: 'Monaco', 'Menlo', 'Ubuntu Mono', monospace;
}
.form-check-label {
  font-size: 0.9em;
}

/* Commands sections with controlled scrolling */
.commands-section {
  max-height: 300px;
  overflow-y: auto;
  padding-right: 8px;
}
</style>
