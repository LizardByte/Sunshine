<template>
  <div class="command-group-editor">
    <!-- Empty State -->
    <div v-if="localGroups.length === 0" class="empty-state-container">
      <div class="empty-state">
        <div class="empty-state-icon">
          <i class="fas fa-layer-group"></i>
        </div>
        <h5 class="empty-state-title">{{ $t('commands.no_groups') }}</h5>
        <p class="empty-state-text">{{ $t('commands.no_groups_desc') }}</p>
        <button class="btn btn-primary btn-lg" @click="addGroup">
          <i class="fas fa-plus me-2"></i>{{ $t('commands.add_group') }}
        </button>
      </div>
    </div>

    <!-- Groups Container -->
    <div v-else class="groups-container">
      <div v-for="(group, groupIndex) in localGroups" :key="`group-${groupIndex}`" class="command-group"
        :class="{ 'group-expanded': isGroupExpanded(groupIndex) }">
        <div class="group-card">
          <!-- Group Header -->
          <div class="group-header">
            <div class="group-title-section">
              <div class="group-expand-btn">
                <button class="btn btn-link p-0 text-decoration-none" @click="toggleGroupExpanded(groupIndex)"
                  :aria-expanded="isGroupExpanded(groupIndex)"
                  :aria-label="isGroupExpanded(groupIndex) ? $t('commands.collapse') : $t('commands.expand')"
                  :title="isGroupExpanded(groupIndex) ? 'Collapse group' : 'Expand group'">
                  <i class="fas expand-icon"
                    :class="isGroupExpanded(groupIndex) ? 'fa-caret-down' : 'fa-caret-right'"></i>
                </button>
              </div>
              <div class="group-title-input">
                <input v-model="group.name" class="form-control group-name-input"
                  @input="updateGroup(groupIndex, 'name', group.name)"
                  :placeholder="$t('commands.group_name_placeholder')" :aria-label="$t('commands.group_name')" 
                  :title="group.name" />
              </div>
              <div class="group-info">
                <span class="commands-count" v-if="group.commands?.length > 0" :title="formatCommandsCount(group.commands?.length)">
                  <i class="fas fa-terminal me-1"></i>
                  <span class="count-number">{{ formatCommandsCount(group.commands?.length) }}</span>
                </span>
              </div>
            </div>

            <div class="group-actions">
              <div class="btn-group" role="group" :aria-label="$t('commands.group_actions')">
                <button class="btn btn-outline-success btn-sm" @click="addCommand(groupIndex)"
                  :title="$t('commands.add_command')" :aria-label="$t('commands.add_command')">
                  <i class="fas fa-plus"></i>
                </button>
                <div class="dropdown">
                  <button class="btn btn-outline-secondary btn-sm dropdown-toggle" 
                    type="button" 
                    data-bs-toggle="dropdown" 
                    aria-expanded="false"
                    :title="'More actions'"
                    :aria-label="'More actions'">
                    <i class="fas fa-ellipsis-v"></i>
                  </button>
                  <ul class="dropdown-menu dropdown-menu-end">
                    <li>
                      <button class="dropdown-item text-danger" @click="removeGroup(groupIndex)">
                        <i class="fas fa-trash me-2"></i>{{ $t('commands.remove_group') }}
                      </button>
                    </li>
                  </ul>
                </div>
              </div>
            </div>
          </div>

          <!-- Group Configuration -->
          <div class="group-config" v-if="isGroupExpanded(groupIndex)">
            <div class="row g-4">
              <div class="col-md-6" v-if="availableStages.length > 0">
                <label class="form-label config-label">
                  <i class="fas fa-clock me-2"></i>Execution stage
                </label>
                <select v-model="group.stage" class="form-select"
                  @change="updateGroup(groupIndex, 'stage', group.stage)"
                  :aria-describedby="`stage-help-${groupIndex}`">
                  <option value="" disabled>Select when to run</option>
                  <option v-for="stage in availableStages" :key="stage.id" :value="stage.id">
                    {{ getAllEnumDisplayName(stage.id, 'stage') }}
                  </option>
                </select>
                <div class="form-text form-help-text" v-if="group.stage" :id="`stage-help-${groupIndex}`">
                  {{ getStageDescription(group.stage) }}
                </div>
              </div>
              <div class="col-md-6" v-if="!simpleMode">
                <label class="form-label config-label">
                  <i class="fas fa-exclamation-triangle me-2"></i>Failure policy
                </label>
                <select v-model="group.failure_policy" class="form-select"
                  @change="updateGroup(groupIndex, 'failure_policy', group.failure_policy)"
                  :aria-describedby="`policy-help-${groupIndex}`">
                  <option value="FAIL_FAST">{{ getAllEnumDisplayName('FAIL_FAST', 'policy') }}</option>
                  <option value="CONTINUE_ON_FAILURE">{{ getAllEnumDisplayName('CONTINUE_ON_FAILURE', 'policy') }}</option>
                  <option value="FAIL_STAGE_ON_ANY">{{ getAllEnumDisplayName('FAIL_STAGE_ON_ANY', 'policy') }}</option>
                </select>
                <div class="form-text form-help-text" :id="`policy-help-${groupIndex}`">
                  {{ getPolicyDescription(group.failure_policy) }}
                </div>
              </div>
            </div>
          </div>

          <!-- Commands List -->
          <div v-if="isGroupExpanded(groupIndex)" class="commands-container">
            <!-- Empty State -->
            <div v-if="!group.commands || group.commands.length === 0" class="empty-state text-center py-5">
              <div class="empty-state-icon mb-3">
                <i class="fas fa-terminal fa-3x text-muted"></i>
              </div>
              <h6 class="text-muted mb-2">{{ $t('commands.no_commands_in_group') }}</h6>
              <p class="text-muted small mb-4">{{ $t('commands.add_command_help') }}</p>
              <button class="btn btn-primary" @click="addCommand(groupIndex)"
                :aria-label="$t('commands.add_first_command')">
                <i class="fas fa-plus me-2"></i>{{ $t('commands.add_first_command') }}
              </button>
            </div>

            <!-- Commands -->
            <div v-else class="commands-list">
              <div v-for="(command, commandIndex) in group.commands" :key="`cmd-${groupIndex}-${commandIndex}`"
                class="command-item" :class="{ 'command-item-dragging': false }">
                <div class="command-card">
                  <div class="command-header">
                    <span class="command-number">{{ commandIndex + 1 }}</span>
                    <div class="command-actions ms-auto">
                      <div class="btn-group" role="group" :aria-label="$t('commands.command_actions')">
                        <button v-if="group.commands.length > 1" class="btn btn-sm btn-outline-light" 
                          @click="moveCommand(groupIndex, commandIndex, -1)"
                          :disabled="commandIndex === 0" :title="$t('commands.move_up')"
                          :aria-label="$t('commands.move_up')">
                          <i class="fas fa-chevron-up"></i>
                        </button>
                        <button v-if="group.commands.length > 1" class="btn btn-sm btn-outline-light" 
                          @click="moveCommand(groupIndex, commandIndex, 1)"
                          :disabled="commandIndex === group.commands.length - 1" :title="$t('commands.move_down')"
                          :aria-label="$t('commands.move_down')">
                          <i class="fas fa-chevron-down"></i>
                        </button>
                        <button class="btn btn-sm btn-outline-light" @click="command.showHelp = !command.showHelp"
                          :title="$t('commands.show_examples')" :aria-label="$t('commands.show_examples')"
                          :class="{ 'active': command.showHelp }">
                          <i class="fas fa-question-circle"></i>
                        </button>
                        <div class="dropdown">
                          <button class="btn btn-sm btn-outline-light dropdown-toggle" 
                            type="button" 
                            data-bs-toggle="dropdown" 
                            aria-expanded="false"
                            :title="'More actions'"
                            :aria-label="'More actions'">
                            <i class="fas fa-ellipsis-v"></i>
                          </button>
                          <ul class="dropdown-menu dropdown-menu-end">
                            <li>
                              <button class="dropdown-item text-danger" @click="removeCommand(groupIndex, commandIndex)">
                                <i class="fas fa-trash me-2"></i>{{ $t('commands.remove_command') }}
                              </button>
                            </li>
                          </ul>
                        </div>
                      </div>
                    </div>
                  </div>

                  <div class="command-body">
                    <div class="row g-4">
                      <!-- Command Input -->
                      <div class="col-12">
                        <label class="form-label command-label">
                          <i class="fas fa-terminal me-2"></i>Command
                        </label>
                        <input v-model="command.cmd" type="text" class="form-control command-input"
                          @input="updateCommand(groupIndex, commandIndex, 'cmd', command.cmd)"
                          :placeholder="getCommandPlaceholder()" required
                          :aria-describedby="`cmd-help-${groupIndex}-${commandIndex}`" />
                      </div>

                      <!-- Options Section -->
                      <div class="col-12">
                        <div class="options-section">
                          <h6 class="options-heading">
                            <i class="fas fa-cog me-2"></i>Options
                          </h6>
                          
                          <!-- Timeout Row -->
                          <div class="row g-3 mb-3">
                            <div class="col-md-4">
                              <label class="form-label command-label">
                                <i class="fas fa-clock me-2"></i>Timeout
                              </label>
                              <div class="input-group">
                                <input v-model.number="command.timeout_seconds" type="number"
                                  @input="updateCommand(groupIndex, commandIndex, 'timeout_seconds', command.timeout_seconds)"
                                  class="form-control" min="1" max="3600" placeholder="30" />
                                <span class="input-group-text">sec</span>
                              </div>
                            </div>
                          </div>
                          
                          <!-- Behavior Row -->
                          <div class="row">
                            <div class="col-12">
                              <label class="form-label command-label">
                                <i class="fas fa-sliders-h me-2"></i>Behavior
                              </label>
                              <div class="options-grid">
                                <div v-if="platform === 'windows'" class="form-check">
                                  <input v-model="command.elevated" class="form-check-input" type="checkbox"
                                    @change="updateCommand(groupIndex, commandIndex, 'elevated', command.elevated)"
                                    :id="`elevated-${groupIndex}-${commandIndex}`" 
                                    title="Run with administrator privileges" />
                                  <label class="form-check-label" :for="`elevated-${groupIndex}-${commandIndex}`">
                                    <i class="fas fa-shield-alt me-1"></i>Run as administrator
                                  </label>
                                </div>
                                <div class="form-check">
                                  <input v-model="command.ignore_error" class="form-check-input" type="checkbox"
                                    @change="updateCommand(groupIndex, commandIndex, 'ignore_error', command.ignore_error)"
                                    :id="`ignore-error-${groupIndex}-${commandIndex}`"
                                    title="Continue even if this command fails" />
                                  <label class="form-check-label" :for="`ignore-error-${groupIndex}-${commandIndex}`">
                                    <i class="fas fa-exclamation-triangle me-1"></i>Ignore errors
                                  </label>
                                </div>
                                <div class="form-check">
                                  <input v-model="command.async" class="form-check-input" type="checkbox"
                                    @change="updateCommand(groupIndex, commandIndex, 'async', command.async)"
                                    :id="`async-${groupIndex}-${commandIndex}`"
                                    title="Run this command in the background without waiting for it to finish" />
                                  <label class="form-check-label" :for="`async-${groupIndex}-${commandIndex}`">
                                    <i class="fas fa-rocket me-1"></i>Run asynchronously
                                  </label>
                                </div>
                              </div>
                            </div>
                          </div>
                        </div>
                      </div>
                    </div>

                    <!-- Command Help Examples -->
                    <div v-if="command.showHelp" class="command-help mt-3"
                      :id="`cmd-help-${groupIndex}-${commandIndex}`">
                      <div class="help-header">
                        <h6 class="help-title">
                          <i class="fas fa-lightbulb me-2"></i>{{ $t('commands.example_commands') }}
                        </h6>
                      </div>
                      <div class="help-content">
                        <div class="row g-3">
                          <div class="col-md-6" v-if="platform === 'windows'">
                            <div class="help-section">
                              <h6 class="help-section-title">Windows Commands</h6>
                              <div class="help-examples">
                                <div class="help-example">
                                  <code>taskkill /f /im "notepad.exe"</code>
                                  <small class="help-description">Close application before streaming</small>
                                </div>
                                <div class="help-example">
                                  <code>net stop "Windows Audio"</code>
                                  <small class="help-description">Stop system service</small>
                                </div>
                                <div class="help-example">
                                  <code>powershell Set-DisplayResolution 1920 1080</code>
                                  <small class="help-description">Change display resolution</small>
                                </div>
                              </div>
                            </div>
                          </div>
                          <div class="col-md-6" v-else>
                            <div class="help-section">
                              <h6 class="help-section-title">Linux Commands</h6>
                              <div class="help-examples">
                                <div class="help-example">
                                  <code>pkill firefox</code>
                                  <small class="help-description">Close application before streaming</small>
                                </div>
                                <div class="help-example">
                                  <code>systemctl stop pulseaudio</code>
                                  <small class="help-description">Stop system service</small>
                                </div>
                                <div class="help-example">
                                  <code>xrandr --output HDMI-1 --mode 1920x1080</code>
                                  <small class="help-description">Change display resolution</small>
                                </div>
                              </div>
                            </div>
                          </div>
                        </div>
                        <div class="help-tip">
                          <i class="fas fa-info-circle me-2"></i>
                          <strong>Tip:</strong> Configure teardown commands in separate stages to reverse setup
                          operations automatically.
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              </div>

              <!-- Add Command Button -->
              <div class="add-command-section text-center">
                <button class="btn btn-outline-primary" @click="addCommand(groupIndex)"
                  :aria-label="$t('commands.add_command')">
                  <i class="fas fa-plus me-2"></i>{{ $t('commands.add_command') }}
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Add Group Button -->
    <div class="text-center add-group-section">
      <button class="btn btn-success" @click="addGroup" :aria-label="$t('commands.add_group')">
        <i class="fas fa-plus me-2"></i>{{ $t('commands.add_group') }}
      </button>
    </div>
  </div>
</template>
<script setup>
import { defineProps, defineEmits } from 'vue'
import { useCommandGroupEditor } from './CommandGroupEditor.js'

const props = defineProps({
  groups: {
    type: Array,
    default: () => []
  },
  stage: String,
  simpleMode: {
    type: Boolean,
    default: false
  },
  platform: String,
  availableStages: {
    type: Array,
    default: () => []
  },
  section: {
    type: String,
    default: ''
  }
})

const emit = defineEmits(['update'])

const {
  expandedGroups,
  showCommandHelp,
  localGroups,
  updateGroup,
  updateCommand,
  getStageDescription,
  addGroup,
  removeGroup,
  addCommand,
  removeCommand,
  moveCommand,
  toggleGroupExpanded,
  isGroupExpanded,
  getStageDisplayName,
  getDefaultFailurePolicy,
  getCommandPlaceholder,
  getPolicyDescription,
  getFriendlyStageNames,
  getFriendlyPolicyNames,
  formatCommandsCount,
  getPolicyDisplayName,
  getAllEnumDisplayName
} = useCommandGroupEditor(props, emit)
</script>

<style scoped>
/* Main Container */
.command-group-editor {
  max-width: 100%;
  margin: 0 auto;
}

/* Empty State Styling */
.empty-state-container {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 300px;
  padding: 2rem;
}

.empty-state {
  text-align: center;
  max-width: 400px;
}

.empty-state-icon {
  font-size: 4rem;
  color: #6c757d;
  margin-bottom: 1.5rem;
}

.empty-state-title {
  color: #495057;
  font-weight: 600;
  margin-bottom: 0.75rem;
}

.empty-state-text {
  color: #6c757d;
  margin-bottom: 2rem;
  line-height: 1.5;
}

/* Groups Container */
.groups-container {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

/* Group Card Styling */
.command-group {
  border: 1px solid #e9ecef;
  border-radius: 12px;
  background: #ffffff;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.05);
  transition: all 0.3s ease;
  overflow: hidden;
  margin-bottom: 1.5rem;
}

.command-group:hover {
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.08);
  border-color: #007bff;
}

.group-expanded {
  box-shadow: 0 8px 25px rgba(0, 123, 255, 0.15);
  border-color: #007bff;
}

.group-card {
  width: 100%;
}

/* Group Header */
.group-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1.5rem 2rem;
  background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%);
  border-bottom: 1px solid #dee2e6;
  min-height: 80px;
}

.group-title-section {
  display: flex;
  align-items: center;
  gap: 1rem;
  flex: 1;
}

.group-expand-btn .btn {
  color: #007bff;
  font-size: 1.2rem;
  padding: 0.5rem;
  width: 40px;
  height: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 50%;
  background: rgba(0, 123, 255, 0.1);
  border: none;
  transition: all 0.2s ease;
}

.group-expand-btn .btn:hover {
  color: #0056b3;
  background: rgba(0, 123, 255, 0.2);
  transform: scale(1.1);
}

.expand-icon {
  transition: transform 0.3s ease;
}

.group-title-input {
  flex: 1;
  max-width: 350px;
}

.group-name-input {
  border: 2px solid rgba(255, 255, 255, 0.8);
  background: rgba(255, 255, 255, 0.95);
  font-weight: 600;
  font-size: 1.1rem;
  padding: 0.75rem 1rem;
  border-radius: 8px;
  transition: all 0.2s ease;
  min-width: 200px;
  max-width: 350px;
}

.group-name-input:focus {
  background: #ffffff;
  box-shadow: 0 0 0 4px rgba(0, 123, 255, 0.25);
  border-color: #007bff;
  transform: scale(1.02);
}

.group-info {
  margin-left: auto;
  margin-right: 1rem;
}

.commands-count {
  background: linear-gradient(135deg, #007bff, #0056b3);
  color: white;
  padding: 0.4rem 1rem;
  border-radius: 20px;
  font-size: 0.875rem;
  font-weight: 600;
  box-shadow: 0 2px 4px rgba(0, 123, 255, 0.3);
  transition: all 0.3s ease;
}

.count-number {
  transition: all 0.3s ease;
}

.group-actions .btn-group {
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.15);
  border-radius: 8px;
  overflow: hidden;
}

.group-actions .btn {
  border: none;
  padding: 0.6rem 1rem;
  font-weight: 600;
  transition: all 0.2s ease;
}

.group-actions .btn-outline-success {
  background: linear-gradient(135deg, #28a745, #218838);
  color: white;
  border: none;
  border-radius: 8px;
}

.group-actions .btn-outline-success:hover {
  background: linear-gradient(135deg, #218838, #1e7e34);
  transform: translateY(-1px);
}

.group-actions .btn-outline-secondary {
  background: #f8f9fa;
  color: #6c757d;
  border: 1px solid #dee2e6;
  border-radius: 8px;
}

.group-actions .btn-outline-secondary:hover {
  background: #e9ecef;
  color: #495057;
  transform: translateY(-1px);
}

/* Group Configuration */
.group-config {
  padding: 2rem;
  background: linear-gradient(135deg, #f8f9fa 0%, #ffffff 100%);
  border-bottom: 1px solid #dee2e6;
}

.config-label {
  font-weight: 600;
  color: #495057;
  margin-bottom: 0.5rem;
  display: flex;
  align-items: center;
  font-size: 0.95rem;
}

.config-label i {
  color: #007bff;
  font-size: 1rem;
}

/* Commands Container */
.commands-container {
  padding: 2rem;
  background: #fafbfc;
}

/* Command Items */
.commands-list {
  display: flex;
  flex-direction: column;
  gap: 1.25rem;
}

.command-item {
  transition: all 0.3s ease;
}

.command-card {
  border: 2px solid #f1f3f4;
  border-radius: 10px;
  background: #ffffff;
  transition: all 0.3s ease;
  overflow: hidden;
  box-shadow: 0 2px 6px rgba(0, 0, 0, 0.05);
}

.command-card:hover {
  border-color: #007bff;
  box-shadow: 0 4px 15px rgba(0, 123, 255, 0.15);
  transform: translateY(-1px);
}

.command-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 1rem 1.25rem;
  background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%);
  border-bottom: 1px solid #e9ecef;
}

.command-number {
  background: linear-gradient(135deg, #007bff, #0056b3);
  color: white;
  padding: 0.4rem 0.8rem;
  border-radius: 20px;
  font-weight: 700;
  font-size: 0.9rem;
  min-width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 2px 4px rgba(0, 123, 255, 0.3);
}

.command-actions .btn {
  border: 1px solid #dee2e6;
  transition: all 0.2s ease;
  margin: 0 1px;
  border-radius: 6px;
}

.command-actions .btn:hover {
  transform: translateY(-1px);
  box-shadow: 0 3px 8px rgba(0, 0, 0, 0.15);
}

.command-actions .btn-outline-light {
  background: #f8f9fa;
  border-color: #dee2e6;
  color: #6c757d;
}

.command-actions .btn-outline-light:hover {
  background: #e9ecef;
  color: #495057;
}

.command-actions .btn-outline-danger {
  background: #dc3545;
  color: white;
  border-color: #dc3545;
}

.command-actions .btn-outline-danger:hover {
  background: #c82333;
  border-color: #c82333;
}

.command-actions .btn.active {
  background: #007bff;
  color: white;
  border-color: #007bff;
}

.command-body {
  padding: 1.25rem;
}

.command-label {
  font-weight: 600;
  color: #495057;
  margin-bottom: 0.5rem;
  display: flex;
  align-items: center;
  font-size: 0.95rem;
}

.command-label i {
  color: #007bff;
  font-size: 1rem;
}

.command-input {
  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
  background: #f8f9fa;
  border: 2px solid #e9ecef;
  transition: all 0.2s ease;
  padding: 0.75rem 1rem;
  border-radius: 8px;
  font-size: 0.95rem;
}

.command-input:focus {
  background: #ffffff;
  border-color: #007bff;
  box-shadow: 0 0 0 4px rgba(0, 123, 255, 0.25);
  transform: scale(1.01);
}

/* Options Section */
.options-section {
  border-top: 1px solid #e9ecef;
  padding-top: 1.5rem;
  margin-top: 1rem;
}

.options-heading {
  font-weight: 600;
  color: #495057;
  margin-bottom: 1rem;
  padding-bottom: 0.5rem;
  border-bottom: 2px solid #e9ecef;
  display: flex;
  align-items: center;
  font-size: 1rem;
}

.options-heading i {
  color: #007bff;
  font-size: 1.1rem;
}

.options-grid {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

@media (min-width: 768px) {
  .options-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 0.75rem;
  }
}

/* Form Help Text with Better Contrast */
.form-help-text {
  color: #6b7280 !important;
  font-weight: 500;
}

/* Improved Button States */
.btn-outline-light:not(:disabled):not(.disabled) {
  background: #ffffff;
  border-color: #007bff;
  color: #007bff;
}

.btn-outline-light:not(:disabled):not(.disabled):hover {
  background: #007bff;
  color: white;
  transform: translateY(-1px);
}

/* Dropdown Styling */
.dropdown-menu {
  border: 1px solid #e9ecef;
  border-radius: 8px;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.15);
}

.dropdown-item.text-danger:hover {
  background-color: #dc3545;
  color: white;
}

/* Animation Classes */
@keyframes slideIn {
  from {
    opacity: 0;
    transform: translateY(-10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

@keyframes countUp {
  from {
    transform: scale(1.2);
  }
  to {
    transform: scale(1);
  }
}

.command-item {
  animation: slideIn 0.3s ease-out;
}

.count-number {
  animation: countUp 0.3s ease-out;
}

/* Input Group Styling */
.input-group-text {
  background: #f8f9fa;
  border-color: #e9ecef;
  color: #6c757d;
  font-weight: 600;
}

/* Form Check Styling */
.form-check {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.form-check-label {
  font-weight: 500;
  color: #495057;
  cursor: pointer;
  margin-bottom: 0;
  display: flex;
  align-items: center;
}

.form-check-input {
  margin: 0;
  cursor: pointer;
}

.form-check-input:checked {
  background-color: #007bff;
  border-color: #007bff;
}

/* Expand Icon Animation */
.expand-icon {
  transition: transform 0.2s ease;
  font-size: 1.2rem;
}

/* Commands Count Animation - Remove duplicate */

/* Improved Contrast for Disabled Buttons */
.btn:disabled,
.btn.disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.btn-outline-light:disabled,
.btn-outline-light.disabled {
  background: #f8f9fa;
  border-color: #e9ecef;
  color: #adb5bd;
}

/* Group Name Input Improvements - Remove duplicate */

/* Command Help */
.command-help {
  background: linear-gradient(135deg, #f8f9fa 0%, #ffffff 100%);
  border: 2px solid #e9ecef;
  border-radius: 10px;
  padding: 1.25rem;
  margin-top: 1.25rem;
}

.help-header {
  margin-bottom: 1rem;
}

.help-title {
  color: #495057;
  font-weight: 700;
  margin: 0;
  display: flex;
  align-items: center;
  font-size: 1rem;
}

.help-title i {
  color: #ffc107;
  margin-right: 0.5rem;
}

.help-section-title {
  color: #007bff;
  font-weight: 600;
  margin-bottom: 0.75rem;
  font-size: 0.95rem;
}

.help-examples {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.help-example {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.help-example code {
  background: #e9ecef;
  color: #495057;
  padding: 0.5rem 0.75rem;
  border-radius: 6px;
  font-family: 'Consolas', 'Monaco', monospace;
  font-size: 0.85rem;
  border: 1px solid #dee2e6;
}

.help-description {
  color: #6c757d;
  font-style: italic;
  margin-left: 0.5rem;
}

.help-tip {
  margin-top: 1rem;
  padding: 0.75rem 1rem;
  background: rgba(0, 123, 255, 0.1);
  border-left: 4px solid #007bff;
  border-radius: 6px;
  color: #495057;
  font-size: 0.9rem;
}

/* Add Sections */
.add-command-section {
  margin-top: 2rem;
  padding: 1.5rem;
  border-top: 1px solid #e9ecef;
  background: rgba(248, 249, 250, 0.5);
}

.add-group-section {
  margin-top: 2rem;
  padding: 1.5rem;
}

.add-command-section .btn,
.add-group-section .btn {
  padding: 0.75rem 2rem;
  font-weight: 700;
  border-radius: 8px;
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.15);
  transition: all 0.3s ease;
  font-size: 1rem;
}

.add-command-section .btn:hover,
.add-group-section .btn:hover {
  transform: translateY(-3px);
  box-shadow: 0 6px 20px rgba(0, 0, 0, 0.2);
}

.add-group-section .btn {
  background: linear-gradient(135deg, #28a745, #218838);
  border: none;
}

.add-group-section .btn:hover {
  box-shadow: 0 6px 20px rgba(40, 167, 69, 0.4);
}

/* Form Controls */
.form-select:focus,
.form-control:focus {
  border-color: #007bff;
  box-shadow: 0 0 0 4px rgba(0, 123, 255, 0.25);
}

.form-select,
.form-control {
  border: 2px solid #e9ecef;
  border-radius: 6px;
  transition: all 0.2s ease;
}

.form-check-input:checked {
  background-color: #007bff;
  border-color: #007bff;
}

/* Responsive Design */
@media (max-width: 768px) {
  .group-header {
    flex-direction: column;
    gap: 1rem;
    align-items: stretch;
    padding: 1.5rem 1rem;
  }

  .group-title-section {
    justify-content: center;
    gap: 0.75rem;
  }

  .group-name-input {
    min-width: auto;
    max-width: none;
  }

  .group-info {
    margin: 0;
    text-align: center;
  }

  .command-header {
    flex-direction: column;
    gap: 0.75rem;
    align-items: stretch;
    padding: 1rem;
  }

  .command-actions {
    justify-content: center;
  }

  .command-group-editor {
    padding: 0.5rem;
  }

  .group-config,
  .commands-container {
    padding: 1.5rem 1rem;
  }

  .options-grid {
    grid-template-columns: 1fr;
  }
}

/* Animation Classes */
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.3s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}

.slide-down-enter-active,
.slide-down-leave-active {
  transition: all 0.3s ease;
  max-height: 500px;
  overflow: hidden;
}

.slide-down-enter-from,
.slide-down-leave-to {
  max-height: 0;
  opacity: 0;
}

/* Bootstrap Overrides */
.btn-group>.btn {
  border-radius: 0;
}

.btn-group>.btn:first-child {
  border-top-left-radius: 8px;
  border-bottom-left-radius: 8px;
}

.btn-group>.btn:last-child {
  border-top-right-radius: 8px;
  border-bottom-right-radius: 8px;
}

/* Legacy Support */
.monospace {
  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
  font-size: 0.9em;
}

.form-label.small {
  font-size: 0.875em;
  font-weight: 600;
  margin-bottom: 0.25rem;
}

.bg-light {
  background-color: #f8f9fa !important;
}

code {
  font-size: 0.875em;
  color: #e83e8c;
  background-color: #f8f9fa;
  padding: 0.125rem 0.25rem;
  border-radius: 0.25rem;
}
</style>
