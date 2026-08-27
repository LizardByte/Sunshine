<script setup>
import { inject } from 'vue'

const platform = inject('platform')
</script>

<template>
  <template v-if="$slots.windows && platform === 'windows'">
    <slot name="windows"></slot>
  </template>

  <template v-if="$slots.freebsd && platform === 'freebsd'">
    <slot name="freebsd"></slot>
  </template>

  <template v-if="$slots.linux && platform === 'linux'">
    <slot name="linux"></slot>
  </template>

  <template v-if="$slots.macos && platform === 'macos'">
    <slot name="macos"></slot>
  </template>

  <!-- common: rendered for any platform -->
  <template v-if="$slots.default">
    <slot></slot>
  </template>
</template>
