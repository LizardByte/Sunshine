
<script>
import Checkbox from '../../Checkbox.vue'
import EventActionsSection from '../../components/EventActionsSection.vue'

export default {
  name: 'General',
  components: {
    Checkbox,
    EventActionsSection
  },
  props: {
    platform: String,
    config: Object
  },
  mounted() {
    // Debugging: log config and event actions model
    console.info('[General.vue] mounted debug', {
      config: this.config,
      eventActionsModel: this.config?.global_event_actions,
      legacyCommands: this.config?.global_prep_cmd
    })
  },
  computed: {
    // Event actions model for the component (using stage-based format)
    eventActionsModel: {
      get() {
        // Parse global_event_actions JSON string or use existing object, handle Vue Proxy/reactivity
        let eventActions = undefined
        try {
          if (this.config.global_event_actions) {
            if (typeof this.config.global_event_actions === 'string') {
              eventActions = JSON.parse(this.config.global_event_actions)
            } else if (typeof this.config.global_event_actions === 'object') {
              // Defensive: de-proxy if needed
              eventActions = JSON.parse(JSON.stringify(this.config.global_event_actions))
            }
          }
        } catch (e) {
          console.error('[General.vue] Failed to parse global_event_actions:', e, this.config.global_event_actions)
          eventActions = undefined
        }

        // Debug: log type and content
        console.info('[General.vue] eventActionsModel.get: eventActions type', typeof eventActions, eventActions)

        // If eventActions is a direct stages object (not wrapped in {stages: ...}), return it
        if (eventActions && typeof eventActions === 'object') {
          if (Object.prototype.hasOwnProperty.call(eventActions, 'stages')) {
            // Wrapped format
            const stages = eventActions.stages || {}
            console.info('[General.vue] eventActionsModel.get: returning eventActions.stages (wrapped)', { eventActions, stages, stageKeys: Object.keys(stages) })
            return stages
          } else {
            // Direct format: check for known stage keys or at least any keys
            const keys = Object.keys(eventActions)
            const knownStageKeys = ['PRE_STREAM_START', 'POST_STREAM_STOP', 'PRE_STREAM_STOP', 'POST_STREAM_START']
            const hasStageKey = keys.some(k => knownStageKeys.includes(k)) || keys.length > 0
            if (hasStageKey) {
              console.info('[General.vue] eventActionsModel.get: returning eventActions as direct stages object', { eventActions, stageKeys: keys })
              return eventActions
            }
          }
        }

        // Fallback: Parse legacy commands ONLY if neither format is present
        let legacyCommands = []
        try {
          if (this.config.global_prep_cmd) {
            if (Array.isArray(this.config.global_prep_cmd)) {
              legacyCommands = this.config.global_prep_cmd
            } else {
              legacyCommands = JSON.parse(this.config.global_prep_cmd)
            }
          }
        } catch (e) {
          console.error('[General.vue] Failed to parse global_prep_cmd JSON:', e, this.config.global_prep_cmd)
          legacyCommands = []
        }
        const stages = this.convertLegacyToStages(legacyCommands)
        console.info('[General.vue] eventActionsModel.get: returning legacy-converted stages', { legacyCommands, stages })
        return stages
      },
      set(value) {
        // Update global_event_actions with the new stages
        console.info('[General.vue] eventActionsModel.set called', value)
        // Save as JSON string - value is now the stages object directly
        this.config.global_event_actions = JSON.stringify({ stages: value || {} })
        // Remove legacy format when new format is used
        if (value && Object.keys(value).length > 0) {
          delete this.config.global_prep_cmd
          console.info('[General.vue] Legacy global_prep_cmd removed after eventActionsModel.set')
        }
      }
    },

    // Check if there are legacy commands to migrate
    hasLegacyCommands() {
      let legacyCommands = []
      try {
        if (this.config.global_prep_cmd) {
          if (Array.isArray(this.config.global_prep_cmd)) {
            legacyCommands = this.config.global_prep_cmd
          } else {
            legacyCommands = JSON.parse(this.config.global_prep_cmd)
          }
        }
      } catch (e) {
        console.error('[General.vue] Failed to parse global_prep_cmd JSON:', e, this.config.global_prep_cmd)
        legacyCommands = []
      }
      if (!legacyCommands || legacyCommands.length === 0) {
        console.info('[General.vue] hasLegacyCommands: No legacy commands found')
        return false
      }

      // Check if event actions exist in either format
      let eventActions = undefined
      try {
        if (this.config.global_event_actions) {
          if (typeof this.config.global_event_actions === 'string') {
            eventActions = JSON.parse(this.config.global_event_actions)
          } else if (typeof this.config.global_event_actions === 'object') {
            eventActions = this.config.global_event_actions
          }
        }
      } catch (e) {
        console.error('[General.vue] Failed to parse global_event_actions JSON:', e, this.config.global_event_actions)
        eventActions = undefined
      }

      let hasEventActions = false
      if (eventActions && typeof eventActions === 'object') {
        if (Object.prototype.hasOwnProperty.call(eventActions, 'stages')) {
          // Wrapped format
          const stages = eventActions.stages || {}
          hasEventActions = Object.keys(stages).length > 0
        } else {
          // Direct format
          const keys = Object.keys(eventActions)
          const knownStageKeys = ['PRE_STREAM_START', 'POST_STREAM_STOP', 'PRE_STREAM_STOP', 'POST_STREAM_START']
          hasEventActions = keys.some(k => knownStageKeys.includes(k)) || keys.length > 0
        }
      }
      const result = !hasEventActions
      console.info('[General.vue] hasLegacyCommands:', {
        legacyCommands,
        eventActions,
        hasEventActions,
        result
      })
      return result
    }
  },
  methods: {
    // Convert legacy commands to stage-based format
    convertLegacyToStages(legacyCommands) {
      // Ensure legacyCommands is always an array
      if (!Array.isArray(legacyCommands)) {
        if (!legacyCommands) {
          legacyCommands = []
        } else if (typeof legacyCommands === 'object') {
          // If it's an object but not an array, wrap it in an array
          legacyCommands = [legacyCommands]
        } else {
          // If it's something else (string, number), ignore
          legacyCommands = []
        }
      }
      if (legacyCommands.length === 0) {
        console.info('[General.vue] convertLegacyToStages: No legacy commands to convert')
        return {}
      }

      const stages = {}
      // Create startup commands in PRE_STREAM_START stage
      const startupCommands = legacyCommands
        .filter(cmd => cmd && cmd.do_cmd && cmd.do_cmd.trim())
        .map(cmd => ({
          cmd: cmd.do_cmd.trim(),
          elevated: cmd.elevated || false,
          timeout_seconds: 30,
          ignore_error: false,
          async: false
        }))
      if (startupCommands.length > 0) {
        stages.PRE_STREAM_START = [{
          name: 'Legacy Setup Commands',
          failure_policy: 'FAIL_FAST',
          commands: startupCommands
        }]
      }
      // Create cleanup commands in POST_STREAM_STOP stage
      const cleanupCommands = legacyCommands
        .filter(cmd => cmd && cmd.undo_cmd && cmd.undo_cmd.trim())
        .map(cmd => ({
          cmd: cmd.undo_cmd.trim(),
          elevated: cmd.elevated || false,
          timeout_seconds: 30,
          ignore_error: false,
          async: false
        }))
      if (cleanupCommands.length > 0) {
        stages.POST_STREAM_STOP = [{
          name: 'Legacy Cleanup Commands',
          failure_policy: 'CONTINUE_ON_FAILURE',
          commands: cleanupCommands
        }]
      }
      console.info('[General.vue] convertLegacyToStages result', {
        legacyCommands,
        startupCommands,
        cleanupCommands,
        stages
      })
      return stages
    },

    migrateLegacyCommands() {
      let legacyCommands = []
      try {
        if (this.config.global_prep_cmd) {
          // Check if it's already an array or a JSON string
          if (Array.isArray(this.config.global_prep_cmd)) {
            legacyCommands = this.config.global_prep_cmd
          } else {
            legacyCommands = JSON.parse(this.config.global_prep_cmd)
          }
        }
      } catch (e) {
        console.error('[General.vue] Failed to parse global_prep_cmd JSON:', e, this.config.global_prep_cmd)
        legacyCommands = []
      }
      console.info('[General.vue] migrateLegacyCommands called', { legacyCommands })
      const stages = this.convertLegacyToStages(legacyCommands)
      // Update the config with new format (as JSON string)
      this.config.global_event_actions = JSON.stringify({ stages })
      // Clear legacy commands
      delete this.config.global_prep_cmd
      console.info('[General.vue] Legacy commands migrated to stages', { stages, config: this.config })
    }
  }
}
</script>

<template>
  <div v-if="config" id="general" class="config-page">
    <!-- Locale -->
    <div class="mb-3">
      <label for="locale" class="form-label">{{ $t('config.locale') }}</label>
      <select id="locale" class="form-select" v-model="config.locale">
        <option value="bg">Български (Bulgarian)</option>
        <option value="de">Deutsch (German)</option>
        <option value="en">English</option>
        <option value="en_GB">English, UK</option>
        <option value="en_US">English, US</option>
        <option value="es">Español (Spanish)</option>
        <option value="fr">Français (French)</option>
        <option value="it">Italiano (Italian)</option>
        <option value="ja">日本語 (Japanese)</option>
        <option value="ko">한국어 (Korean)</option>
        <option value="pl">Polski (Polish)</option>
        <option value="pt">Português (Portuguese)</option>
        <option value="pt_BR">Português, Brasileiro (Portuguese, Brazilian)</option>
        <option value="ru">Русский (Russian)</option>
        <option value="sv">svenska (Swedish)</option>
        <option value="tr">Türkçe (Turkish)</option>
        <option value="uk">Українська (Ukranian)</option>
        <option value="zh">简体中文 (Chinese Simplified)</option>
      </select>
      <div class="form-text">{{ $t('config.locale_desc') }}</div>
    </div>

    <!-- Sunshine Name -->
    <div class="mb-3">
      <label for="sunshine_name" class="form-label">{{ $t('config.sunshine_name') }}</label>
      <input type="text" class="form-control" id="sunshine_name" placeholder="Sunshine"
        v-model="config.sunshine_name" />
      <div class="form-text">{{ $t('config.sunshine_name_desc') }}</div>
    </div>

    <!-- Log Level -->
    <div class="mb-3">
      <label for="min_log_level" class="form-label">{{ $t('config.log_level') }}</label>
      <select id="min_log_level" class="form-select" v-model="config.min_log_level">
        <option value="0">{{ $t('config.log_level_0') }}</option>
        <option value="1">{{ $t('config.log_level_1') }}</option>
        <option value="2">{{ $t('config.log_level_2') }}</option>
        <option value="3">{{ $t('config.log_level_3') }}</option>
        <option value="4">{{ $t('config.log_level_4') }}</option>
        <option value="5">{{ $t('config.log_level_5') }}</option>
        <option value="6">{{ $t('config.log_level_6') }}</option>
      </select>
      <div class="form-text">{{ $t('config.log_level_desc') }}</div>
    </div>

    <!-- Global Event Actions -->
    <EventActionsSection 
      v-model="eventActionsModel" 
      :platform="platform"
    />

    <!-- Legacy Commands Migration Alert -->
    <div v-if="hasLegacyCommands" class="alert alert-warning">
      <div class="d-flex align-items-center">
        <i class="fas fa-exclamation-triangle me-2"></i>
        <div class="flex-grow-1">
          <strong>{{ $t('_commands.migrate_legacy_title', 'Legacy Commands Detected') }}</strong>
          <p class="mb-0">{{ $t('_commands.migrate_legacy_desc', 'Convert your existing prep commands into the new advanced command system.') }}</p>
        </div>
        <button class="btn btn-outline-warning" @click="migrateLegacyCommands">
          {{ $t('_commands.migrate_now', 'Migrate Now') }}
        </button>
      </div>
    </div>

    <!-- Notify Pre-Releases -->
    <Checkbox class="mb-3" id="notify_pre_releases" locale-prefix="config" v-model="config.notify_pre_releases"
      default="false"></Checkbox>
  </div>
</template>

<style scoped>
/* Additional styling can be added here if needed */
</style>
