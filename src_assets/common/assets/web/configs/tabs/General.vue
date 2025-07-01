<script setup>
import Checkbox from '../../Checkbox.vue'
import EventActionsEditor from './EventActionsEditor.vue'
import { ref, computed } from 'vue'
import { convertPrepCmdsToEventActions } from '../../utils/convertPrepCmd.js'

const props = defineProps({
  platform: String,
  config: Object
})
const config = ref(props.config)

// Track if we're showing the conversion notice
const showConversionNotice = computed(() => {
  return config.value.global_prep_cmd && 
         config.value.global_prep_cmd.length > 0 && 
         (!config.value.global_event_actions || Object.keys(config.value.global_event_actions).length === 0)
})

// Track if we should show the event actions editor
const showEventActionsEditor = computed(() => {
  return config.value.global_event_actions && Array.isArray(config.value.global_event_actions) && config.value.global_event_actions.length >= 0
})

const converting = ref(false)
const conversionError = ref('')

function addCmd() {
  let template = {
    do: "",
    undo: "",
  };

  if (props.platform === 'windows') {
    template = { ...template, elevated: false };
  }
  config.value.global_prep_cmd.push(template);
}

function removeCmd(index) {
  config.value.global_prep_cmd.splice(index,1)
}

async function convertToEventActions() {
  if (converting.value) return
  
  converting.value = true
  conversionError.value = ''
  
  try {
    // Use frontend conversion utility instead of backend API
    const prepCmds = config.value.global_prep_cmd.map(cmd => ({
      do_cmd: cmd.do || '',
      undo_cmd: cmd.undo || '',
      elevated: cmd.elevated || false
    }))
    const result = convertPrepCmdsToEventActions(prepCmds)
    if (result && Array.isArray(result) && result.length > 0) {
      config.value.global_event_actions = result
      config.value.global_prep_cmd = []
      alert('Successfully converted global preparation commands to the new event actions format!')
    } else {
      conversionError.value = 'Conversion failed: No commands to convert.'
    }
  } catch (error) {
    console.error('Conversion error:', error)
    conversionError.value = 'Failed to convert commands: ' + error.message
  } finally {
    converting.value = false
  }
}

function onEventActionsChange(newActions) {
  config.value.global_event_actions = newActions
}
</script>

<template>
  <div id="general" class="config-page">
    <!-- Locale -->
    <div class="mb-3">
      <label for="locale" class="form-label">{{ $t('config.locale') }}</label>
      <select id="locale" class="form-select" v-model="config.locale">
        <option value="bg">Български (Bulgarian)</option>
        <option value="cs">Čeština (Czech)</option>
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
        <option value="zh_TW">繁體中文 (Chinese Traditional)</option>
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

    <!-- Global Event Actions / Prep Commands -->
    <div id="global_prep_cmd" class="mb-3 d-flex flex-column">
      <!-- Only show label for legacy commands -->
      <label v-if="!showEventActionsEditor" class="form-label">{{ $t('config.global_prep_cmd') }} + OK</label>
      
      <!-- Conversion Notice -->
      <div v-if="showConversionNotice" class="alert alert-warning d-flex align-items-center" role="alert">
        <i class="fas fa-exchange-alt me-2"></i>
        <div class="flex-grow-1">
          <strong>Old Command Format Detected</strong><br/>
          You are currently using the old format for commands. We recommend converting to the new format for improved flexibility and future compatibility.<br/>
          <span class="text-muted">You can continue editing your old commands below, or click the button to convert them automatically.</span>
        </div>
        <button class="btn btn-primary ms-3" @click="convertToEventActions" :disabled="converting">
          <i class="fas fa-magic" :class="{'fa-spin': converting}"></i>
          {{ converting ? 'Converting...' : 'Convert to New Command Syntax' }}
        </button>
      </div>
      
      <!-- Conversion Error -->
      <div v-if="conversionError" class="alert alert-danger" role="alert">
        <i class="fas fa-exclamation-triangle me-2"></i>
        {{ conversionError }}
      </div>

      <!-- Legacy Global Prep Commands Table (show when not using event actions) -->
      <div v-if="!showEventActionsEditor">
        <div class="form-text mb-3">{{ $t('config.global_prep_cmd_desc') }}</div>
        <table class="table">
          <thead>
          <tr>
            <th scope="col"><i class="fas fa-play"></i> {{ $t('_common.do_cmd') }}</th>
            <th scope="col"><i class="fas fa-undo"></i> {{ $t('_common.undo_cmd') }}</th>
            <th scope="col" v-if="platform === 'windows'">
              <i class="fas fa-shield-alt"></i> {{ $t('_common.run_as') }}
            </th>
            <th scope="col"></th>
          </tr>
          </thead>
          <tbody>
          <tr v-if="!config.global_prep_cmd || config.global_prep_cmd.length === 0">
            <td colspan="4" class="text-center text-muted">{{ $t('config.no_prep_cmds') }}</td>
          </tr>
          <tr v-for="(c, i) in config.global_prep_cmd" :key="i">
            <td>
              <input type="text" class="form-control monospace" v-model="c.do" />
            </td>
            <td>
              <input type="text" class="form-control monospace" v-model="c.undo" />
            </td>
            <td v-if="platform === 'windows'" class="align-middle">
              <Checkbox :id="'prep-cmd-admin-' + i"
                        label="_common.elevated"
                        desc=""
                        v-model="c.elevated"
              ></Checkbox>
            </td>
            <td>
              <button class="btn btn-danger" @click="removeCmd(i)">
                <i class="fas fa-trash"></i>
              </button>
              <button class="btn btn-success" @click="addCmd">
                <i class="fas fa-plus"></i>
              </button>
            </td>
          </tr>
          </tbody>
        </table>
        <button class="ms-0 mt-2 btn btn-success" style="margin: 0 auto" @click="addCmd">
          &plus; {{ $t('config.add') }}
        </button>
      </div>

      <!-- Event Actions Editor (show when using event actions) -->
      <EventActionsEditor 
        v-if="showEventActionsEditor"
        :model-value="config.global_event_actions" 
        @update:model-value="onEventActionsChange"
      >
        <template #header>
          <label class="form-label">{{ $t('config.global_prep_cmd') }}</label>
        </template>
        <template #description>
          <div class="form-text mb-3">{{ $t('config.global_prep_cmd_desc') }}</div>
        </template>
      </EventActionsEditor>
    </div>

    <!-- Notify Pre-Releases -->
    <Checkbox class="mb-3"
              id="notify_pre_releases"
              locale-prefix="config"
              v-model="config.notify_pre_releases"
              default="false"
    ></Checkbox>
  </div>
</template>

<style scoped>

</style>
