<template>
  <div class="modal fade" ref="modalEl" tabindex="-1" aria-hidden="true">
    <div class="modal-dialog" :class="dialogClass">
      <div class="modal-content">
        <div class="modal-header">
          <h5 class="modal-title"><slot name="title"></slot></h5>
          <button type="button" class="btn-close" :aria-label="$t('_common.close')" @click="close"></button>
        </div>
        <div class="modal-body">
          <slot></slot>
        </div>
        <div class="modal-footer" v-if="$slots.footer">
          <slot name="footer"></slot>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { Modal } from 'bootstrap'

export default {
  props: {
    modelValue: {
      type: Boolean,
      default: false
    },
    dialogClass: {
      type: String,
      default: ''
    },
    backdrop: {
      type: [Boolean, String],
      default: true
    },
    keyboard: {
      type: Boolean,
      default: true
    }
  },
  emits: ['update:modelValue', 'close'],
  data() {
    return {
      bsModal: null
    }
  },
  watch: {
    modelValue(shown) {
      if (shown) {
        this.bsModal.show()
      } else {
        this.bsModal.hide()
      }
    }
  },
  mounted() {
    this.bsModal = new Modal(this.$refs.modalEl, {
      backdrop: this.backdrop,
      keyboard: this.keyboard
    })
    this.$refs.modalEl.addEventListener('hidden.bs.modal', this.handleHidden)
    if (this.modelValue) {
      this.bsModal.show()
    }
  },
  beforeUnmount() {
    this.$refs.modalEl.removeEventListener('hidden.bs.modal', this.handleHidden)
    this.bsModal?.dispose()
  },
  methods: {
    close() {
      this.bsModal.hide()
    },
    handleHidden() {
      this.$emit('update:modelValue', false)
      this.$emit('close')
    }
  }
}
</script>
