<template>
  <div class="screenshot-modal" @click="close" @touchstart="handleTouchStart" @touchend="handleTouchEnd">
    <div class="screenshot-modal-content">
      <button type="button" class="screenshot-close" @click="close" aria-label="Close">
        <x :size="20"></x>
      </button>

      <!-- Previous Button -->
      <button v-if="screenshots.length > 1" type="button" class="screenshot-nav screenshot-nav-prev" @click.stop="prev"
        aria-label="Previous screenshot">
        <chevron-left :size="32"></chevron-left>
      </button>

      <!-- Next Button -->
      <button v-if="screenshots.length > 1" type="button" class="screenshot-nav screenshot-nav-next" @click.stop="next"
        aria-label="Next screenshot">
        <chevron-right :size="32"></chevron-right>
      </button>

      <!-- Screenshot Counter -->
      <div v-if="screenshots.length > 1" class="screenshot-counter">
        {{ index + 1 }} / {{ screenshots.length }}
      </div>

      <img :src="screenshots[index]" alt="Screenshot" @click.stop />
    </div>
  </div>
</template>

<script>
import { ChevronLeft, ChevronRight, X } from '@lucide/vue'

export default {
  components: {
    ChevronLeft,
    ChevronRight,
    X,
  },
  props: {
    screenshots: {
      type: Array,
      required: true
    },
    startIndex: {
      type: Number,
      default: 0
    }
  },
  emits: ['close'],
  data() {
    return {
      index: this.startIndex,
      touchStartX: 0,
      touchEndX: 0,
    };
  },
  mounted() {
    window.addEventListener('keydown', this.handleKeydown);
  },
  beforeUnmount() {
    window.removeEventListener('keydown', this.handleKeydown);
  },
  methods: {
    close() {
      this.$emit('close');
    },
    next() {
      if (this.screenshots.length === 0) return;
      this.index = (this.index + 1) % this.screenshots.length;
    },
    prev() {
      if (this.screenshots.length === 0) return;
      this.index = (this.index - 1 + this.screenshots.length) % this.screenshots.length;
    },
    handleKeydown(event) {
      if (event.key === 'ArrowRight') {
        event.preventDefault();
        this.next();
      } else if (event.key === 'ArrowLeft') {
        event.preventDefault();
        this.prev();
      } else if (event.key === 'Escape') {
        event.preventDefault();
        this.close();
      }
    },
    handleTouchStart(event) {
      this.touchStartX = event.changedTouches[0].screenX;
    },
    handleTouchEnd(event) {
      this.touchEndX = event.changedTouches[0].screenX;
      this.handleSwipe();
    },
    handleSwipe() {
      const swipeThreshold = 50;
      const diff = this.touchStartX - this.touchEndX;

      if (Math.abs(diff) > swipeThreshold) {
        if (diff > 0) {
          // Swiped left, show next
          this.next();
        } else {
          // Swiped right, show previous
          this.prev();
        }
      }
    },
  },
}
</script>
