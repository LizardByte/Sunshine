import { notifyKey } from '@/components/Notification.vue'
import router from '@/router'
import { authState } from '@/utils/auth'

/**
 * The set of error messages that indicate a CSRF validation failure.
 */
const CSRF_ERRORS = new Set(['Missing CSRF token', 'Invalid CSRF token', 'CSRF token expired'])

/**
 * Cached in-flight/fetched CSRF token, shared across calls so concurrent requests
 * don't each trigger their own GET /api/csrf-token. Cleared on a CSRF failure so
 * the next request fetches a fresh one instead of retrying the same rejected token.
 */
let csrfTokenPromise = null

function getCsrfToken() {
  if (!csrfTokenPromise) {
    csrfTokenPromise = fetch('./api/csrf-token')
      .then(r => r.json())
      .then(data => data.csrf_token)
  }
  return csrfTokenPromise
}

/**
 * Wrapper around the native fetch that automatically detects CSRF errors
 * (HTTP 400 with a known CSRF error message) and displays a notification,
 * and redirects to /login on a 401 (session expired or never established).
 * State-changing requests (anything other than GET/HEAD) automatically fetch
 * and attach an X-CSRF-Token header first.
 *
 * @param {string} url - The URL to fetch.
 * @param {RequestInit} [options] - Standard fetch options.
 * @returns {Promise<Response>} The fetch Response.
 */
export async function apiFetch(url, options = {}) {
  const method = (options.method || 'GET').toUpperCase()
  const needsCsrfToken = method !== 'GET' && method !== 'HEAD'

  let response
  if (needsCsrfToken) {
    const token = await getCsrfToken()
    const headers = new Headers(options.headers || {})
    headers.set('X-CSRF-Token', token)
    response = await fetch(url, { ...options, headers })
  } else {
    response = await fetch(url, options)
  }

  if (response.status === 400) {
    let body = null
    try {
      body = await response.clone().json()
    } catch (e) {
      console.debug('apiFetch: response body is not JSON', e)
    }

    if (body && CSRF_ERRORS.has(body.error)) {
      // The cached token was rejected (missing/invalid/expired) - drop it so the
      // next request fetches a fresh one instead of repeating the same failure.
      csrfTokenPromise = null
      notifyKey.error('_common.csrf_error_desc', '_common.csrf_error')
    }
  }

  if (response.status === 401) {
    authState.authenticated = false
    if (router.currentRoute.value.name !== 'login') {
      router.push({ path: '/login', query: { redirect: router.currentRoute.value.fullPath } })
    }
  }

  return response
}
