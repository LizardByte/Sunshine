import { notifyKey } from '@/components/Notification.vue'
import router from '@/router'
import { authState } from '@/utils/auth'

/**
 * The set of error messages that indicate a CSRF validation failure.
 */
const CSRF_ERRORS = new Set(['Missing CSRF token', 'Invalid CSRF token', 'CSRF token expired'])

/**
 * Wrapper around the native fetch that automatically detects CSRF errors
 * (HTTP 400 with a known CSRF error message) and displays a notification,
 * and redirects to /login on a 401 (session expired or never established).
 *
 * @param {string} url - The URL to fetch.
 * @param {RequestInit} [options] - Standard fetch options.
 * @returns {Promise<Response>} The fetch Response.
 */
export async function apiFetch(url, options) {
  const response = await fetch(url, options)

  if (response.status === 400) {
    let body = null
    try {
      body = await response.clone().json()
    } catch (e) {
      console.debug('apiFetch: response body is not JSON', e)
    }

    if (body && CSRF_ERRORS.has(body.error)) {
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
