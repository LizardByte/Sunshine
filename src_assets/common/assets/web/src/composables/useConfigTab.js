import { ref, toRaw } from 'vue'

export function useConfigTab(rawConfig, defaults = {}) {
  const config = ref(rawConfig)

  Object.keys(defaults).forEach(key => {
    if (config.value[key] === undefined) {
      config.value[key] = structuredClone(defaults[key]);
    }
  })

  function getOwnConfigOptions() {
    const raw = toRaw(config.value);
    const result = {};
    Object.keys(defaults).forEach(key => {
      result[key] = raw[key];
    });
    return structuredClone(result);
  }

  return { config, getOwnConfigOptions }
}
