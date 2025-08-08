<template>
  <div class="login-container">
    <div class="login-form">
      <div class="text-center mb-4">
        <img src="/images/logo-sunshine-45.png" height="45" alt="Sunshine">
        <h1 class="h3 mb-3 fw-normal">{{ $t('auth.login_title') }}</h1>
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
          {{ $t('auth.login_sign_in') }}
        </button>
        <div v-if="error" class="alert alert-danger mt-3">
          {{ error }}
        </div>
      </form>
      <div v-else class="text-center">
        <div class="alert alert-success">
          {{ $t('auth.login_success') }}
        </div>
        <output class="spinner-border">
          <span class="visually-hidden">{{$t('auth.login_loading')}}</span>
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

function sanitizeRedirect(raw) {
  try {
    if (!raw || typeof raw !== 'string') return '/'
    // decode then re-encode for validation of % sequences
    try { raw = decodeURIComponent(raw) } catch { /* ignore */ }
    // Must start with single slash, no protocol, no double slash at start, limit length
    if (!raw.startsWith('/')) return '/'
    if (raw.startsWith('//')) return '/'
    if (raw.includes('://')) return '/'
    if (raw.length > 512) return '/'
    // Strip any /login recursion to avoid loop
    if (raw.startsWith('/login')) return '/'
    return raw
  } catch { return '/' }
}

function hasSessionCookie() {
  return document.cookie.split(';').some(c => c.trim().startsWith('session_token='))
}

function redirectNowIfAuthenticated() {
  if (hasSessionCookie()) {
    // Use any pending redirect or default to root
    const target = sanitizeRedirect(sessionStorage.getItem('pending_redirect') || safeRedirect.value || '/')
    // Replace to avoid leaving /login in history so back button won't return here
    window.location.replace(target)
  }
}

onMounted(() => {
  document.title = `Sunshine - ${t('auth.login_title')}`
  const urlParams = new URLSearchParams(window.location.search)
  const redirectParam = urlParams.get('redirect')
  if (redirectParam) {
    const sanitized = sanitizeRedirect(redirectParam)
    sessionStorage.setItem('pending_redirect', sanitized)
    urlParams.delete('redirect')
    const cleanUrl = window.location.pathname + (urlParams.toString() ? '?' + urlParams.toString() : '')
    window.history.replaceState({}, document.title, cleanUrl)
  }
  requestedRedirect.value = sessionStorage.getItem('pending_redirect') || '/'
  safeRedirect.value = sanitizeRedirect(requestedRedirect.value)

  // If already authenticated (user pressed back into login or bookmarked it), redirect immediately
  redirectNowIfAuthenticated()
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
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        username: credentials.value.username,
        password: credentials.value.password,
        // Send raw (already sanitized) path, do NOT encode again to avoid server rejecting it
        redirect: requestedRedirect.value
      })
    })
    const data = await response.json().catch(() => ({}))
    if (response.ok && data.status) {
      isLoggedIn.value = true
      safeRedirect.value = sanitizeRedirect(data.redirect) || '/'
      sessionStorage.removeItem('pending_redirect')
      setTimeout(() => { redirectToApp() }, 500)
    } else {
      error.value = data.error || t('auth.login_failed')
    }
  } catch (e) {
    console.error('Login error:', e)
    error.value = t('auth.login_network_error')
  } finally {
    loading.value = false
  }
}

/**
 * Redirects the user to the application after successful login.
 * @returns {void}
 */
function redirectToApp() {
  window.location.replace(safeRedirect.value)
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
