import { notifyKey } from './Notification.vue'

/**
 * The set of error messages that indicate a CSRF validation failure.
 */
const CSRF_ERRORS = new Set(['Missing CSRF token', 'Invalid CSRF token', 'CSRF token expired'])

/**
 * Wrapper around the native fetch that automatically detects CSRF errors
 * (HTTP 400 with a known CSRF error message) and displays a notification.
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

  return response
}
