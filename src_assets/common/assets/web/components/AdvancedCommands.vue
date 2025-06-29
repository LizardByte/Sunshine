<template>
  <div class="advanced-commands">
    <!-- Header -->
    <div class="d-flex justify-content-between align-items-center mb-4">
      <div>
        <h5>{{ appMode ? $t('apps.cmd_prep_name') : $t('commands.advanced_title') }}</h5>
        <div class="form-text">{{ appMode ? $t('commands.event_actions_desc') : $t('commands.advanced_desc') }}</div>
      </div>
      <div class="d-flex gap-2">
        <button class="btn btn-outline-secondary btn-sm" @click="showLegacyMigration = !showLegacyMigration"
          v-if="hasLegacyCommands">
          <i class="fas fa-arrow-up"></i> {{ $t('commands.migrate_legacy') }}
        </button>
      </div>
    </div>

    <!-- Legacy Migration Panel -->
    <div v-if="showLegacyMigration" class="alert alert-info mb-4">
      <h6><i class="fas fa-info-circle"></i> {{ $t('commands.migrate_legacy_title') }}</h6>
      <p>{{ $t('commands.migrate_legacy_desc') }}</p>
      <div class="d-flex gap-2">
        <button class="btn btn-primary btn-sm" @click="migrateLegacyCommands">
          <i class="fas fa-magic"></i> {{ $t('commands.migrate_now') }}
        </button>
        <button class="btn btn-secondary btn-sm" @click="showLegacyMigration = false">
          {{ $t('_common.cancel') }}
        </button>
      </div>
    </div>

    <!-- Basic Commands (Default View) -->
    <div v-if="!isAdvancedMode" class="basic-view">
      <div class="card">
        <div class="card-header">
          <div class="d-flex align-items-center">
            <i class="fas fa-terminal me-2"></i>
            <h6 class="mb-0">{{ $t('commands.basic_prep_commands') }}</h6>
          </div>
          <small>{{ $t('commands.basic_prep_desc') }}</small>
        </div>
        <div class="card-body">
          <!-- No commands state -->
          <div v-if="basicCommands.length === 0" class="text-center py-4">
            <div class="text-muted mb-3">
              <i class="fas fa-terminal fa-2x"></i>
              <div class="mt-2">{{ $t('commands.no_basic_commands') }}</div>
              <small>{{ $t('commands.no_basic_commands_desc') }}</small>
            </div>
            <button class="btn btn-primary btn-sm" @click="addBasicCommand">
              <i class="fas fa-plus"></i> {{ $t('commands.add_first_command') }}
            </button>
          </div>

          <!-- Commands list -->
          <div v-else>
            <div v-for="(command, index) in basicCommands" :key="index" class="basic-command-item mb-3">
              <div class="card">
                <div class="card-body">
                  <div class="row g-3">
                    <!-- Do Command -->
                    <div class="col-md-6">
                      <div class="d-flex align-items-center mb-2">
                        <i class="fas fa-play text-success me-2"></i>
                        <label class="form-label small mb-0">{{ $t('commands.do_command') }}</label>
                      </div>
                      <textarea :value="command.do" class="form-control monospace" rows="2"
                        @input="updateBasicCommand(index, 'do', $event.target.value)"
                        :placeholder="getCommandPlaceholder()"></textarea>
                      <div class="form-text">{{ $t('commands.prestream_desc') }}</div>
                    </div>
                    <!-- Undo Command -->
                    <div class="col-md-6">
                      <div class="d-flex align-items-center mb-2">
                        <i class="fas fa-undo text-secondary me-2"></i>
                        <label class="form-label small mb-0">{{ $t('commands.undo_command') }}</label>
                      </div>
                      <textarea :value="command.undo" class="form-control monospace" rows="2"
                        @input="updateBasicCommand(index, 'undo', $event.target.value)"
                        :placeholder="getUndoPlaceholder()"></textarea>
                      <div class="form-text">{{ $t('commands.poststream_desc') }}</div>
                    </div>
                  </div>

                  <!-- Elevation and Actions -->
                  <div class="row g-3 mt-2">
                    <div class="col-md-6" v-if="platform === 'windows'">
                      <div class="form-check">
                        <input class="form-check-input" type="checkbox" :id="'elevated-' + index"
                          :checked="command.elevated"
                          @change="updateBasicCommand(index, 'elevated', $event.target.checked)">
                        <label class="form-check-label" :for="'elevated-' + index">
                          {{ $t('commands.run_elevated') }}
                        </label>
                      </div>
                    </div>
                    <div class="col-md-6">
                      <div class="d-flex justify-content-end gap-2">
                        <button class="btn btn-outline-secondary btn-sm" @click="moveBasicCommand(index, -1)"
                          :disabled="index === 0" :title="$t('commands.move_up')">
                          <i class="fas fa-arrow-up"></i>
                        </button>
                        <button class="btn btn-outline-secondary btn-sm" @click="moveBasicCommand(index, 1)"
                          :disabled="index === basicCommands.length - 1" :title="$t('commands.move_down')">
                          <i class="fas fa-arrow-down"></i>
                        </button>
                        <button class="btn btn-outline-danger btn-sm" @click="removeBasicCommand(index)"
                          :title="$t('commands.remove_command')">
                          <i class="fas fa-trash"></i>
                        </button>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <!-- Add new command button -->
            <div class="text-center mt-3">
              <button class="btn btn-primary btn-sm" @click="addBasicCommand">
                <i class="fas fa-plus"></i> {{ $t('commands.add_command') }}
              </button>
            </div>
          </div>

          <!-- Advanced mode switch -->
          <div class="mt-4 text-center">
            <button class="btn btn-outline-secondary btn-sm" @click="switchToAdvancedMode">
              <i class="fas fa-cogs"></i> {{ $t('commands.need_more_control') }}
            </button>
          </div>
        </div>
      </div>
    </div>

    <!-- Advanced Mode Button (when already in advanced mode) -->
    <div v-else class="advanced-mode-button">
      <div class="text-center">
        <button class="btn btn-primary" @click="showAdvancedModal = true">
          <i class="fas fa-cogs"></i> {{ $t('commands.view_advanced') }}
        </button>
        <div class="form-text mt-2">{{ $t('commands.migrate_legacy_desc') }}</div>
      </div>
    </div>

    <!-- Advanced Commands Modal -->
    <div v-if="showAdvancedModal" class="modal d-block" tabindex="-1" style="background-color: rgba(0,0,0,0.5);">
      <div class="modal-dialog modal-xl" style="max-width: 75%; width: 75%;">
        <div class="modal-content">
          <div class="modal-header">
            <h5 class="modal-title">
              <i class="fas fa-cogs me-2"></i>{{ $t('commands.view_advanced') }}
            </h5>
            <div class="d-flex gap-2">
              <button class="btn btn-outline-secondary btn-sm" @click="switchToBasicMode">
                <i class="fas fa-arrow-left"></i> {{ $t('commands.view_simple') }}
              </button>
              <button type="button" class="btn-close" @click="showAdvancedModal = false"></button>
            </div>
          </div>
          <div class="modal-body">
            <div class="advanced-commands-view">
              <div class="commands-container">
                <!-- Start Commands Section -->
                <div class="commands-section">
                  <div class="section-card">
                    <div class="section-header start-header">
                      <div class="header-content">
                        <i class="fas fa-play-circle"></i>
                        <h6>{{ $t('commands.startup_commands') }}</h6>
                      </div>
                      <div class="header-description">{{ $t('commands.stage_startup_desc') }}</div>
                    </div>
                    <div class="section-body">
                      <CommandGroupEditor 
                        :groups="getAllStartStageGroups()" 
                        @update="updateAllStartStageGroups($event)"
                        :available-stages="startStages" 
                        :platform="platform" 
                        :simple-mode="false" 
                        :section="'start'" 
                      />
                    </div>
                  </div>
                </div>

                <!-- Cleanup Commands Section -->
                <div class="commands-section">
                  <div class="section-card">
                    <div class="section-header cleanup-header">
                      <div class="header-content">
                        <i class="fas fa-stop-circle"></i>
                        <h6>{{ $t('commands.shutdown_commands') }}</h6>
                      </div>
                      <div class="header-description">{{ $t('commands.stage_cleanup_desc') }}</div>
                    </div>
                    <div class="section-body">
                      <CommandGroupEditor 
                        :groups="getAllCleanupStageGroups()" 
                        @update="updateAllCleanupStageGroups($event)"
                        :available-stages="cleanupStages" 
                        :platform="platform" 
                        :simple-mode="false" 
                        :section="'cleanup'" 
                      />
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
          <div class="modal-footer">
            <button type="button" class="btn btn-secondary" @click="showAdvancedModal = false">
              {{ $t('_common.close') }}
            </button>
          </div>
        </div>
      </div>
    </div>

  </div>
</template>

<script setup>
import { defineProps, defineEmits } from 'vue'
import CommandGroupEditor from './CommandGroupEditor.vue'
import { useAdvancedCommands } from './AdvancedCommands.js'

const props = defineProps({
  modelValue: {
    type: Object,
    default: () => ({})
  },
  platform: String,
  legacyCommands: {
    type: Array,
    default: () => []
  },
  appMode: {
    type: Boolean,
    default: false
  }
})

const emit = defineEmits(['update:modelValue'])

const {
  showLegacyMigration,
  isAdvancedMode,
  showAdvancedModal,
  startStages,
  cleanupStages,
  hasLegacyCommands,
  commandsData,
  hasAnyCommands,
  basicCommands,
  getAllStartStageGroups,
  getAllCleanupStageGroups,
  updateAllStartStageGroups,
  updateAllCleanupStageGroups,
  migrateLegacyCommands,
  addBasicCommand,
  removeBasicCommand,
  updateBasicCommand,
  moveBasicCommand,
  getCommandPlaceholder,
  getUndoPlaceholder,
  switchToAdvancedMode,
  switchToBasicMode
} = useAdvancedCommands(props, emit)
</script>

<style scoped>
/* Modal styles */
.modal {
  z-index: 1055;
}

.modal-xl {
  max-width: 75% !important;
  width: 75% !important;
}

.advanced-mode-button {
  text-align: center;
  padding: 2rem;
  border: 2px dashed #dee2e6;
  border-radius: 0.5rem;
  background-color: #f8f9fa;
}

.advanced-mode-button:hover {
  border-color: #007bff;
  background-color: #f0f8ff;
}

/* Basic command styles */
.stage-pair-card {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  height: 100%;
}

.stage-card {
  border: 2px solid #e9ecef;
  border-radius: 0.5rem;
  padding: 1rem;
  cursor: pointer;
  transition: all 0.2s ease;
  display: flex;
  flex-direction: column;
  flex: 1;
}

.stage-card:hover {
  border-color: #007bff;
  box-shadow: 0 2px 4px rgba(0, 123, 255, 0.1);
}

.stage-card.has-commands {
  border-color: #007bff;
  background-color: #f8f9ff;
}

.stage-card.setup {
  border-left: 4px solid #007bff;
}

.stage-card.teardown {
  border-left: 4px solid #6c757d;
}

.stage-card.teardown:hover {
  border-color: #6c757d;
  box-shadow: 0 2px 4px rgba(108, 117, 125, 0.1);
}

.stage-card.teardown.has-commands {
  border-color: #6c757d;
  background-color: #f8f9fa;
}

.stage-card.advanced {
  border-color: #6c757d;
}

.stage-card.advanced:hover {
  border-color: #495057;
}

.stage-card.advanced.has-commands {
  border-color: #495057;
  background-color: #f8f9fa;
}

.stage-header {
  display: flex;
  align-items: flex-start;
  flex-grow: 1;
  cursor: pointer;
}

.stage-icon {
  margin-right: 0.75rem;
  font-size: 1.25rem;
  margin-top: 0.125rem;
}

.stage-info {
  flex-grow: 1;
}

.stage-actions {
  margin-top: 1rem;
  text-align: center;
}

.simple-view .card {
  height: 100%;
}

.beginner-view .border {
  transition: all 0.2s ease;
}

.beginner-view .border:hover {
  border-color: #007bff !important;
  box-shadow: 0 2px 4px rgba(0, 123, 255, 0.1);
}

.btn-check:checked+.btn {
  background-color: #007bff;
  border-color: #007bff;
  color: white;
}

.card-header.bg-primary {
  background-color: #007bff !important;
}

.card-header.bg-secondary {
  background-color: #6c757d !important;
}

.basic-command-item .card {
  border: 1px solid #e9ecef;
  transition: all 0.2s ease;
}

.basic-command-item .card:hover {
  border-color: #007bff;
  box-shadow: 0 2px 4px rgba(0, 123, 255, 0.1);
}

.basic-view .monospace {
  font-family: 'Courier New', monospace;
  font-size: 0.9em;
}

.basic-view .form-label.small {
  font-size: 0.875em;
  font-weight: 600;
  margin-bottom: 0.25rem;
}

/* Advanced Commands View */
.advanced-commands-view {
  width: 100%;
  margin: 0;
  padding: 0;
}

.commands-container {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 2rem;
  width: 100%;
  align-items: start;
}

.commands-section {
  display: flex;
  flex-direction: column;
  min-width: 0;
  width: 100%;
}

.section-card {
  display: flex;
  flex-direction: column;
  background: #ffffff;
  border: 1px solid #e9ecef;
  border-radius: 0.75rem;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.05);
  overflow: hidden;
  min-height: 500px;
  width: 100%;
}

.section-header {
  width: 100%;
  box-sizing: border-box;
  padding: 1.25rem 1.5rem;
  color: white;
  background: linear-gradient(135deg, #007bff 0%, #0056b3 100%);
}

.section-header.cleanup-header {
  background: linear-gradient(135deg, #6c757d 0%, #495057 100%);
}

.header-content {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  margin-bottom: 0.5rem;
}

.header-content i {
  font-size: 1.25rem;
}

.header-content h6 {
  margin: 0;
  font-size: 1.1rem;
  font-weight: 600;
}

.header-description {
  font-size: 0.9rem;
  opacity: 0.9;
  margin: 0;
}

.section-body {
  flex: 1;
  padding: 1.5rem;
  display: flex;
  flex-direction: column;
  min-height: 400px;
}

/* CommandGroupEditor Container Styles */
.section-body :deep(.command-group-editor) {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
  width: 100%;
  min-width: 0;
}

.section-body :deep(.cg-group-card) {
  background: #f8f9fa;
  border: 1px solid #dee2e6;
  border-radius: 0.5rem;
  padding: 1.25rem;
  margin-bottom: 1rem;
  width: 100%;
  box-sizing: border-box;
}

.section-body :deep(.cg-header) {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 0.75rem;
  margin-bottom: 1rem;
  font-weight: 600;
  color: #495057;
}

.section-body :deep(.cg-row) {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
  margin-bottom: 1rem;
  padding: 0.75rem 0;
  border-bottom: 1px solid #e9ecef;
}

.section-body :deep(.cg-row:last-child) {
  border-bottom: none;
  margin-bottom: 0;
}

.section-body :deep(.cg-cell) {
  display: flex;
  align-items: flex-start;
  gap: 0.75rem;
  margin-bottom: 0.5rem;
}

.section-body :deep(.cg-cell > *) {
  min-width: 0;
}

.section-body :deep(.cg-cell input),
.section-body :deep(.cg-cell select),
.section-body :deep(.cg-cell textarea) {
  flex: 1;
  min-height: 38px;
  border: 1px solid #ced4da;
  border-radius: 0.375rem;
  padding: 0.5rem 0.75rem;
  font-size: 0.9rem;
}

.section-body :deep(.cg-cell textarea) {
  resize: vertical;
  min-height: 76px;
  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
}

.section-body :deep(.cg-cell .btn) {
  flex-shrink: 0;
  min-height: 38px;
  padding: 0.5rem 0.75rem;
}

.section-body :deep(.dropdown-menu) {
  z-index: 1050;
  min-width: 200px;
  border: 1px solid #ced4da;
  border-radius: 0.375rem;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}

/* Responsive Design */
@media (max-width: 1200px) {
  .commands-container {
    grid-template-columns: 1fr;
    gap: 1.5rem;
  }
  
  .section-card {
    min-height: 400px;
  }
  
  .section-body {
    padding: 1.25rem;
    min-height: 300px;
  }
}

@media (max-width: 768px) {
  .commands-container {
    gap: 1rem;
  }
  
  .section-header {
    padding: 1rem;
  }
  
  .section-body {
    padding: 1rem;
    min-height: 250px;
  }
  
  .section-body :deep(.cg-group-card) {
    padding: 1rem;
  }
  
  .section-body :deep(.cg-cell) {
    flex-direction: column;
    gap: 0.5rem;
  }
  
  .section-body :deep(.cg-cell > *) {
    width: 100%;
  }
  
  .header-content {
    gap: 0.5rem;
  }
  
  .header-content h6 {
    font-size: 1rem;
  }
}

@media (max-width: 480px) {
  .section-header {
    padding: 0.75rem;
  }
  
  .section-body {
    padding: 0.75rem;
  }
  
  .section-body :deep(.cg-group-card) {
    padding: 0.75rem;
  }
  
  .header-content i {
    font-size: 1.1rem;
  }
  
  .header-content h6 {
    font-size: 0.95rem;
  }
  
  .header-description {
    font-size: 0.85rem;
  }
}
</style>
