<template>
  <div class="modal fade" ref="modalEl" tabindex="-1" aria-hidden="true">
    <div class="modal-dialog" :class="dialogClass">
      <div class="modal-content">
        <div class="modal-header">
          <h5 class="modal-title">
            <slot name="title"></slot>
          </h5>
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
    },
    // Raises this modal above any already-open modal instead of sharing z-index.
    stacked: {
      type: Boolean,
      default: false
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
    if (this.stacked) {
      this.$refs.modalEl.addEventListener('show.bs.modal', this.raiseZIndex)
    }
    if (this.modelValue) {
      this.bsModal.show()
    }
  },
  beforeUnmount() {
    this.$refs.modalEl.removeEventListener('hidden.bs.modal', this.handleHidden)
    this.$refs.modalEl.removeEventListener('show.bs.modal', this.raiseZIndex)
    this.bsModal?.dispose()
  },
  methods: {
    close() {
      this.bsModal.hide()
    },
    handleHidden() {
      this.$emit('update:modelValue', false)
      this.$emit('close')
    },
    raiseZIndex() {
      const modalEl = this.$refs.modalEl
      const openCount = document.querySelectorAll('.modal.show').length
      const z = 1055 + (openCount + 1) * 20
      modalEl.style.zIndex = z

      requestAnimationFrame(() => {
        const backdrops = document.querySelectorAll('.modal-backdrop')
        const backdrop = backdrops[backdrops.length - 1]
        if (backdrop) backdrop.style.zIndex = z - 10
      })
    }
  }
}
</script>
