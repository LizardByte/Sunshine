<template>
  <div class="alert" :class="`alert-${variant}`">
    <div v-if="compact" class="d-flex align-items-center">
      <component :is="resolvedIcon" :size="18" class="icon me-3"></component>
      <slot></slot>
    </div>
    <div v-else>
      <div class="d-flex align-items-center" :class="{ 'mb-3': $slots.body || action }">
        <component :is="resolvedIcon" :size="32" class="icon-lg me-3"></component>
        <div>
          <slot></slot>
        </div>
      </div>
      <slot name="body"></slot>
      <router-link v-if="action" class="btn" :class="`btn-${variant}`" :to="action.to">
        <component :is="action.icon" :size="18" class="icon"></component>
        {{ action.label }}
      </router-link>
    </div>
  </div>
</template>

<script>
import { AlertCircle, AlertTriangle, CheckCircle, Info } from '@lucide/vue'

const defaultIcons = {
  danger: AlertCircle,
  warning: AlertTriangle,
  info: Info,
  success: CheckCircle
}

export default {
  props: {
    variant: {
      type: String,
      required: true
    },
    icon: {
      type: [Object, Function],
      default: null
    },
    action: {
      type: Object,
      default: null
    },
    compact: {
      type: Boolean,
      default: false
    }
  },
  computed: {
    resolvedIcon() {
      return this.icon || defaultIcons[this.variant]
    }
  }
}
</script>
