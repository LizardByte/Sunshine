<template>
  <output class="spinner-border" :class="{ 'spinner-border-sm': small }">
    <span class="visually-hidden">{{ label }}</span>
  </output>
</template>

<script>
export default {
  props: {
    label: {
      type: String,
      required: true
    },
    small: {
      type: Boolean,
      default: false
    }
  }
}
</script>
