<template>
  <div class="row justify-content-center py-5">
    <div class="col-md-6 col-lg-4">
      <div class="card">
        <div class="card-body">
          <h1 class="h3 mb-3 text-center">{{ $t('login.title') }}</h1>
          <p class="text-muted text-center mb-4">{{ $t('login.description') }}</p>
          <form @submit.prevent="login">
            <FormGroup id="username" :label="$t('_common.username')">
              <input required type="text" class="form-control" id="username" autocomplete="username"
                v-model="credentials.username" />
            </FormGroup>
            <FormGroup id="password" :label="$t('_common.password')">
              <input required type="password" class="form-control" id="password" autocomplete="current-password"
                v-model="credentials.password" />
            </FormGroup>
            <StatusAlert :error="error" alert-class="mt-3 mb-0" />
            <button type="submit" class="btn btn-primary w-100 mt-4">
              <log-in :size="18" class="icon"></log-in>
              {{ $t('login.submit') }}
            </button>
          </form>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import FormGroup from '@/components/FormGroup.vue'
import StatusAlert from '@/components/StatusAlert.vue'
import { apiFetch } from '@/utils/fetch_utils'
import { LogIn } from '@lucide/vue'

export default {
  components: {
    FormGroup,
    StatusAlert,
    LogIn,
  },
  data() {
    return {
      error: null,
      credentials: {
        username: "",
        password: "",
      },
    };
  },
  methods: {
    login() {
      this.error = null;
      apiFetch("./api/login", {
        method: "POST",
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(this.credentials),
      }).then((r) => {
        if (r.status === 200) {
          r.json().then((rj) => {
            if (rj.status === true) {
              const redirect = this.$route.query.redirect;
              this.$router.push(typeof redirect === 'string' ? redirect : '/');
            } else {
              this.error = rj.error || this.$t('login.error');
            }
          });
        } else if (r.status === 401) {
          this.error = this.$t('login.error');
        } else {
          this.error = "Internal Server Error";
        }
      });
    },
  },
}
</script>
