<template>
  <component
    v-if="iconComponent"
    :is="iconComponent"
    :size="size"
    :class="className"
  />
</template>

<script setup>
import { computed } from 'vue'
import { GitHubIcon, DiscordIcon } from 'vue3-simple-icons'

const props = defineProps({
  icon: {
    type: String,
    required: true,
    default: 'GitHub'
  },
  size: {
    type: [Number, String],
    default: 24
  },
  className: {
    type: String,
    default: ''
  }
})

// Map icon names to actual components
const iconMap = {
  'GitHub': GitHubIcon,
  'Discord': DiscordIcon,
}

const iconComponent = computed(() => {
  const component = iconMap[props.icon]
  if (!component) {
    console.error(`Icon "${props.icon}" not found in SimpleIcon mapping`)
    return null
  }
  return component
})
</script>
