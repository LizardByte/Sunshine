<template>
  <div class="application-commands">
    <!-- Header with Mode Toggle -->
    <div class="d-flex justify-content-between align-items-center mb-3">
      <div>
        <h6>{{ $t('apps.cmd_prep_name') }}</h6>
        <div class="form-text small">{{ $t('apps.cmd_prep_desc') }}</div>
      </div>
      <div class="btn-group btn-group-sm" role="group">
        <input type="radio" class="btn-check" id="app-mode-basic" v-model="viewMode" value="basic">
        <label class="btn btn-outline-primary" for="app-mode-basic">
          <i class="fas fa-play"></i> {{ $t('commands.view_simple') }}
        </label>
        <input type="radio" class="btn-check" id="app-mode-advanced" v-model="viewMode" value="advanced">
        <label class="btn btn-outline-primary" for="app-mode-advanced">
          <i class="fas fa-cogs"></i> {{ $t('commands.view_advanced') }}
        </label>
      </div>
    </div>

    <!-- Basic Mode: Simple Do/Undo Interface -->
    <div v-if="viewMode === 'basic'" class="basic-view">
      <div class="row g-3">
        <div class="col-lg-6">
          <div class="card h-100">
            <div class="card-header bg-primary text-white">
              <div class="d-flex align-items-center">
                <i class="fas fa-play-circle me-2"></i>
                <h6 class="mb-0">{{ $t('_common.do_cmd') }}</h6>
              </div>
              <small>{{ $t('commands.stage_startup_desc') }}</small>
            </div>
            <div class="card-body">
              <div v-if="!hasDoCommands" class="text-center py-3">
                <i class="fas fa-plus-circle fa-2x text-muted mb-2"></i>
                <p class="text-muted">{{ $t('commands.add_command_help') }}</p>
                <button class="btn btn-primary btn-sm" @click="addDoCommand">
                  <i class="fas fa-plus"></i> {{ $t('commands.add_first_command') }}
                </button>
              </div>
              <div v-else>
                <div v-for="(command, index) in doCommands" :key="index" class="command-item mb-2">
                  <div class="input-group input-group-sm">
                    <input 
                      :value="command.cmd"
                      @input="updateDoCommand(index, 'cmd', $event.target.value)"
                      type="text" 
                      class="form-control monospace"
                      :placeholder="$t('commands.command_placeholder')"
                    />
                    <button 
                      class="btn btn-outline-danger"
                      @click="removeDoCommand(index)"
                      :title="$t('commands.remove_command')"
                    >
                      <i class="fas fa-trash"></i>
                    </button>
                  </div>
                  <div v-if="platform === 'windows'" class="form-check mt-1">
                    <input 
                      :checked="command.elevated"
                      @change="updateDoCommand(index, 'elevated', $event.target.checked)"
                      class="form-check-input"
                      type="checkbox" 
                      :id="`do-elevated-${index}`"
                    />
                    <label class="form-check-label small" :for="`do-elevated-${index}`">
                      <i class="fas fa-shield-alt"></i> {{ $t('_common.elevated') }}
                    </label>
                  </div>
                </div>
                <button class="btn btn-success btn-sm" @click="addDoCommand">
                  <i class="fas fa-plus"></i> {{ $t('commands.add_command') }}
                </button>
              </div>
            </div>
          </div>
        </div>
        
        <div class="col-lg-6">
          <div class="card h-100">
            <div class="card-header bg-secondary text-white">
              <div class="d-flex align-items-center">
                <i class="fas fa-undo me-2"></i>
                <h6 class="mb-0">{{ $t('commands.stage_cleanup') }}</h6>
              </div>
              <small>{{ $t('commands.stage_cleanup_desc') }}</small>
            </div>
            <div class="card-body">
              <div v-if="!hasUndoCommands" class="text-center py-3">
                <i class="fas fa-plus-circle fa-2x text-muted mb-2"></i>
                <p class="text-muted">{{ $t('commands.add_command_help') }}</p>
                <button class="btn btn-secondary btn-sm" @click="addUndoCommand">
                  <i class="fas fa-plus"></i> {{ $t('commands.add_first_command') }}
                </button>
              </div>
              <div v-else>
                <div v-for="(command, index) in undoCommands" :key="index" class="command-item mb-2">
                  <div class="input-group input-group-sm">
                    <input 
                      :value="command.cmd"
                      @input="updateUndoCommand(index, 'cmd', $event.target.value)"
                      type="text" 
                      class="form-control monospace"
                      :placeholder="$t('commands.command_placeholder')"
                    />
                    <button 
                      class="btn btn-outline-danger"
                      @click="removeUndoCommand(index)"
                      :title="$t('commands.remove_command')"
                    >
                      <i class="fas fa-trash"></i>
                    </button>
                  </div>
                  <div v-if="platform === 'windows'" class="form-check mt-1">
                    <input 
                      :checked="command.elevated"
                      @change="updateUndoCommand(index, 'elevated', $event.target.checked)"
                      class="form-check-input"
                      type="checkbox" 
                      :id="`undo-elevated-${index}`"
                    />
                    <label class="form-check-label small" :for="`undo-elevated-${index}`">
                      <i class="fas fa-shield-alt"></i> {{ $t('_common.elevated') }}
                    </label>
                  </div>
                </div>
                <button class="btn btn-success btn-sm" @click="addUndoCommand">
                  <i class="fas fa-plus"></i> {{ $t('commands.add_command') }}
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>
      
      <div class="text-center mt-3" v-if="hasAnyCommands">
        <button class="btn btn-outline-primary btn-sm" @click="viewMode = 'advanced'">
          <i class="fas fa-cogs"></i> {{ $t('commands.need_more_control') }}
        </button>
      </div>
    </div>

    <!-- Advanced Mode: Full AdvancedCommands Component -->
    <div v-else-if="viewMode === 'advanced'" class="advanced-view">
      <div class="alert alert-info mb-3">
        <div class="d-flex justify-content-between align-items-center">
          <div>
            <h6 class="mb-1"><i class="fas fa-info-circle"></i> {{ $t('commands.advanced_mode_title') }}</h6>
            <small>{{ $t('commands.advanced_mode_desc') }}</small>
          </div>
          <button class="btn btn-outline-secondary btn-sm" @click="viewMode = 'basic'">
            <i class="fas fa-arrow-left"></i> {{ $t('commands.view_simple') }}
          </button>
        </div>
      </div>
      
      <AdvancedCommands 
        v-model="advancedCommands"
        :platform="platform"
        :legacy-commands="legacyCommands"
        :app-mode="true"
      />
    </div>
  </div>
</template>

<script>
import AdvancedCommands from './AdvancedCommands.vue'

export default {
  name: 'ApplicationCommands',
  components: {
    AdvancedCommands
  },
  props: {
    modelValue: {
      type: Object,
      default: () => ({})
    },
    platform: {
      type: String,
      default: ''
    },
    legacyCommands: {
      type: Array,
      default: () => []
    },
    legacyDetached: {
      type: Array,
      default: () => []
    }
  },
  emits: ['update:modelValue'],
  data() {
    return {
      viewMode: 'basic',
      advancedCommands: {}
    }
  },
  computed: {
    doCommands: {
      get() {
        return this.getBasicCommands('PRE_STREAM_START')
      },
      set(value) {
        this.setBasicCommands('PRE_STREAM_START', value)
      }
    },
    undoCommands: {
      get() {
        return this.getBasicCommands('POST_STREAM_STOP')
      },
      set(value) {
        this.setBasicCommands('POST_STREAM_STOP', value)
      }
    },
    hasDoCommands() {
      return this.doCommands && this.doCommands.length > 0
    },
    hasUndoCommands() {
      return this.undoCommands && this.undoCommands.length > 0
    },
    hasAnyCommands() {
      return this.hasDoCommands || this.hasUndoCommands || this.hasAdvancedCommands
    },
    hasAdvancedCommands() {
      if (!this.advancedCommands || typeof this.advancedCommands !== 'object') return false
      return Object.values(this.advancedCommands).some(stage => 
        stage && Array.isArray(stage) && stage.length > 0
      )
    }
  },
  watch: {
    modelValue: {
      handler(newVal) {
        // Deep copy to avoid reference issues and ensure reactivity
        this.advancedCommands = JSON.parse(JSON.stringify(newVal || {}))
        this.detectMode()
        this.migrateDetachedCommands()
      },
      immediate: true,
      deep: true
    },
    advancedCommands: {
      handler(newVal) {
        // Emit a deep copy to avoid reference issues
        this.$emit('update:modelValue', JSON.parse(JSON.stringify(newVal)))
      },
      deep: true
    }
  },
  methods: {
    detectMode() {
      // If there are commands in stages other than PRE_STREAM_START and POST_STREAM_STOP,
      // automatically switch to advanced mode
      const basicStages = ['PRE_STREAM_START', 'POST_STREAM_STOP']
      const hasAdvancedStageCommands = Object.keys(this.advancedCommands || {}).some(stage => 
        !basicStages.includes(stage) && 
        this.advancedCommands[stage] && 
        Array.isArray(this.advancedCommands[stage]) && 
        this.advancedCommands[stage].length > 0
      )
      
      if (hasAdvancedStageCommands) {
        this.viewMode = 'advanced'
      }
    },
    getBasicCommands(stage) {
      if (!this.advancedCommands || !this.advancedCommands[stage]) return []
      
      // Convert from advanced format to basic format
      const stageGroups = this.advancedCommands[stage]
      if (!Array.isArray(stageGroups) || stageGroups.length === 0) return []
      
      const commands = []
      stageGroups.forEach(group => {
        if (group && group.commands && Array.isArray(group.commands)) {
          group.commands.forEach(cmd => {
            commands.push({
              cmd: cmd.cmd || '',
              elevated: cmd.elevated || false,
              timeout_seconds: cmd.timeout_seconds || 30
            })
          })
        }
      })
      
      return commands
    },
    setBasicCommands(stage, commands) {
      if (!this.advancedCommands) {
        this.advancedCommands = {}
      }
      
      // Convert basic format to advanced format
      if (!commands || commands.length === 0) {
        this.$set(this.advancedCommands, stage, [])
        return
      }
      
      const group = {
        name: stage === 'PRE_STREAM_START' ? 'Startup Commands' : 'Cleanup Commands',
        failure_policy: 'fail_fast',
        commands: commands.map(cmd => ({
          cmd: cmd.cmd || '',
          elevated: cmd.elevated || false,
          timeout_seconds: cmd.timeout_seconds || 30
        }))
      }
      
      this.$set(this.advancedCommands, stage, [group])
    },
    addDoCommand() {
      const newCommand = {
        cmd: '',
        elevated: false,
        timeout_seconds: 30
      }
      const commands = [...this.doCommands, newCommand]
      this.setBasicCommands('PRE_STREAM_START', commands)
    },
    removeDoCommand(index) {
      const commands = this.doCommands.filter((_, i) => i !== index)
      this.setBasicCommands('PRE_STREAM_START', commands)
    },
    updateDoCommand(index, field, value) {
      const commands = [...this.doCommands]
      commands[index][field] = value
      this.setBasicCommands('PRE_STREAM_START', commands)
    },
    addUndoCommand() {
      const newCommand = {
        cmd: '',
        elevated: false,
        timeout_seconds: 30
      }
      const commands = [...this.undoCommands, newCommand]
      this.setBasicCommands('POST_STREAM_STOP', commands)
    },
    removeUndoCommand(index) {
      const commands = this.undoCommands.filter((_, i) => i !== index)
      this.setBasicCommands('POST_STREAM_STOP', commands)
    },
    updateUndoCommand(index, field, value) {
      const commands = [...this.undoCommands]
      commands[index][field] = value
      this.setBasicCommands('POST_STREAM_STOP', commands)
    },
    migrateDetachedCommands() {
      // Migrate legacy detached commands to POST_STREAM_START stage
      if (this.legacyDetached && this.legacyDetached.length > 0) {
        const detachedCommands = this.legacyDetached
          .filter(cmd => cmd && cmd.trim().length > 0)
          .map(cmd => ({
            cmd: cmd.trim(),
            elevated: false,
            timeout_seconds: 30
          }))
        
        if (detachedCommands.length > 0) {
          if (!this.advancedCommands['POST_STREAM_START']) {
            this.$set(this.advancedCommands, 'POST_STREAM_START', [])
          }
          
          // Add detached commands as a background group
          const backgroundGroup = {
            name: 'Background Services (Migrated)',
            failure_policy: 'continue_on_failure',
            commands: detachedCommands
          }
          
          this.advancedCommands['POST_STREAM_START'].push(backgroundGroup)
        }
      }
    }
  }
}
</script>

<style scoped>
.application-commands {
  margin-bottom: 1rem;
}

.command-item {
  border-left: 3px solid transparent;
  padding-left: 0.5rem;
  transition: border-color 0.2s ease;
}

.command-item:hover {
  border-left-color: #007bff;
}

.card-header.bg-primary {
  background-color: #007bff !important;
}

.card-header.bg-secondary {
  background-color: #6c757d !important;
}

.monospace {
  font-family: 'Monaco', 'Menlo', 'Ubuntu Mono', monospace;
  font-size: 0.875rem;
}

.btn-check:checked + .btn {
  background-color: #007bff;
  border-color: #007bff;
  color: white;
}
</style>
