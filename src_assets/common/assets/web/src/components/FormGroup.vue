<template>
  <div class="mb-3">
    <label :for="fieldId" class="form-label">{{ label }}</label>
    <slot></slot>
    <div class="form-text" v-if="description">{{ description }}</div>
  </div>
</template>

<script>
export default {
  props: {
    fieldId: {
      type: String,
      default: null
    },
    label: {
      type: String,
      required: true
    },
    description: {
      type: String,
      default: null
    }
  }
}
</script>
