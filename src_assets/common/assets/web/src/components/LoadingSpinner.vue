<template>
  <div class="spinner-border" :class="{ 'spinner-border-sm': small }" role="status">
    <span class="visually-hidden">{{ label }}</span>
  </div>
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
