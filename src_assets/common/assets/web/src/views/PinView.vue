<template>
  <h1 class="my-4 text-center">{{ $t('pin.pin_pairing') }}</h1>
  <form class="form d-flex flex-column align-items-center" @submit.prevent="registerDevice">
    <div class="card flex-column d-flex p-4 mb-4">
      <div class="input-group mt-2">
        <span class="input-group-text">
          <hash :size="18" class="icon"></hash>
        </span>
        <input type="text" pattern="\d*" :placeholder="$t('navbar.pin')" :aria-label="$t('navbar.pin')" autofocus
          class="form-control" required v-model="pin" />
      </div>
      <div class="input-group my-4">
        <span class="input-group-text">
          <monitor :size="18" class="icon"></monitor>
        </span>
        <input type="text" :placeholder="$t('pin.device_name')" :aria-label="$t('pin.device_name')"
          class="form-control" required v-model="name" />
      </div>
      <button type="submit" class="btn btn-primary">
        <forward :size="18" class="icon"></forward>
        {{ $t('pin.send') }}
      </button>
    </div>
    <div class="alert alert-warning">
      <b>{{ $t('_common.warning') }}</b> {{ $t('pin.warning_msg') }}
    </div>
    <StatusAlert :error="error" :success="success" :success-message="$t('pin.pair_success')" />
  </form>
</template>

<script>
import StatusAlert from '@/components/StatusAlert.vue'
import { apiFetch } from '@/utils/fetch_utils'
import { Forward, Hash, Monitor } from '@lucide/vue'

export default {
  components: {
    StatusAlert,
    Forward,
    Hash,
    Monitor,
  },
  inject: ['i18n'],
  data() {
    return {
      pin: '',
      name: '',
      error: null,
      success: false,
    }
  },
  methods: {
    registerDevice() {
      this.error = null;
      this.success = false;
      apiFetch("./api/pin", {
        method: "POST",
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({ pin: this.pin, name: this.name })
      })
        .then((response) => response.json())
        .then((response) => {
          if (response.status === true) {
            this.success = true;
            this.pin = '';
            this.name = '';
          } else {
            this.error = this.i18n.t('pin.pair_failure');
          }
        });
    }
  }
}
</script>
