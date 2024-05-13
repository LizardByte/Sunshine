<script setup>
const props = defineProps({
  platform: {
    type: String,
    required: true
  }
})
</script>

<template>
  <template v-if="$slots.windows && platform === 'windows'">
    <slot name="windows"></slot>
  </template>

  <template v-if="$slots.linux && platform === 'linux'">
    <slot name="linux"></slot>
  </template>

  <template v-if="$slots.macos && platform === 'macos'">
    <slot name="macos"></slot>
  </template>
</template>


<style scoped>

</style>
