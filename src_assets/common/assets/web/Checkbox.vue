<script setup>
const model = defineModel({ required: true });
const props = defineProps({
  class: {
    type: String,
    default: ""
  },
  desc: {
    type: [String, null],
    default: ""
  },
  id: {
    type: String,
    required: true
  },
  label: {
    type: String,
    default: ""
  },
  localePrefix: {
    type: String,
    default: "missing-prefix"
  },
  checkedByDef: {
    type: Boolean,
    default: false
  },
  uncheckedByDef: {
    type: Boolean,
    default: false
  },
  trueValue: {
    type: undefined,
    default: "enabled"
  },
  falseValue: {
    type: undefined,
    default: "disabled"
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

const labelField = props.label || `${props.localePrefix}.${props.id}`;
const descField = props.desc || `${props.localePrefix}.${props.id}_desc`;
const showDesc = props.desc !== null;
const showDefValue = props.checkedByDef || props.uncheckedByDef;
const defValue = props.checkedByDef === props.uncheckedByDef ? "INVALID" :
  props.checkedByDef ? "_common.enabled_def_cbox" : "_common.disabled_def_cbox"
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
           :true-value="props.trueValue"
           :false-value="props.falseValue" />
    <div class="form-text" v-if="showDesc">
      {{ $t(descField) }}
    </div>
  </div>
</template>
