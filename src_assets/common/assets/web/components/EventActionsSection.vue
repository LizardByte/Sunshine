
<template>
  <div class="event-actions-section">
    <!-- Section Header -->
    <div class="section-header mb-3">
      <h5 class="section-title mb-1">{{ displayTitle }}</h5>
      <div class="section-description">{{ displayDescription }}</div>
    </div>

    <!-- View Tabs -->
    <div class="view-tabs mb-3" v-if="commandsList.length > 0">
      <ul class="nav nav-tabs" role="tablist">
        <li class="nav-item" role="presentation">
          <button class="nav-link" 
                  :class="{ active: currentView === 'stage' }"
                  @click="currentView = 'stage'"
                  type="button" role="tab">
            <i class="fas fa-layer-group me-2"></i>By Stage
          </button>
        </li>
        <li class="nav-item" role="presentation">
          <button class="nav-link" 
                  :class="{ active: currentView === 'chronological' }"
                  @click="currentView = 'chronological'"
                  type="button" role="tab">
            <i class="fas fa-clock me-2"></i>Chronological Order
          </button>
        </li>
      </ul>
    </div>

    <!-- Commands List -->
    <div class="commands-list-container">
      <!-- Empty State -->
      <div v-if="commandsList.length === 0" class="empty-state text-center py-5">
        <div class="empty-state-icon mb-3">
          <i class="fas fa-terminal fa-3x text-muted"></i>
        </div>
        <h6 class="text-muted mb-2">{{ $t('event_actions.no_commands') || 'No commands configured' }}</h6>
        <p class="text-muted small mb-4">{{ $t('event_actions.add_command_help') || 'Add your first command to get started' }}</p>
        <button class="btn btn-primary" @click="openCommandModal()">
          <i class="fas fa-plus me-2"></i>{{ $t('event_actions.add_first_command') || 'Add First Command' }}
        </button>
      </div>

      <!-- Commands Table -->
      <div v-else class="commands-table">
        <div class="table-header">
          <div class="d-flex justify-content-between align-items-center mb-3">
            <div class="commands-count-info">
              <span class="text-muted">{{ commandsList.length }} {{ commandsList.length === 1 ? 'command' : 'commands' }}</span>
              <span v-if="currentView === 'chronological'" class="text-muted ms-2">
                <i class="fas fa-info-circle me-1"></i>Showing execution order during stream lifecycle
              </span>
              <span v-if="currentView === 'stage'" class="text-muted ms-2">
                <i class="fas fa-info-circle me-1"></i>Commands grouped by stage, numbered within each group
              </span>
            </div>
            <button class="btn btn-primary btn-sm" @click="openCommandModal()">
              <i class="fas fa-plus me-1"></i>{{ $t('event_actions.add_command') || 'New Command' }}
            </button>
          </div>
        </div>

        <!-- Stage View -->
        <div v-if="currentView === 'stage'" class="commands-list stage-view">
          <div v-for="(group, groupIndex) in groupedStages" :key="`stage-group-${group.stage.id}`" 
               class="stage-group">
            <!-- Stage Header -->
            <div class="section-divider" v-if="groupIndex > 0"></div>
            <div class="stage-group-header">
              <div class="stage-header-badge" :class="getStagePhaseClass(group.stage.id)">
                <i class="fas fa-layer-group me-2"></i>{{ group.stage.name }}
              </div>
              <div class="stage-description">{{ group.stage.description }}</div>
              <div class="stage-command-count">{{ group.commands.length }} {{ group.commands.length === 1 ? 'command' : 'commands' }}</div>
            </div>
            
            <!-- Commands in this stage -->
            <div v-for="(command, commandIndex) in group.commands" :key="`stage-${group.stage.id}-${command.originalIndex}`" 
                 class="command-row stage-command">
              <div class="command-card">
                <div class="command-content">
                  <div class="command-main">
                    <div class="command-number">
                      <span class="badge" :class="getChronologicalBadgeClass(command.stage)">
                        {{ commandIndex + 1 }}
                      </span>
                    </div>
                    <div class="command-details flex-grow-1">
                      <div class="command-text">
                        <code class="command-code">{{ command.cmd || 'Empty command' }}</code>
                      </div>
                      <div class="command-meta">
                        <span v-if="hasPairCommand(command)" class="meta-tag paired">
                          <i class="fas fa-link me-1"></i>Paired
                        </span>
                        <span v-if="command.elevated && platform === 'windows'" class="meta-tag elevated">
                          <i class="fas fa-shield-alt me-1"></i>Elevated
                        </span>
                        <span v-if="command.async" class="meta-tag async">
                          <i class="fas fa-rocket me-1"></i>Async
                        </span>
                        <span v-if="command.ignore_error" class="meta-tag ignore-error">
                          <i class="fas fa-exclamation-triangle me-1"></i>Ignore Errors
                        </span>
                        <span v-if="command.timeout_seconds && command.timeout_seconds !== 30" class="meta-tag timeout">
                          <i class="fas fa-clock me-1"></i>{{ command.timeout_seconds }}s
                        </span>
                      </div>
                    </div>
                  </div>
                  <div class="command-actions">
                    <div class="btn-group" role="group">
                      <button class="btn btn-sm btn-outline-secondary" 
                              @click="openCommandModal(command, command.originalIndex)"
                              :title="'Edit command'">
                        <i class="fas fa-edit"></i>
                      </button>
                      <button v-if="commandsList.length > 1" 
                              class="btn btn-sm btn-outline-secondary" 
                              @click="moveCommand(command.originalIndex, -1)"
                              :disabled="command.originalIndex === 0"
                              :title="'Move up'">
                        <i class="fas fa-chevron-up"></i>
                      </button>
                      <button v-if="commandsList.length > 1" 
                              class="btn btn-sm btn-outline-secondary" 
                              @click="moveCommand(command.originalIndex, 1)"
                              :disabled="command.originalIndex === commandsList.length - 1"
                              :title="'Move down'">
                        <i class="fas fa-chevron-down"></i>
                      </button>
                      <button class="btn btn-sm btn-outline-danger" 
                              @click="removeCommand(command.originalIndex)"
                              :title="'Delete command'">
                        <i class="fas fa-trash"></i>
                      </button>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <!-- Chronological View -->
        <div v-else-if="currentView === 'chronological'" class="commands-list chronological-view">
          <div v-for="(section, sectionIndex) in chronologicalSections" :key="`section-${sectionIndex}`" 
               class="chronological-section">
            <!-- Section Header -->
            <div class="section-divider" v-if="sectionIndex > 0"></div>
            <div class="chronological-section-header">
              <div class="section-phase-badge" :class="section.phase">
                <i :class="section.icon" class="me-2"></i>{{ section.title }}
              </div>
              <div class="section-description">{{ section.description }}</div>
            </div>
            
            <!-- Commands in this section -->
            <div v-for="command in section.commands" :key="`chrono-${command.chronoIndex}`" 
                 class="command-row chronological-command">
              <div class="command-card">
                <div class="command-content">
                  <div class="command-main">
                    <div class="command-number">
                      <span class="badge" :class="getChronologicalBadgeClass(command.stage)">
                        {{ command.chronoIndex + 1 }}
                      </span>
                    </div>
                    <div class="command-details flex-grow-1">
                      <div class="command-text">
                        <code class="command-code">{{ command.cmd || 'Empty command' }}</code>
                      </div>
                      <div class="command-meta">
                        <span v-if="hasPairCommand(command)" class="meta-tag paired">
                          <i class="fas fa-link me-1"></i>Paired
                        </span>
                        <span v-if="command.elevated && platform === 'windows'" class="meta-tag elevated">
                          <i class="fas fa-shield-alt me-1"></i>Elevated
                        </span>
                        <span v-if="command.async" class="meta-tag async">
                          <i class="fas fa-rocket me-1"></i>Async
                        </span>
                        <span v-if="command.ignore_error" class="meta-tag ignore-error">
                          <i class="fas fa-exclamation-triangle me-1"></i>Ignore Errors
                        </span>
                        <span v-if="command.timeout_seconds && command.timeout_seconds !== 30" class="meta-tag timeout">
                          <i class="fas fa-clock me-1"></i>{{ command.timeout_seconds }}s
                        </span>
                        <span class="meta-tag stage-chrono" :class="getStagePhaseClass(command.stage)">
                          <i class="fas fa-layer-group me-1"></i>{{ getStageDisplayName(command.stage) }}
                        </span>
                      </div>
                    </div>
                  </div>
                  <div class="command-actions">
                    <div class="btn-group" role="group">
                      <button class="btn btn-sm btn-outline-secondary" 
                              @click="openCommandModal(command, command.originalIndex)"
                              :title="'Edit command'">
                        <i class="fas fa-edit"></i>
                      </button>
                      <button class="btn btn-sm btn-outline-danger" 
                              @click="removeCommand(command.originalIndex)"
                              :title="'Delete command'">
                        <i class="fas fa-trash"></i>
                      </button>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Command Edit Modal -->
    <div v-if="showCommandModal" class="modal d-block" tabindex="-1" style="background-color: rgba(0,0,0,0.5);">
      <div class="modal-dialog modal-lg">
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title">
              <i class="fas fa-terminal me-2"></i>
              {{ editingCommand ? 'Edit Command' : 'New Command' }}
            </h5>
            <button type="button" class="btn-close" @click="closeCommandModal"></button>
          </div>
          <div class="modal-body">
            <form @submit.prevent="saveCommand">
              <!-- Command Input -->
              <div class="mb-4">
                <label class="form-label fw-bold">
                  <i class="fas fa-terminal me-2"></i>Command
                </label>
                <textarea v-model="modalCommand.cmd"
                         class="form-control command-input"
                         rows="3"
                         :placeholder="getCommandPlaceholder()"
                         required
                         ref="commandInput"></textarea>
                <div class="form-text">Enter the command to execute</div>
              </div>

              <!-- Stage Selection -->
              <div class="mb-4">
                <label class="form-label fw-bold">
                  <i class="fas fa-clock me-2"></i>Execution Stage
                </label>
                <select v-model="modalCommand.stage" class="form-select" required>
                  <option value="" disabled>Select when to run this command</option>
                  <optgroup label="Startup Commands">
                    <option v-for="stage in startStages" :key="stage.id" :value="stage.id">
                      {{ stage.name }}
                    </option>
                  </optgroup>
                  <optgroup label="Cleanup Commands">
                    <option v-for="stage in cleanupStages" :key="stage.id" :value="stage.id">
                      {{ stage.name }}
                    </option>
                  </optgroup>
                </select>
                <div class="form-text" v-if="modalCommand.stage">
                  {{ getStageDescription(modalCommand.stage) }}
                </div>
              </div>

              <!-- Options -->
              <div class="mb-4">
                <label class="form-label fw-bold">
                  <i class="fas fa-cog me-2"></i>Options
                </label>
                
                <!-- Timeout -->
                <div class="row mb-3">
                  <div class="col-md-6">
                    <label class="form-label">Timeout</label>
                    <div class="input-group">
                      <input v-model.number="modalCommand.timeout_seconds" 
                             type="number" 
                             class="form-control" 
                             min="1" 
                             max="3600" 
                             placeholder="30" />
                      <span class="input-group-text">seconds</span>
                    </div>
                  </div>
                </div>

                <!-- Checkboxes -->
                <div class="options-grid">
                  <div v-if="platform === 'windows'" class="form-check">
                    <input v-model="modalCommand.elevated" 
                           class="form-check-input" 
                           type="checkbox" 
                           id="modal-elevated">
                    <label class="form-check-label" for="modal-elevated">
                      <i class="fas fa-shield-alt me-1"></i>Run as administrator
                    </label>
                  </div>
                  <div class="form-check">
                    <input v-model="modalCommand.ignore_error" 
                           class="form-check-input" 
                           type="checkbox" 
                           id="modal-ignore-error">
                    <label class="form-check-label" for="modal-ignore-error">
                      <i class="fas fa-exclamation-triangle me-1"></i>Ignore errors
                    </label>
                  </div>
                  <div class="form-check">
                    <input v-model="modalCommand.async" 
                           class="form-check-input" 
                           type="checkbox" 
                           id="modal-async">
                    <label class="form-check-label" for="modal-async">
                      <i class="fas fa-rocket me-1"></i>Run asynchronously
                    </label>
                  </div>
                </div>
              </div>

              <!-- Cleanup Command Section -->
              <div class="mb-4" v-if="showCleanupSection">
                <label class="form-label fw-bold">
                  <i class="fas fa-undo me-2"></i>
                  <!-- Contextual label for pair/cleanup command -->
                  <template v-if="editingCommand">
                    <template v-if="startStages.some(s => s.id === modalCommand.stage)">
                      Pair Command (Optional)
                    </template>
                    <template v-else>
                      Paired Startup Command (Optional)
                    </template>
                  </template>
                  <template v-else>
                    Cleanup Command (Optional)
                  </template>
                </label>

                <div class="cleanup-command-toggle mb-3">
                  <div class="form-check">
                    <input v-model="modalCleanupCommand.enabled"
                           class="form-check-input"
                           type="checkbox"
                           id="modal-cleanup-enabled">
                    <label class="form-check-label" for="modal-cleanup-enabled">
                      <i class="fas fa-broom me-1"></i>
                      <template v-if="editingCommand">
                        <template v-if="startStages.some(s => s.id === modalCommand.stage)">
                          <span>{{ modalCleanupCommand._pairGroupIndex !== undefined ? 'Edit existing pair command' : 'Create pair command' }}</span>
                        </template>
                        <template v-else>
                          <span>{{ modalCleanupCommand._pairGroupIndex !== undefined ? 'Edit paired startup command' : 'Create paired startup command' }}</span>
                        </template>
                      </template>
                      <template v-else>
                        Add cleanup command to reverse this action
                      </template>
                    </label>
                  </div>
                  <div class="form-text">
                    <template v-if="editingCommand">
                      <template v-if="startStages.some(s => s.id === modalCommand.stage)">
                        <span>
                          {{ modalCleanupCommand._pairGroupIndex !== undefined ? 'Edit the paired cleanup command' : 'Create a paired cleanup command' }}
                        </span>
                      </template>
                      <template v-else>
                        <span>
                          {{ modalCleanupCommand._pairGroupIndex !== undefined ? 'Edit the paired startup command' : 'Create a paired startup command' }}
                        </span>
                      </template>
                    </template>
                    <template v-else>
                      Optionally create a command to undo this action during a cleanup stage
                    </template>
                  </div>
                </div>

                <div v-if="modalCleanupCommand.enabled" class="cleanup-command-form">
                  <!-- Cleanup Command Input -->
                  <div class="mb-3">
                    <label class="form-label">
                      <template v-if="editingCommand">
                        <template v-if="startStages.some(s => s.id === modalCommand.stage)">
                          Paired Cleanup Command
                        </template>
                        <template v-else>
                          Paired Startup Command
                        </template>
                      </template>
                      <template v-else>
                        Cleanup Command
                      </template>
                    </label>
                    <textarea v-model="modalCleanupCommand.cmd"
                             class="form-control command-input"
                             rows="2"
                             :placeholder="getCleanupCommandPlaceholder()"
                             ref="cleanupCommandInput"></textarea>
                    <div class="form-text">
                      <template v-if="editingCommand">
                        <template v-if="startStages.some(s => s.id === modalCommand.stage)">
                          Enter the paired cleanup command
                        </template>
                        <template v-else>
                          Enter the paired startup command
                        </template>
                      </template>
                      <template v-else>
                        Enter the command to undo the main action
                      </template>
                    </div>
                  </div>

                  <!-- Cleanup Stage (dropdown, default to suggested) -->
                  <div class="mb-3">
                    <label class="form-label">Cleanup Stage</label>
                    <select v-model="modalCleanupCommand.stage" class="form-select" required>
                      <option value="" disabled>Select cleanup stage</option>
                      <option v-for="stage in cleanupStages" :key="stage.id" :value="stage.id">
                        {{ stage.name }}
                      </option>
                    </select>
                    <div class="form-text">Cleanup will run during: {{ getStageDescription(modalCleanupCommand.stage) }}</div>
                  </div>

                  <!-- Cleanup Options -->
                  <div class="mb-3">
                    <label class="form-label" for="modal-cleanup-timeout">Cleanup Options</label>
                    
                    <!-- Cleanup Timeout -->
                    <div class="row mb-2">
                      <div class="col-md-6">
                        <label class="form-label small" for="modal-cleanup-timeout">Timeout</label>
                        <div class="input-group">
                          <input v-model.number="modalCleanupCommand.timeout_seconds" 
                                 type="number" 
                                 class="form-control" 
                                 min="1" 
                                 max="3600" 
                                 placeholder="30" 
                                 id="modal-cleanup-timeout" />
                          <span class="input-group-text">seconds</span>
                        </div>
                      </div>
                    </div>

                    <!-- Cleanup Checkboxes -->
                    <div class="cleanup-options-grid">
                      <div v-if="platform === 'windows'" class="form-check">
                        <input v-model="modalCleanupCommand.elevated" 
                               class="form-check-input" 
                               type="checkbox" 
                               id="modal-cleanup-elevated">
                        <label class="form-check-label" for="modal-cleanup-elevated">
                          <i class="fas fa-shield-alt me-1"></i>Run as administrator
                        </label>
                      </div>
                      <div class="form-check">
                        <input v-model="modalCleanupCommand.ignore_error" 
                               class="form-check-input" 
                               type="checkbox" 
                               id="modal-cleanup-ignore-error">
                        <label class="form-check-label" for="modal-cleanup-ignore-error">
                          <i class="fas fa-exclamation-triangle me-1"></i>Ignore errors
                        </label>
                      </div>
                      <div class="form-check">
                        <input v-model="modalCleanupCommand.async" 
                               class="form-check-input" 
                               type="checkbox" 
                               id="modal-cleanup-async">
                        <label class="form-check-label" for="modal-cleanup-async">
                          <i class="fas fa-rocket me-1"></i>Run asynchronously
                        </label>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </form>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" @click="closeCommandModal">
              {{ $t('_common.cancel') || 'Cancel' }}
            </button>
            <button type="button" class="btn btn-primary" @click="saveCommand" :disabled="!modalCommand.cmd?.trim()">
              <i class="fas fa-save me-2"></i>{{ editingCommand ? 'Save Changes' : 'Add Command' }}
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed, ref, nextTick, watch } from 'vue'

const props = defineProps({
  modelValue: {
    type: Object,
    default: () => ({})
  },
  platform: {
    type: String,
    required: true
  },
  legacyCommands: {
    type: Array,
    default: () => []
  },
  appMode: {
    type: Boolean,
    default: false
  },
  title: {
    type: String,
    default: ''
  },
  description: {
    type: String,
    default: ''
  }
})

const emit = defineEmits(['update:modelValue'])

// Current view state
const currentView = ref('stage') // 'stage' or 'chronological'

// Stage definitions
const startStages = [
  { 
    id: 'PRE_STREAM_START', 
    name: 'Before Stream Start',
    description: 'Commands executed before the stream begins'
  },
  { 
    id: 'POST_STREAM_START', 
    name: 'After Stream Start',
    description: 'Commands executed after the stream has started successfully'
  },
  { 
    id: 'PRE_DISPLAY_CHECK', 
    name: 'Before Display Check',
    description: 'Commands executed before display validation'
  },
  { 
    id: 'POST_DISPLAY_CHECK', 
    name: 'After Display Check',
    description: 'Commands executed after display has been validated'
  },
  { 
    id: 'ADDITIONAL_CLIENT', 
    name: 'New Client Connected',
    description: 'Commands executed when an additional client connects'
  },
  { 
    id: 'STREAM_RESUME', 
    name: 'Stream Resume',
    description: 'Commands executed when stream resumes from pause'
  }
]

const cleanupStages = [
  { 
    id: 'STREAM_PAUSE', 
    name: 'Stream Pause',
    description: 'Commands executed when stream is paused'
  },
  { 
    id: 'PRE_STREAM_STOP', 
    name: 'Before Stream Stop',
    description: 'Commands executed before the stream stops'
  },
  { 
    id: 'PRE_DISPLAY_CLEANUP', 
    name: 'Before Display Cleanup',
    description: 'Commands executed before display cleanup'
  },
  { 
    id: 'POST_DISPLAY_CLEANUP', 
    name: 'After Display Cleanup',
    description: 'Commands executed after display cleanup'
  },
  { 
    id: 'AFTER_DISPLAY_CHECK_CLEANUP', 
    name: 'After Display Check Cleanup',
    description: 'Commands executed after display check but before cleanup is finalized'
  },
  { 
    id: 'POST_STREAM_STOP', 
    name: 'After Stream Stop',
    description: 'Commands executed after the stream has stopped'
  },
  { 
    id: 'ADDITIONAL_CLIENT_DISCONNECT', 
    name: 'Additional Client Disconnect',
    description: 'Commands executed when an additional client disconnects'
  }
]

// Chronological execution order
const chronologicalOrder = [
  'PRE_DISPLAY_CHECK',
  'POST_DISPLAY_CHECK',
  'PRE_STREAM_START',
  'POST_STREAM_START',
  'STREAM_RESUME',
  'ADDITIONAL_CLIENT',
  'STREAM_PAUSE',
  'ADDITIONAL_CLIENT_DISCONNECT',
  'PRE_STREAM_STOP',
  'PRE_DISPLAY_CLEANUP',
  'POST_DISPLAY_CLEANUP',
  'POST_STREAM_STOP'
]

// Computed titles with fallbacks
const displayTitle = computed(() => {
  return props.title || (props.appMode ? 'Application Event Actions' : 'Global Event Actions')
})

const displayDescription = computed(() => {
  return props.description || (props.appMode 
    ? 'Configure commands to run when this application starts and stops'
    : 'Configure commands to run when Sunshine starts and stops streaming')
})

// Modal state
const showCommandModal = ref(false)
const editingCommand = ref(false)
const editingIndex = ref(-1)
const commandInput = ref(null)

// Modal command data
const modalCommand = ref({
  cmd: '',
  stage: '',
  timeout_seconds: 30,
  elevated: false,
  ignore_error: false,
  async: false
})

// Cleanup command data
const modalCleanupCommand = ref({
  enabled: false,
  cmd: '',
  stage: '',
  timeout_seconds: 30,
  elevated: false,
  ignore_error: false,
  async: false
})

// Stage pairing for cleanup suggestions
const stagePairs = {
  'PRE_DISPLAY_CHECK': 'POST_DISPLAY_CLEANUP',
  'POST_DISPLAY_CHECK': 'PRE_DISPLAY_CLEANUP', 
  'PRE_STREAM_START': 'POST_STREAM_STOP',
  'POST_STREAM_START': 'PRE_STREAM_STOP',
  'ADDITIONAL_CLIENT': 'ADDITIONAL_CLIENT_DISCONNECT',
  'STREAM_RESUME': 'STREAM_PAUSE',
  // Add pairing for after display check cleanup if needed in the future
}

// Reverse mapping for finding pairs
const reverseStagePairs = Object.fromEntries(
  Object.entries(stagePairs).map(([key, value]) => [value, key])
)

// Function to find the pair command for a given command
function findPairCommand(command) {
  console.log('Finding pair for command:', command)
  console.log('Stage pairs:', stagePairs)
  console.log('Reverse stage pairs:', reverseStagePairs)
  
  const targetStage = stagePairs[command.stage] || reverseStagePairs[command.stage]
  console.log('Target stage for pair:', targetStage)
  
  if (!targetStage) {
    console.log('No target stage found, returning null')
    return null
  }
  
  // First, find the index of the current command in its own stage
  const data = props.modelValue || {}
  let currentCommandIndex = -1
  const currentStageGroups = data[command.stage] || []
  
  for (let groupIndex = 0; groupIndex < currentStageGroups.length; groupIndex++) {
    const group = currentStageGroups[groupIndex]
    if (group.commands) {
      for (let cmdIndex = 0; cmdIndex < group.commands.length; cmdIndex++) {
        // Compare command content instead of object reference
        const groupCmd = group.commands[cmdIndex]
        if (groupCmd.cmd === command.cmd && 
            groupCmd.timeout_seconds === command.timeout_seconds &&
            groupCmd.elevated === command.elevated &&
            groupCmd.ignore_error === command.ignore_error &&
            groupCmd.async === command.async) {
          currentCommandIndex = cmdIndex
          console.log('Found current command at index:', currentCommandIndex)
          break
        }
      }
      if (currentCommandIndex >= 0) break
    }
  }
  
  if (currentCommandIndex < 0) {
    console.log('Could not find current command index, returning null')
    return null
  }
  
  console.log('Current data:', data)
  console.log('Data keys:', Object.keys(data))
  console.log('Checking for stage:', targetStage)
  console.log('Does target stage exist in data?', targetStage in data)
  console.log('Target stage value:', data[targetStage])
  
  const stageGroups = data[targetStage] || []
  console.log('Stage groups for target stage:', stageGroups)
  console.log('Stage groups length:', stageGroups.length)
  
  for (let groupIndex = 0; groupIndex < stageGroups.length; groupIndex++) {
    const group = stageGroups[groupIndex]
    console.log(`Checking group ${groupIndex}:`, group)
    if (group.commands) {
      console.log(`Group ${groupIndex} commands:`, group.commands)
      
      // Match by index position, accounting for reverse order in cleanup stages
      let targetIndex = currentCommandIndex
      
      // Determine if we need to apply reverse indexing based on the stage relationship
      // We need reverse indexing when:
      // 1. Going from setup to cleanup stage (target is cleanup)
      // 2. Going from cleanup to setup stage (source is cleanup)
      const isSourceCleanupStage = command.stage.startsWith('POST_') || 
                                   command.stage === 'PRE_STREAM_STOP' || 
                                   command.stage === 'PRE_DISPLAY_CLEANUP' ||
                                   command.stage === 'STREAM_PAUSE' ||
                                   command.stage === 'ADDITIONAL_CLIENT_DISCONNECT'
      
      const isTargetCleanupStage = targetStage.startsWith('POST_') || 
                                   targetStage === 'PRE_STREAM_STOP' || 
                                   targetStage === 'PRE_DISPLAY_CLEANUP' ||
                                   targetStage === 'STREAM_PAUSE' ||
                                   targetStage === 'ADDITIONAL_CLIENT_DISCONNECT'
      
      // Apply reverse indexing if either source is cleanup OR target is cleanup
      // This covers both directions: setup→cleanup and cleanup→setup
      const shouldReverseIndex = isSourceCleanupStage || isTargetCleanupStage
      
      if (shouldReverseIndex) {
        // For cleanup stages, commands are in reverse order
        // So PRE_STREAM_START[0] pairs with POST_STREAM_STOP[last]
        // And POST_STREAM_STOP[0] pairs with PRE_STREAM_START[last]
        targetIndex = (group.commands.length - 1) - currentCommandIndex
      }
      
      console.log('Target index for pair:', targetIndex, 'shouldReverseIndex:', shouldReverseIndex, 'isSourceCleanup:', isSourceCleanupStage, 'isTargetCleanup:', isTargetCleanupStage)
      
      if (targetIndex < group.commands.length) {
        const possiblePair = group.commands[targetIndex]
        if (possiblePair) {
          const result = {
            ...possiblePair,
            stage: targetStage,
            groupIndex: groupIndex,
            commandIndex: targetIndex
          }
          console.log('Found pair command by index:', result)
          return result
        }
      }
    } else {
      console.log(`Group ${groupIndex} has no commands property`)
    }
  }
  console.log('No pair command found, returning null')
  return null
}

// Check if a command has a pair
function hasPairCommand(command) {
  return !!findPairCommand(command)
}

// Computed commands list - flattened from the event actions data
const commandsList = computed(() => {
  const commands = []
  const data = props.modelValue || {}
  
  // Get all stages in order
  const allStages = [...startStages, ...cleanupStages]
  
  allStages.forEach(stage => {
    const stageGroups = data[stage.id] || []
    stageGroups.forEach(group => {
      if (group.commands) {
        group.commands.forEach(command => {
          commands.push({
            ...command,
            stage: stage.id,
            stageName: stage.name
          })
        })
      }
    })
  })
  
  return commands
})

// Computed cleanup stage suggestion
const suggestedCleanupStage = computed(() => {
  return stagePairs[modalCommand.value.stage] || ''
})

// Computed property to determine if cleanup section should be shown
const showCleanupSection = computed(() => {
  if (editingCommand.value) {
    // When editing, show if the current command stage has a pair stage
    const currentStage = modalCommand.value.stage
    const hasPairStage = !!(stagePairs[currentStage] || reverseStagePairs[currentStage])
    return hasPairStage
  } else {
    // When creating new, show if there's a suggested cleanup stage
    const suggested = !!suggestedCleanupStage.value
    return suggested
  }
})

// Watch for stage changes to auto-suggest cleanup stage
watch(() => modalCommand.value.stage, (newStage) => {
  const suggestedStage = stagePairs[newStage]
  if (suggestedStage && !editingCommand.value) {
    modalCleanupCommand.value.stage = suggestedStage
    // Do not auto-enable, just set the stage
  } else if (editingCommand.value) {
    // When editing, update the cleanup stage if there's a pair
    const pairStage = stagePairs[newStage] || reverseStagePairs[newStage]
    if (pairStage && modalCleanupCommand.value.enabled) {
      modalCleanupCommand.value.stage = pairStage
    }
  }
})

// Computed chronological sections for timeline view
const chronologicalSections = computed(() => {
  const commands = commandsList.value.map((cmd, index) => ({
    ...cmd,
    originalIndex: index
  }))
  
  const sections = []
  let chronoIndex = 0
  
  // Group stages into logical phases
  const phases = [
    {
      title: 'Stream Preparation',
      description: 'Commands executed during stream setup and validation',
      phase: 'startup',
      icon: 'fas fa-play-circle',
      stages: ['PRE_DISPLAY_CHECK', 'POST_DISPLAY_CHECK', 'PRE_STREAM_START']
    },
    {
      title: 'Stream Start',
      description: 'Commands executed when stream becomes active',
      phase: 'startup',
      icon: 'fas fa-broadcast-tower',
      stages: ['POST_STREAM_START', 'STREAM_RESUME']
    },
    {
      title: 'Client Management',
      description: 'Commands executed when clients connect or disconnect',
      phase: 'runtime',
      icon: 'fas fa-users',
      stages: ['ADDITIONAL_CLIENT', 'ADDITIONAL_CLIENT_DISCONNECT']
    },
    {
      title: 'Stream Control',
      description: 'Commands executed during stream lifecycle events',
      phase: 'runtime',
      icon: 'fas fa-pause-circle',
      stages: ['STREAM_PAUSE']
    },
    {
      title: 'Stream Cleanup',
      description: 'Commands executed during stream shutdown and cleanup',
      phase: 'cleanup',
      icon: 'fas fa-stop-circle',
      stages: ['PRE_STREAM_STOP', 'PRE_DISPLAY_CLEANUP', 'POST_DISPLAY_CLEANUP', 'POST_STREAM_STOP']
    }
  ]
  
  phases.forEach(phase => {
    const phaseCommands = []
    
    phase.stages.forEach(stageId => {
      const stageCommands = commands.filter(cmd => cmd.stage === stageId)
      stageCommands.forEach(cmd => {
        phaseCommands.push({
          ...cmd,
          chronoIndex: chronoIndex++
        })
      })
    })
    
    if (phaseCommands.length > 0) {
      sections.push({
        ...phase,
        commands: phaseCommands
      })
    }
  })
  
  return sections
})

// Computed grouped stages for stage view
const groupedStages = computed(() => {
  const allStages = [...startStages, ...cleanupStages]
  const groups = []
  
  // Group commands by stage
  allStages.forEach(stage => {
    const stageCommands = commandsList.value
      .map((cmd, index) => ({ ...cmd, originalIndex: index }))
      .filter(cmd => cmd.stage === stage.id)
    
    if (stageCommands.length > 0) {
      groups.push({
        stage: stage,
        commands: stageCommands
      })
    }
  })
  
  return groups
})

// Helper functions
function getStageDisplayName(stageId) {
  const stage = [...startStages, ...cleanupStages].find(s => s.id === stageId)
  return stage ? stage.name : stageId
}

function getStageDescription(stageId) {
  const stage = [...startStages, ...cleanupStages].find(s => s.id === stageId)
  return stage ? stage.description : ''
}

function getCommandPlaceholder() {
  if (props.platform === 'windows') {
    return 'e.g., C:\\Windows\\System32\\notepad.exe'
  }
  return 'e.g., /usr/bin/echo "Hello World"'
}

function getCleanupCommandPlaceholder() {
  if (props.platform === 'windows') {
    return 'e.g., taskkill /f /im notepad.exe'
  }
  return 'e.g., pkill -f "process_name"'
}

// Helper functions for chronological view
function getChronologicalBadgeClass(stageId) {
  const startupStages = ['PRE_DISPLAY_CHECK', 'POST_DISPLAY_CHECK', 'PRE_STREAM_START', 'POST_STREAM_START', 'STREAM_RESUME']
  const runtimeStages = ['ADDITIONAL_CLIENT', 'STREAM_PAUSE', 'ADDITIONAL_CLIENT_DISCONNECT']
  const cleanupStages = ['PRE_STREAM_STOP', 'PRE_DISPLAY_CLEANUP', 'POST_DISPLAY_CLEANUP', 'POST_STREAM_STOP']
  
  if (startupStages.includes(stageId)) return 'bg-success'
  if (runtimeStages.includes(stageId)) return 'bg-warning'
  if (cleanupStages.includes(stageId)) return 'bg-secondary'
  return 'bg-primary'
}

function getStagePhaseClass(stageId) {
  const startupStages = ['PRE_DISPLAY_CHECK', 'POST_DISPLAY_CHECK', 'PRE_STREAM_START', 'POST_STREAM_START', 'STREAM_RESUME']
  const runtimeStages = ['ADDITIONAL_CLIENT', 'STREAM_PAUSE', 'ADDITIONAL_CLIENT_DISCONNECT']
  const cleanupStages = ['PRE_STREAM_STOP', 'PRE_DISPLAY_CLEANUP', 'POST_DISPLAY_CLEANUP', 'POST_STREAM_STOP']
  
  if (startupStages.includes(stageId)) return 'startup'
  if (runtimeStages.includes(stageId)) return 'runtime'
  if (cleanupStages.includes(stageId)) return 'cleanup'
  return 'default'
}

// Modal functions
async function openCommandModal(command = null, index = -1) {
  editingCommand.value = !!command
  editingIndex.value = index
  
  if (command) {
    // Editing existing command
    console.log('Opening edit modal for command:', command)
    modalCommand.value = {
      cmd: command.cmd || '',
      stage: command.stage || '',
      timeout_seconds: command.timeout_seconds || 30,
      elevated: command.elevated || false,
      ignore_error: command.ignore_error || false,
      async: command.async || false
    }

    // Determine if this is a startup or cleanup command
    const isStartup = startStages.some(s => s.id === command.stage)
    const isCleanup = cleanupStages.some(s => s.id === command.stage)

    // Always find the pair command (opposite direction)
    const pairCommand = findPairCommand(command)
    console.log('Found pair command:', pairCommand)

    if (pairCommand) {
      // If editing a startup command, paired is cleanup; if editing cleanup, paired is startup
      modalCleanupCommand.value.enabled = true
      modalCleanupCommand.value.cmd = pairCommand.cmd || ''
      modalCleanupCommand.value.stage = pairCommand.stage
      modalCleanupCommand.value.timeout_seconds = pairCommand.timeout_seconds || 30
      modalCleanupCommand.value.elevated = pairCommand.elevated || false
      modalCleanupCommand.value.ignore_error = pairCommand.ignore_error || false
      modalCleanupCommand.value.async = pairCommand.async || false
      // Store pair location for editing
      modalCleanupCommand.value._pairGroupIndex = pairCommand.groupIndex
      modalCleanupCommand.value._pairCommandIndex = pairCommand.commandIndex
      console.log('Set modalCleanupCommand to existing pair:', modalCleanupCommand.value)
    } else {
      // No existing pair, but allow creating one
      const pairStage = stagePairs[command.stage] || reverseStagePairs[command.stage]
      console.log('No existing pair found. Pair stage would be:', pairStage)
      modalCleanupCommand.value.enabled = false
      modalCleanupCommand.value.cmd = ''
      modalCleanupCommand.value.stage = pairStage || ''
      modalCleanupCommand.value.timeout_seconds = 30
      modalCleanupCommand.value.elevated = false
      modalCleanupCommand.value.ignore_error = false
      modalCleanupCommand.value.async = false
      delete modalCleanupCommand.value._pairGroupIndex
      delete modalCleanupCommand.value._pairCommandIndex
      console.log('Set modalCleanupCommand to new pair template:', modalCleanupCommand.value)
    }
  } else {
    // Creating new command
    modalCommand.value = {
      cmd: '',
      stage: 'PRE_STREAM_START', // Default to most common stage
      timeout_seconds: 30,
      elevated: false,
      ignore_error: false,
      async: false
    }
    // Default cleanup stage to suggested - update properties individually
    modalCleanupCommand.value.enabled = !!suggestedCleanupStage.value
    modalCleanupCommand.value.cmd = ''
    modalCleanupCommand.value.stage = suggestedCleanupStage.value || ''
    modalCleanupCommand.value.timeout_seconds = 30
    modalCleanupCommand.value.elevated = false
    modalCleanupCommand.value.ignore_error = false
    modalCleanupCommand.value.async = false
    // Clear any stored pair location
    delete modalCleanupCommand.value._pairGroupIndex
    delete modalCleanupCommand.value._pairCommandIndex
  }
  showCommandModal.value = true
  await nextTick()
  if (commandInput.value) {
    commandInput.value.focus()
  }
}

function closeCommandModal() {
  showCommandModal.value = false
  editingCommand.value = false
  editingIndex.value = -1
  modalCommand.value = {
    cmd: '',
    stage: '',
    timeout_seconds: 30,
    elevated: false,
    ignore_error: false,
    async: false
  }
  // Reset cleanup command properties individually
  modalCleanupCommand.value.enabled = false
  modalCleanupCommand.value.cmd = ''
  modalCleanupCommand.value.stage = ''
  modalCleanupCommand.value.timeout_seconds = 30
  modalCleanupCommand.value.elevated = false
  modalCleanupCommand.value.ignore_error = false
  modalCleanupCommand.value.async = false
  delete modalCleanupCommand.value._pairGroupIndex
  delete modalCleanupCommand.value._pairCommandIndex
}

function saveCommand() {
  if (!modalCommand.value.cmd?.trim() || !modalCommand.value.stage) {
    return
  }

  const updatedData = { ...props.modelValue }
  const stageId = modalCommand.value.stage
  
  // Ensure the stage exists
  if (!updatedData[stageId]) {
    updatedData[stageId] = []
  }
  
  // Ensure there's at least one group for this stage
  if (updatedData[stageId].length === 0) {
    updatedData[stageId].push({
      name: `${stageId} Commands`,
      failure_policy: 'FAIL_FAST',
      commands: []
    })
  }
  
  const newCommand = {
    cmd: modalCommand.value.cmd.trim(),
    timeout_seconds: modalCommand.value.timeout_seconds || 30,
    elevated: modalCommand.value.elevated || false,
    ignore_error: modalCommand.value.ignore_error || false,
    async: modalCommand.value.async || false
  }
  
  if (editingCommand.value && editingIndex.value >= 0) {
    // Update existing command
    let currentIndex = 0
    let found = false
    
    for (const stage of [...startStages, ...cleanupStages]) {
      const stageGroups = updatedData[stage.id] || []
      for (const group of stageGroups) {
        for (let i = 0; i < (group.commands?.length || 0); i++) {
          if (currentIndex === editingIndex.value) {
            group.commands[i] = newCommand
            found = true
            break
          }
          currentIndex++
        }
        if (found) break
      }
      if (found) break
    }
    
    // Handle pair command editing
    if (modalCleanupCommand.value.enabled && modalCleanupCommand.value.cmd?.trim()) {
      const pairStageId = modalCleanupCommand.value.stage
      
      // Ensure the pair stage exists
      if (!updatedData[pairStageId]) {
        updatedData[pairStageId] = []
      }
      
      // Ensure there's at least one group for the pair stage
      if (updatedData[pairStageId].length === 0) {
        updatedData[pairStageId].push({
          name: `${pairStageId} Commands`,
          failure_policy: 'FAIL_FAST',
          commands: []
        })
      }
      
      const pairCommand = {
        cmd: modalCleanupCommand.value.cmd.trim(),
        timeout_seconds: modalCleanupCommand.value.timeout_seconds || 30,
        elevated: modalCleanupCommand.value.elevated || false,
        ignore_error: modalCleanupCommand.value.ignore_error || false,
        async: modalCleanupCommand.value.async || false
      }
      
      // Check if we're updating an existing pair or creating a new one
      if (modalCleanupCommand.value._pairGroupIndex !== undefined && 
          modalCleanupCommand.value._pairCommandIndex !== undefined) {
        // Update existing pair command
        const pairGroup = updatedData[pairStageId][modalCleanupCommand.value._pairGroupIndex]
        if (pairGroup && pairGroup.commands) {
          pairGroup.commands[modalCleanupCommand.value._pairCommandIndex] = pairCommand
        }
      } else {
        // Add new pair command
        updatedData[pairStageId][0].commands.push(pairCommand)
      }
    } else if (modalCleanupCommand.value._pairGroupIndex !== undefined && 
               modalCleanupCommand.value._pairCommandIndex !== undefined) {
      // User disabled the pair command, remove it
      const pairStageId = stagePairs[modalCommand.value.stage] || reverseStagePairs[modalCommand.value.stage]
      if (pairStageId && updatedData[pairStageId]) {
        const pairGroup = updatedData[pairStageId][modalCleanupCommand.value._pairGroupIndex]
        if (pairGroup && pairGroup.commands) {
          pairGroup.commands.splice(modalCleanupCommand.value._pairCommandIndex, 1)
          // Remove group if it becomes empty
          if (pairGroup.commands.length === 0) {
            updatedData[pairStageId].splice(modalCleanupCommand.value._pairGroupIndex, 1)
            // Remove stage if it becomes empty
            if (updatedData[pairStageId].length === 0) {
              delete updatedData[pairStageId]
            }
          }
        }
      }
    }
  } else {
    // Add new command to the first group of the selected stage
    updatedData[stageId][0].commands.push(newCommand)
    
    // Handle cleanup command if enabled
    if (modalCleanupCommand.value.enabled && modalCleanupCommand.value.cmd?.trim()) {
      const cleanupStageId = modalCleanupCommand.value.stage
      
      // Ensure the cleanup stage exists
      if (!updatedData[cleanupStageId]) {
        updatedData[cleanupStageId] = []
      }
      
      // Ensure there's at least one group for the cleanup stage
      if (updatedData[cleanupStageId].length === 0) {
        updatedData[cleanupStageId].push({
          name: `${cleanupStageId} Commands`,
          failure_policy: 'FAIL_FAST',
          commands: []
        })
      }
      
      const cleanupCommand = {
        cmd: modalCleanupCommand.value.cmd.trim(),
        timeout_seconds: modalCleanupCommand.value.timeout_seconds || 30,
        elevated: modalCleanupCommand.value.elevated || false,
        ignore_error: modalCleanupCommand.value.ignore_error || false,
        async: modalCleanupCommand.value.async || false
      }
      
      // Add cleanup command in reverse order (prepend to beginning of cleanup stage)
      updatedData[cleanupStageId][0].commands.unshift(cleanupCommand)
    }
  }
  
  emit('update:modelValue', updatedData)
  closeCommandModal()
}

function removeCommand(index) {
  if (!confirm('Are you sure you want to delete this command? If it has a paired command, both will be deleted.')) {
    return
  }

  const updatedData = { ...props.modelValue }
  let currentIndex = 0
  let found = false
  let removedCommand = null

  // First, remove the main command and remember its details
  for (const stage of [...startStages, ...cleanupStages]) {
    const stageGroups = updatedData[stage.id] || []
    for (const group of stageGroups) {
      for (let i = 0; i < (group.commands?.length || 0); i++) {
        if (currentIndex === index) {
          removedCommand = { ...group.commands[i], stage: stage.id }
          group.commands.splice(i, 1)
          found = true
          break
        }
        currentIndex++
      }
      if (found) break
    }
    if (found) break
  }

  // Now, try to find and remove the paired command if it exists
  if (removedCommand) {
    const pair = findPairCommand(removedCommand)
    if (pair) {
      const pairStageId = pair.stage
      const pairGroupIndex = pair.groupIndex
      const pairCommandIndex = pair.commandIndex
      if (
        updatedData[pairStageId] &&
        updatedData[pairStageId][pairGroupIndex] &&
        updatedData[pairStageId][pairGroupIndex].commands &&
        updatedData[pairStageId][pairGroupIndex].commands.length > pairCommandIndex
      ) {
        updatedData[pairStageId][pairGroupIndex].commands.splice(pairCommandIndex, 1)
        // Remove group if it becomes empty
        if (updatedData[pairStageId][pairGroupIndex].commands.length === 0) {
          updatedData[pairStageId].splice(pairGroupIndex, 1)
          // Remove stage if it becomes empty
          if (updatedData[pairStageId].length === 0) {
            delete updatedData[pairStageId]
          }
        }
      }
    }
  }

  emit('update:modelValue', updatedData)
}

function moveCommand(index, direction) {
  const commands = [...commandsList.value]
  const newIndex = index + direction
  
  if (newIndex < 0 || newIndex >= commands.length) {
    return
  }
  
  // Swap commands
  [commands[index], commands[newIndex]] = [commands[newIndex], commands[index]]
  
  // Rebuild the data structure
  const updatedData = {}
  
  commands.forEach(command => {
    const stageId = command.stage
    if (!updatedData[stageId]) {
      updatedData[stageId] = [{
        name: `${getStageDisplayName(stageId)} Commands`,
        failure_policy: 'FAIL_FAST',
        commands: []
      }]
    }
    
    const { stage, stageName, ...commandData } = command
    updatedData[stageId][0].commands.push(commandData)
  })
  
  emit('update:modelValue', updatedData)
}
</script>

<style scoped>
.event-actions-section {
  width: 100%;
  margin-bottom: 1.5rem;
}

.section-header {
  margin-bottom: 1rem;
}

.section-title {
  font-weight: 600;
  font-size: 1.1rem;
  color: var(--bs-body-color, #1f2937);
  display: flex;
  align-items: center;
  margin-bottom: 0.5rem;
}

.section-description {
  color: var(--bs-secondary, #6b7280);
  font-size: 0.9rem;
  line-height: 1.4;
}

/* Commands List Container */
.commands-list-container {
  background: var(--bs-body-bg, #ffffff);
  border: 1px solid var(--bs-border-color, #e2e8f0);
  border-radius: 0.5rem;
  overflow: hidden;
}

/* View Tabs */
.view-tabs .nav-tabs {
  border-bottom: 1px solid var(--bs-border-color, #e2e8f0);
  background: var(--bs-light, #f8f9fa);
  padding: 0 1rem;
  margin-bottom: 0;
}

.view-tabs .nav-link {
  border: none;
  border-radius: 0;
  color: var(--bs-secondary, #6b7280);
  font-weight: 500;
  padding: 0.75rem 1rem;
  transition: all 0.15s ease;
}

.view-tabs .nav-link:hover {
  color: var(--bs-primary, #0d6efd);
  background: transparent;
  border-color: transparent;
}

.view-tabs .nav-link.active {
  color: var(--bs-primary, #0d6efd);
  background: var(--bs-body-bg, #ffffff);
  border-color: var(--bs-border-color, #e2e8f0) var(--bs-border-color, #e2e8f0) transparent;
  border-width: 1px 1px 0;
  border-style: solid;
}

.commands-count-info {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 0.5rem;
}

/* Empty State */
.empty-state {
  padding: 3rem 2rem;
}

.empty-state-icon {
  color: var(--bs-secondary, #6b7280);
}

/* Commands Table */
.commands-table {
  min-height: 200px;
}

.table-header {
  background: var(--bs-light, #f8f9fa);
  border-bottom: 1px solid var(--bs-border-color, #e2e8f0);
  padding: 1rem 1.5rem;
}

.commands-list {
  max-height: 600px;
  overflow-y: auto;
}

/* Command Row */
.command-row {
  border-bottom: 1px solid var(--bs-border-color, #e2e8f0);
}

.command-row:last-child {
  border-bottom: none;
}

.command-card {
  padding: 1.25rem 1.5rem;
  transition: background-color 0.15s ease;
}

.command-card:hover {
  background: var(--bs-light, #f8f9fa);
}

.command-content {
  display: flex;
  align-items: flex-start;
  gap: 1rem;
}

.command-main {
  display: flex;
  align-items: flex-start;
  gap: 1rem;
  flex: 1;
  min-width: 0;
}

.command-number .badge {
  width: 2rem;
  height: 2rem;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 50%;
  font-size: 0.875rem;
  font-weight: 600;
}

.command-details {
  min-width: 0;
}

.command-text {
  margin-bottom: 0.75rem;
}

.command-code {
  background: var(--bs-gray-100, #f8f9fa);
  border: 1px solid var(--bs-border-color, #e2e8f0);
  border-radius: 0.25rem;
  padding: 0.5rem 0.75rem;
  font-family: var(--bs-font-monospace, 'Fira Code', 'Monaco', 'Consolas', monospace);
  font-size: 0.875rem;
  color: var(--bs-body-color, #1f2937);
  display: block;
  word-break: break-all;
  white-space: pre-wrap;
  max-width: 100%;
  overflow-x: auto;
}

.command-meta {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
}

.meta-tag {
  display: inline-flex;
  align-items: center;
  padding: 0.25rem 0.5rem;
  border-radius: 0.25rem;
  font-size: 0.75rem;
  font-weight: 500;
  white-space: nowrap;
}

.meta-tag.elevated {
  background: #fff3cd;
  color: #856404;
  border: 1px solid #ffeaa7;
}

.meta-tag.paired {
  background: #d4edda;
  color: #155724;
  border: 1px solid #c3e6cb;
}

.meta-tag.async {
  background: #d1ecf1;
  color: #0c5460;
  border: 1px solid #b8daff;
}

.meta-tag.ignore-error {
  background: #f8d7da;
  color: #721c24;
  border: 1px solid #f5c6cb;
}

.meta-tag.timeout {
  background: #e2e3e5;
  color: #383d41;
  border: 1px solid #d6d8db;
}

.meta-tag.stage {
  background: #d4edda;
  color: #155724;
  border: 1px solid #c3e6cb;
}

/* Command Actions */
.command-actions {
  flex-shrink: 0;
}

.command-actions .btn-group .btn {
  padding: 0.375rem 0.5rem;
  border-color: var(--bs-border-color, #e2e8f0);
}

.command-actions .btn:hover {
  background: var(--bs-primary, #0d6efd);
  border-color: var(--bs-primary, #0d6efd);
  color: white;
}

.command-actions .btn-outline-danger:hover {
  background: var(--bs-danger, #dc3545);
  border-color: var(--bs-danger, #dc3545);
  color: white;
}

/* Chronological View Styles */
.chronological-view {
  padding: 0;
}

.chronological-section {
  margin-bottom: 0;
}

.section-divider {
  height: 1px;
  background: linear-gradient(to right, transparent, var(--bs-border-color, #e2e8f0), transparent);
  margin: 1.5rem 0;
}

.chronological-section-header {
  padding: 1rem 1.5rem 0.5rem;
  background: var(--bs-light, #f8f9fa);
  border-bottom: 1px solid var(--bs-border-color, #e2e8f0);
}

.section-phase-badge {
  display: inline-flex;
  align-items: center;
  padding: 0.375rem 0.75rem;
  border-radius: 0.375rem;
  font-weight: 600;
  font-size: 0.875rem;
  margin-bottom: 0.5rem;
}

.section-phase-badge.startup {
  background: #d1f2eb;
  color: #0f5132;
  border: 1px solid #badbcc;
}

.section-phase-badge.runtime {
  background: #fff3cd;
  color: #664d03;
  border: 1px solid #ffeaa7;
}

.section-phase-badge.cleanup {
  background: #e2e3e5;
  color: #383d41;
  border: 1px solid #d6d8db;
}

.chronological-command .command-card {
  border-left: 4px solid transparent;
  margin-left: 1rem;
  position: relative;
}


.chronological-command:not(:last-child) .command-card::after {
  content: '';
  position: absolute;
  left: -1.4375rem;
  top: calc(50% + 0.375rem);
  width: 2px;
  height: calc(100% - 0.75rem);
  background: var(--bs-border-color, #e2e8f0);
}

.meta-tag.stage-chrono.startup {
  background: #d1f2eb;
  color: #0f5132;
  border: 1px solid #badbcc;
}

.meta-tag.stage-chrono.runtime {
  background: #fff3cd;
  color: #664d03;
  border: 1px solid #ffeaa7;
}

.meta-tag.stage-chrono.cleanup {
  background: #e2e3e5;
  color: #383d41;
  border: 1px solid #d6d8db;
}

/* Stage View Styles */
.stage-view {
  padding: 0;
}

.stage-group {
  margin-bottom: 0;
}

.stage-group-header {
  padding: 1rem 1.5rem 0.5rem;
  background: var(--bs-light, #f8f9fa);
  border-bottom: 1px solid var(--bs-border-color, #e2e8f0);
}

.stage-header-badge {
  display: inline-flex;
  align-items: center;
  padding: 0.375rem 0.75rem;
  border-radius: 0.375rem;
  font-weight: 600;
  font-size: 0.875rem;
  margin-bottom: 0.5rem;
}

.stage-header-badge.startup {
  background: #d1f2eb;
  color: #0f5132;
  border: 1px solid #badbcc;
}

.stage-header-badge.runtime {
  background: #fff3cd;
  color: #664d03;
  border: 1px solid #ffeaa7;
}

.stage-header-badge.cleanup {
  background: #e2e3e5;
  color: #383d41;
  border: 1px solid #d6d8db;
}

.stage-header-badge.default {
  background: #cff4fc;
  color: #055160;
  border: 1px solid #9eeaf9;
}

.stage-description {
  color: var(--bs-secondary, #6b7280);
  font-size: 0.8rem;
  margin-bottom: 0.25rem;
}

.stage-command-count {
  color: var(--bs-secondary, #6b7280);
  font-size: 0.75rem;
  font-weight: 500;
}

.stage-command .command-card {
  border-left: 4px solid transparent;
  margin-bottom: 0.5rem;
}

/* Modal Styles */
.modal {
  z-index: 1055;
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
}

.modal-dialog {
  margin: auto;
  max-width: 800px;
  width: 90%;
}

.modal-content {
  border-radius: 0.5rem;
  border: none;
  box-shadow: 0 10px 15px rgba(0, 0, 0, 0.1);
}

.modal-header {
  background: var(--bs-light, #f8f9fa);
  border-bottom: 1px solid var(--bs-border-color, #e2e8f0);
  border-radius: 0.5rem 0.5rem 0 0;
}

.modal-title {
  color: var(--bs-primary, #0d6efd);
  font-weight: 600;
}

.modal-body {
  padding: 2rem;
}

.modal-footer {
  background: var(--bs-light, #f8f9fa);
  border-top: 1px solid var(--bs-border-color, #e2e8f0);
  border-radius: 0 0 0.5rem 0.5rem;
}

/* Form Elements in Modal */
.command-input {
  font-family: var(--bs-font-monospace, 'Fira Code', 'Monaco', 'Consolas', monospace);
  background: var(--bs-gray-100, #f8f9fa);
  border: 1px solid var(--bs-border-color, #e2e8f0);
  border-radius: 0.25rem;
  padding: 0.75rem;
  font-size: 0.875rem;
  line-height: 1.4;
  resize: vertical;
  min-height: 80px;
}

.command-input:focus {
  outline: none;
  border-color: var(--bs-primary, #0d6efd);
  box-shadow: 0 0 0 3px rgba(13, 110, 253, 0.1);
  background: white;
}

.form-label.fw-bold {
  color: var(--bs-body-color, #1f2937);
  margin-bottom: 0.5rem;
}

.form-label i {
  color: var(--bs-primary, #0d6efd);
}

.options-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 1rem;
  margin-top: 0.75rem;
}

.form-check {
  display: flex;
  align-items: center;
}

.form-check-input {
  margin-right: 0.5rem;
  margin-top: 0;
}

.form-check-label {
  display: flex;
  align-items: center;
  margin-bottom: 0;
  font-size: 0.875rem;
  cursor: pointer;
}

.form-check-label i {
  color: var(--bs-secondary, #6b7280);
}

/* Cleanup Command Styles */
.cleanup-command-toggle {
  background: var(--bs-light, #f8f9fa);
  border: 1px solid var(--bs-border-color, #e2e8f0);
  border-radius: 0.375rem;
  padding: 1rem;
}

.cleanup-command-form {
  background: #fefcf3;
  border: 1px solid #fde68a;
  border-radius: 0.375rem;
  padding: 1rem;
  margin-top: 0.75rem;
}

.cleanup-options-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 0.75rem;
  margin-top: 0.5rem;
}

.cleanup-command-form .form-label.small {
  font-size: 0.875rem;
  margin-bottom: 0.25rem;
}

/* Responsive Design */
@media (max-width: 768px) {
  .view-tabs .nav-tabs {
    padding: 0 0.5rem;
  }

  .view-tabs .nav-link {
    padding: 0.5rem 0.75rem;
    font-size: 0.875rem;
  }

  .commands-count-info {
    flex-direction: column;
    align-items: flex-start;
    gap: 0.25rem;
  }

  .command-content {
    flex-direction: column;
    gap: 0.75rem;
  }

  .command-main {
    flex-direction: column;
    gap: 0.75rem;
  }

  .command-actions {
    align-self: stretch;
  }

  .command-actions .btn-group {
    width: 100%;
    display: flex;
  }

  .command-actions .btn {
    flex: 1;
  }

  .command-code {
    font-size: 0.75rem;
    padding: 0.5rem;
  }

  .meta-tag {
    font-size: 0.6875rem;
    padding: 0.1875rem 0.375rem;
  }

  .modal-dialog {
    width: 95%;
    margin: 1rem auto;
  }

  .modal-body {
    padding: 1.5rem;
  }

  .options-grid {
    grid-template-columns: 1fr;
    gap: 0.75rem;
  }

  .cleanup-options-grid {
    grid-template-columns: 1fr;
    gap: 0.5rem;
  }

  .cleanup-command-toggle {
    padding: 0.75rem;
  }

  .cleanup-command-form {
    padding: 0.75rem;
  }

  /* Chronological view mobile adjustments */
  .chronological-section-header {
    padding: 0.75rem 1rem 0.5rem;
  }

  .chronological-command .command-card {
    margin-left: 0.5rem;
  }

  .chronological-command .command-card::before {
    left: -1.25rem;
    width: 0.5rem;
    height: 0.5rem;
  }

  .chronological-command:not(:last-child) .command-card::after {
    left: -1rem;
  }

  .section-phase-badge {
    font-size: 0.75rem;
    padding: 0.25rem 0.5rem;
  }

  .section-description {
    font-size: 0.75rem;
  }

  /* Stage view mobile adjustments */
  .stage-group-header {
    padding: 0.75rem 1rem 0.5rem;
  }

  .stage-header-badge {
    font-size: 0.75rem;
    padding: 0.25rem 0.5rem;
  }

  .stage-description {
    font-size: 0.75rem;
  }

  .stage-command-count {
    font-size: 0.6875rem;
  }
}

/* Scrollbar styling */
.commands-list::-webkit-scrollbar {
  width: 8px;
}

.commands-list::-webkit-scrollbar-track {
  background: transparent;
}

.commands-list::-webkit-scrollbar-thumb {
  background: #cbd5e1;
  border-radius: 4px;
  border: 2px solid transparent;
  background-clip: content-box;
}

.commands-list:hover::-webkit-scrollbar-thumb {
  background: #94a3b8;
  background-clip: content-box;
}

/* Focus styles for accessibility */
*:focus-visible {
  outline: 2px solid var(--bs-primary, #0d6efd);
  outline-offset: 2px;
}

.btn:focus-visible {
  box-shadow: 0 0 0 3px rgba(13, 110, 253, 0.25);
}

/* Center command numbers in chronological section - span 2 rows */
.chronological-command .command-main {
  display: grid;
  grid-template-columns: auto 1fr;
  grid-template-rows: auto auto;
  gap: 1rem;
  align-items: start;
}

.chronological-command .command-number {
  grid-row: 1 / 3;
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
}

.chronological-command .command-number .badge {
  display: flex;
  align-items: center;
  justify-content: center;
}

.chronological-command .command-details {
  grid-column: 2;
  grid-row: 1 / 3;
  min-width: 0;
}

/* Center command numbers in stage section (regular commands) - span 2 rows */
.stage-command .command-main {
  display: grid;
  grid-template-columns: auto 1fr;
  grid-template-rows: auto auto;
  gap: 1rem;
  align-items: start;
}

.stage-command .command-number {
  grid-row: 1 / 3;
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
}

.stage-command .command-number .badge {
  display: flex;
  align-items: center;
  justify-content: center;
}

.stage-command .command-details {
  grid-column: 2;
  grid-row: 1 / 3;
  min-width: 0;
}
</style>
