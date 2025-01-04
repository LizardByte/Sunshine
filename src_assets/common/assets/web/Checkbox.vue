<script setup>
const model = defineModel({ required: true });
const slots = defineSlots();
const props = defineProps({
  class: {
    type: String,
    default: ""
  },
  desc: {
    type: String,
    default: null
  },
  id: {
    type: String,
    required: true
  },
  label: {
    type: String,
    default: null
  },
  localePrefix: {
    type: String,
    default: "missing-prefix"
  },
  default: {
    type: [Boolean, null],
    default: null
  }
});

// Add the mandatory class values
const extendedClassStr = (() => {
  let values = props.class.split(" ");
  if (!values.includes("form-check")) {
    values.push("form-check");
  }
  return values.join(" ");
})();

// Determine the true/false values for the checkbox
const checkboxValues = (() => {
  const mappedValues = (() => {
    // Try literal values first
    let value = model.value;
    if (value === true || value === false) {
      return [true, false];
    }
    if (value === 1 || value === 0) {
      return [1, 0];
    }

    // Try mapping strings next (first in the list will be used as fallback)
    const stringPairs = [
      ["true", "false"],
      ["1", "0"],
      ["enabled", "disabled"],
      ["enable", "disable"],
      ["yes", "no"],
      ["on", "off"]
    ];

    value = `${value}`.toLowerCase().trim();
    for (const pair of stringPairs) {
      if (value === pair[0] || value === pair[1]) {
        return pair;
      }
    }

    // Return default if nothing matches
    console.error(`Checkbox value ${model.value} (${value}) did not match any acceptable pattern!`);
    return stringPairs[0];
  })();

  return { truthy: mappedValues[0], falsy: mappedValues[1] };
})();

const labelField = props.label ?? `${props.localePrefix}.${props.id}`;
const descField = props.desc ?? `${props.localePrefix}.${props.id}_desc`;
const showDesc = props.desc !== "" || Object.entries(slots).length > 0;
const showDefValue = props.default !== null;
const defValue = props.default ? "_common.enabled_def_cbox" : "_common.disabled_def_cbox";
</script>

<template>
  <div :class="extendedClassStr">
    <label :for="props.id" :class="`form-check-label${showDesc ? ' mb-2' : ''}`">
      {{ $t(labelField) }}
      <div class="mt-0 form-text" v-if="showDefValue">
        {{ $t(defValue) }}
      </div>
    </label>
    <input type="checkbox"
           class="form-check-input"
           :id="props.id"
           v-model="model"
           :true-value="checkboxValues.truthy"
           :false-value="checkboxValues.falsy" />
    <div class="form-text" v-if="showDesc">
      {{ $t(descField) }}
      <slot></slot>
    </div>
  </div>
</template>
