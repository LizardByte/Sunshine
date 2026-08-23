<template>
  <article class="virtual-gamepad-feature">
    <div class="virtual-gamepad-feature-icon" aria-hidden="true">
      <component :is="icon" :size="21"></component>
    </div>
    <div>
      <h3>{{ title }}</h3>
      <p>{{ description }}</p>
    </div>
  </article>
</template>

<script>
export default {
  props: {
    icon: {
      type: [Object, Function],
      required: true
    },
    title: {
      type: String,
      required: true
    },
    description: {
      type: String,
      required: true
    }
  }
}
</script>
