<template>
  <div class="my-4">
    <h1>{{ $t('password.password_change') }}</h1>
    <p>{{ $t('password.password_change_desc') }}</p>
  </div>
  <form @submit.prevent="save">
    <div class="card">
      <div class="card-body">
        <div class="row">
          <div class="col-md-6">
            <h4>{{ $t('password.current_creds') }}</h4>
            <FormGroup field-id="currentUsername" :label="$t('_common.username')">
              <input required type="text" class="form-control" id="currentUsername"
                v-model="passwordData.currentUsername" />
            </FormGroup>
            <FormGroup field-id="currentPassword" :label="$t('_common.password')">
              <input autocomplete="current-password" type="password" class="form-control" id="currentPassword"
                v-model="passwordData.currentPassword" />
            </FormGroup>
          </div>
          <div class="col-md-6">
            <h4>{{ $t('password.new_creds') }}</h4>
            <FormGroup field-id="newUsername" :label="$t('_common.username')" :description="$t('password.new_username_desc')">
              <input type="text" class="form-control" id="newUsername" v-model="passwordData.newUsername" />
            </FormGroup>
            <FormGroup field-id="newPassword" :label="$t('_common.password')">
              <input autocomplete="new-password" required type="password" class="form-control" id="newPassword"
                v-model="passwordData.newPassword" />
            </FormGroup>
            <FormGroup field-id="confirmNewPassword" :label="$t('password.confirm_password')">
              <input autocomplete="new-password" required type="password" class="form-control" id="confirmNewPassword"
                v-model="passwordData.confirmNewPassword" />
            </FormGroup>
          </div>
        </div>
        <StatusAlert :error="error" :success="success" :success-message="$t('password.success_msg')"
          alert-class="mt-3 mb-0" />
      </div>
    </div>
    <button type="submit" class="btn btn-primary mt-4">
      <save :size="18" class="icon"></save>
      {{ $t('_common.save') }}
    </button>
  </form>
</template>

<script>
import FormGroup from '@/components/FormGroup.vue'
import StatusAlert from '@/components/StatusAlert.vue'
import { apiFetch } from '@/utils/fetch_utils'
import { Save } from '@lucide/vue'

export default {
  components: {
    FormGroup,
    StatusAlert,
    Save,
  },
  data() {
    return {
      error: null,
      success: false,
      passwordData: {
        currentUsername: "",
        currentPassword: "",
        newUsername: "",
        newPassword: "",
        confirmNewPassword: "",
      },
    };
  },
  methods: {
    save() {
      this.error = null;
      apiFetch("./api/password", {
        method: "POST",
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(this.passwordData),
      }).then((r) => {
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
