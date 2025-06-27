<template>
  <div class="login-container">
    <div class="login-form">
      <div class="text-center mb-4">
        <img src="/images/logo-sunshine-45.png" height="45" alt="Sunshine">
        <h1 class="h3 mb-3 fw-normal">{{ $t('login.title') }}</h1>
      </div>
      <form @submit.prevent="login" v-if="!isLoggedIn" autocomplete="on">
        <div class="mb-3">
          <label for="username" class="form-label">{{ $t('_common.username') }}</label>
          <input 
            type="text" 
            class="form-control" 
            id="username" 
            name="username" 
            v-model="credentials.username" 
            required
            autocomplete="username"
          >
        </div>
        <div class="mb-3">
          <label for="password" class="form-label">{{ $t('_common.password') }}</label>
          <input 
            type="password" 
            class="form-control" 
            id="password" 
            name="password" 
            v-model="credentials.password" 
            required
            autocomplete="current-password"
          >
        </div>
        <button type="submit" class="btn btn-primary w-100" :disabled="loading">
          <span v-if="loading" class="spinner-border spinner-border-sm me-2"></span>
          {{ $t('login.sign_in') }}
        </button>
        <div v-if="error" class="alert alert-danger mt-3">
          {{ error }}
        </div>
      </form>
      <div v-else class="text-center">
        <div class="alert alert-success">
          {{ $t('login.success') }}
        </div>
        <output class="spinner-border">
          <span class="visually-hidden">Loading...</span>
        </output>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useI18n } from 'vue-i18n'

const credentials = ref({ username: '', password: '' })
const loading = ref(false)
const error = ref('')
const isLoggedIn = ref(false)
const requestedRedirect = ref('/')
const safeRedirect = ref('/')
const { t } = useI18n ? useI18n() : { t: (k, d) => d || k }

onMounted(() => {
  const urlParams = new URLSearchParams(window.location.search)
  const redirectParam = urlParams.get('redirect')
  if (redirectParam) {
    sessionStorage.setItem('pending_redirect', redirectParam)
    urlParams.delete('redirect')
    const cleanUrl = window.location.pathname + (urlParams.toString() ? '?' + urlParams.toString() : '')
    window.history.replaceState({}, document.title, cleanUrl)
  }
  requestedRedirect.value = sessionStorage.getItem('pending_redirect') || '/'
  safeRedirect.value = '/'
})

/**
 * Attempt login with credentials.
 * @returns {Promise<void>}
 */
async function login() {
  loading.value = true
  error.value = ''
  try {
    const response = await fetch('/api/auth/login', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        username: credentials.value.username,
        password: credentials.value.password,
        redirect: encodeURIComponent(requestedRedirect.value)
      })
    })
    const data = await response.json()
    if (response.ok && data.status) {
      isLoggedIn.value = true
      safeRedirect.value = data.redirect || '/'
      sessionStorage.removeItem('pending_redirect')
      setTimeout(() => {
        redirectToApp()
      }, 1000)
    } else {
      error.value = data.error || 'Login failed'
    }
  } catch (e) {
    console.error('Login error:', e)
    error.value = 'Network error. Please try again.'
  } finally {
    loading.value = false
  }
}

/**
 * Redirects the user to the application after successful login.
 * @returns {void}
 */
function redirectToApp() {
  window.location.href = encodeURI(safeRedirect.value)
}
</script>

<style scoped>
.login-container {
  max-width: 400px;
  margin: 2rem auto;
  padding: 2rem;
}
.login-form {
  background: var(--bs-body-bg);
  border: 1px solid var(--bs-border-color);
  border-radius: 0.375rem;
  padding: 2rem;
}
</style>
