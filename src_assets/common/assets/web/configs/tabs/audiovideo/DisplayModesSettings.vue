<script setup>
import { ref } from 'vue'
import { $tp } from '../../../platform-i18n'
import PlatformLayout from '../../../PlatformLayout.vue'

const props = defineProps([
  'platform',
  'config',
  'resolutions',
  'fps',
  'max_bitrate',
])

const config = ref(props.config)
const resolutions = ref(props.resolutions)
const fps = ref(props.fps)

const resIn = ref("")
const fpsIn = ref("")
</script>

<template>
  <div class="mb-3">
    <!-- Advertised Resolutions -->
    <div id="resolutions" class="resolutions-container">
      <label>{{ $t('config.resolutions') }}</label>
      <div class="resolutions d-flex flex-wrap">
        <div class="p-2 ms-item m-2 d-flex justify-content-between" v-for="(r,i) in resolutions" :key="r">
          <span class="px-2">{{r}}</span>
          <span style="cursor: pointer" @click="resolutions.splice(i,1)">&times;</span>
        </div>
      </div>
      <form @submit.prevent="resolutions.push(resIn);resIn = '';" class="d-flex align-items-center">
        <input type="text" v-model="resIn" required pattern="[0-9]+x[0-9]+" style="
                  width: 12ch;
                  border-top-right-radius: 0;
                  border-bottom-right-radius: 0;
                " class="form-control" />
        <button v-if="resolutions.length < 25" style="border-top-left-radius: 0; border-bottom-left-radius: 0" class="btn btn-success">
          +
        </button>
      </form>
    </div>

    <!-- Advertised FPS -->
    <div id="fps" class="fps-container">
      <label>{{ $t('config.fps') }}</label>
      <div class="fps d-flex flex-wrap">
        <div class="p-2 ms-item m-2 d-flex justify-content-between" v-for="(f,i) in fps" :key="f">
          <span class="px-2">{{f}}</span>
          <span style="cursor: pointer" @click="fps.splice(i,1)">&times;</span>
        </div>
      </div>
      <form @submit.prevent="+fpsIn >= 30 && fps.push(fpsIn); fpsIn = '';" class="d-flex align-items-center">
        <input type="text" v-model="fpsIn" required pattern="[0-9]+" style="
                  width: 6ch;
                  border-top-right-radius: 0;
                  border-bottom-right-radius: 0;
                " class="form-control" />
        <button v-if="fps.length < 5" style="border-top-left-radius: 0; border-bottom-left-radius: 0" class="btn btn-success">
          +
        </button>
      </form>
    </div>

    <div class="form-text mb-3">{{ $t('config.res_fps_desc') }}</div>

    <!--max_bitrate-->
    <div class="mb-3">
      <label for="max_bitrate" class="form-label">{{ $t("config.max_bitrate") }}</label>
      <input type="number" class="form-control" id="max_bitrate" placeholder="0" v-model="config.max_bitrate" />
      <div class="form-text">{{ $t("config.max_bitrate_desc") }}</div>
    </div>

  </div>
</template>

<style scoped>
.ms-item {
  background-color: var(--bs-dark-bg-subtle);
  font-size: 12px;
  font-weight: bold;
}
</style>
