<template>
  <form @submit.prevent="save">
    <div class="mb-2">
      <label for="usernameInput" class="form-label">Username:</label>
      <input type="text" class="form-control" id="usernameInput" autocomplete="username"
        v-model="passwordData.username" />
    </div>
    <div class="mb-2">
      <label for="passwordInput" class="form-label">Password:</label>
      <input type="password" class="form-control" id="passwordInput" autocomplete="new-password"
        v-model="passwordData.password" required />
    </div>
    <button type="submit" class="btn btn-primary w-100 mb-2" v-bind:disabled="loading">
      Login
    </button>
    <div class="alert alert-danger" v-if="error"><b>Error: </b>{{ error }}</div>
    <div class="alert alert-success" v-if="success">
      <b>Success! </b>
    </div>
  </form>
</template>

<script>
export default {
  data() {
    return {
      error: null,
      success: false,
      loading: false,
      passwordData: {
        username: "",
        password: ""
      },
    };
  },
  methods: {
    save() {
      this.error = null;
      this.loading = true;
      fetch("/api/login", {
        method: "POST",
        body: JSON.stringify(this.passwordData),
      }).then((r) => {
        this.loading = false;
        if (r.status == 200) {
          r.json().then((rj) => {
            if (rj.status.toString() === "true") {
              this.success = true;
              this.$emit('loggedin');
            } else {
              this.error = rj.error || "Invalid Username or Password";
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
