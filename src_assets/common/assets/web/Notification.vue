<template>
  <div class="notification-container" v-if="state.notifications.length > 0">
    <div
      v-for="n in state.notifications"
      :key="n.id"
      class="alert d-flex align-items-start gap-2 mb-2"
      :class="'alert-' + n.type"
      role="alert"
    >
      <component :is="iconFor(n.type)" :size="18" class="icon flex-shrink-0 mt-1"></component>
      <div class="flex-grow-1">
        <div v-if="n.titleKey || n.title">
          <strong>{{ n.titleKey ? $t(n.titleKey) : n.title }}</strong>
        </div>
        <span>{{ n.messageKey ? $t(n.messageKey) : n.message }}</span>
      </div>
      <button type="button" class="btn-close" :aria-label="$t('_common.dismiss')" @click="dismiss(n.id)"></button>
    </div>
  </div>
</template>

<script>
import { reactive } from 'vue'
import { AlertCircle, AlertTriangle, CheckCircle, Info } from 'lucide-vue-next'

/**
 * Singleton reactive notification state shared across the app instance.
 * Using reactive() at module scope means all consumers — including plain JS
 * modules like fetch_utils.js — mutate the same reactive object, and any
 * mounted Notification component will update automatically.
 */
export const state = reactive({
  notifications: [],
  _nextId: 1,
})

/**
 * Push a notification using raw strings.
 *
 * @param {'danger'|'warning'|'success'|'info'} type - Bootstrap color variant.
 * @param {string} message - The notification body text.
 * @param {string} [title] - Optional bold title prefix.
 */
function push(type, message, title) {
  state.notifications.push({ id: state._nextId++, type, message, title: title || null, messageKey: null, titleKey: null })
}

/**
 * Push a notification using i18n translation keys.
 *
 * @param {'danger'|'warning'|'success'|'info'} type - Bootstrap color variant.
 * @param {string} messageKey - i18n key for the notification body.
 * @param {string} [titleKey] - Optional i18n key for the bold title prefix.
 */
function pushKey(type, messageKey, titleKey) {
  state.notifications.push({ id: state._nextId++, type, message: null, title: null, messageKey, titleKey: titleKey || null })
}

/**
 * Map a Bootstrap color variant to its corresponding lucide-vue-next icon name.
 *
 * @param {'danger'|'warning'|'success'|'info'} type - Bootstrap color variant.
 * @returns {string} The lucide icon component name.
 */
function iconFor(type) {
  return { danger: 'AlertCircle', warning: 'AlertTriangle', success: 'CheckCircle', info: 'Info' }[type] || 'Info'
}

/**
 * Convenience helpers for the four common variants using raw strings.
 */
export const notify = {
  /**
   * @param {string} message - The notification body text.
   * @param {string} [title] - Optional bold title shown above the message.
   */
  error: (message, title) => push('danger', message, title),
  /**
   * @param {string} message - The notification body text.
   * @param {string} [title] - Optional bold title shown above the message.
   */
  warning: (message, title) => push('warning', message, title),
  /**
   * @param {string} message - The notification body text.
   * @param {string} [title] - Optional bold title shown above the message.
   */
  success: (message, title) => push('success', message, title),
  /**
   * @param {string} message - The notification body text.
   * @param {string} [title] - Optional bold title shown above the message.
   */
  info: (message, title) => push('info', message, title),
}

/**
 * Convenience helpers for the four common variants using i18n keys.
 */
export const notifyKey = {
  /**
   * @param {string} messageKey - i18n key for the notification body text.
   * @param {string} [titleKey] - Optional i18n key for the bold title shown above the message.
   */
  error: (messageKey, titleKey) => pushKey('danger', messageKey, titleKey),
  /**
   * @param {string} messageKey - i18n key for the notification body text.
   * @param {string} [titleKey] - Optional i18n key for the bold title shown above the message.
   */
  warning: (messageKey, titleKey) => pushKey('warning', messageKey, titleKey),
  /**
   * @param {string} messageKey - i18n key for the notification body text.
   * @param {string} [titleKey] - Optional i18n key for the bold title shown above the message.
   */
  success: (messageKey, titleKey) => pushKey('success', messageKey, titleKey),
  /**
   * @param {string} messageKey - i18n key for the notification body text.
   * @param {string} [titleKey] - Optional i18n key for the bold title shown above the message.
   */
  info: (messageKey, titleKey) => pushKey('info', messageKey, titleKey),
}

export default {
  components: { AlertCircle, AlertTriangle, CheckCircle, Info },

  setup() {
    function dismiss(id) {
      const idx = state.notifications.findIndex(n => n.id === id)
      if (idx !== -1) state.notifications.splice(idx, 1)
    }

    return { state, dismiss, iconFor }
  },
}
</script>

<style scoped>
.notification-container {
  position: fixed;
  bottom: 1rem;
  right: 1rem;
  left: 1rem;
  z-index: 1090;
}

@media (min-width: 576px) {
  .notification-container {
    left: auto;
    width: 22rem;
  }
}
</style>
