<template>
  <h1 class="my-4">{{ $t('welcome.greeting') }}</h1>
  <p>{{ $t('welcome.create_creds') }}</p>
  <div class="d-flex flex-column flex-lg-row gap-4">
    <div class="card flex-grow-1">
      <div class="card-body">
        <div class="alert alert-warning">
          {{ $t('welcome.create_creds_alert') }}
        </div>
        <form @submit.prevent="save">
          <FormGroup id="usernameInput" :label="$t('_common.username')">
            <input type="text" class="form-control" id="usernameInput" autocomplete="username"
              v-model="passwordData.newUsername" />
          </FormGroup>
          <FormGroup id="passwordInput" :label="$t('_common.password')">
            <input type="password" class="form-control" id="passwordInput" autocomplete="new-password"
              v-model="passwordData.newPassword" required />
          </FormGroup>
          <FormGroup id="confirmPasswordInput" :label="$t('welcome.confirm_password')">
            <input type="password" class="form-control" id="confirmPasswordInput" autocomplete="new-password"
              v-model="passwordData.confirmNewPassword" required />
          </FormGroup>
          <button type="submit" class="btn btn-primary" v-bind:disabled="loading">
            <log-in :size="18" class="icon"></log-in>
            {{ $t('welcome.login') }}
          </button>
          <StatusAlert :error="error" :success="success" :success-message="$t('welcome.welcome_success')"
            alert-class="mt-3 mb-0" />
        </form>
      </div>
    </div>
    <div class="d-flex flex-column justify-content-between flex-grow-1">
      <ResourceCard></ResourceCard>
    </div>
  </div>
</template>

<script>
import ResourceCard from '@/components/ResourceCard.vue'
import FormGroup from '@/components/FormGroup.vue'
import StatusAlert from '@/components/StatusAlert.vue'
import { apiFetch } from '@/utils/fetch_utils'
import { LogIn } from '@lucide/vue'

export default {
  components: {
    ResourceCard,
    FormGroup,
    StatusAlert,
    LogIn,
  },
  data() {
    return {
      error: null,
      success: false,
      loading: false,
      passwordData: {
        newUsername: "sunshine",
        newPassword: "",
        confirmNewPassword: "",
      },
    };
  },
  methods: {
    save() {
      this.error = null;
      this.loading = true;
      apiFetch("./api/password", {
        method: "POST",
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(this.passwordData),
      }).then((r) => {
        this.loading = false;
        if (r.status === 200) {
          r.json().then((rj) => {
            this.success = rj.status;
            if (this.success === true) {
              setTimeout(() => {
                document.location.reload();
              }, 5000);
            } else {
              this.error = rj.error;
            }
          });
        } else {
          this.error = "Internal Server Error";
        }
      });
    },
  },
}
</script>
