<template>
  <div class="alert alert-danger" :class="alertClass" v-if="error">
    <b>{{ $t('_common.error') }}</b> {{ error }}
  </div>
  <div class="alert alert-success" :class="alertClass" v-if="success">
    <b>{{ $t('_common.success') }}</b> {{ successMessage }}
  </div>
</template>

<script>
export default {
  props: {
    error: {
      type: String,
      default: null
    },
    success: {
      type: Boolean,
      default: false
    },
    successMessage: {
      type: String,
      default: ''
    },
    alertClass: {
      type: String,
      default: 'my-3'
    }
  }
}
</script>
