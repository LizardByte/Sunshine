import { reactive } from 'vue'
import { apiFetch } from '@/utils/fetch_utils'

export const authState = reactive({
  configured: null,
  authenticated: null,
})

/**
 * Refresh authState from the backend.
 *
 * @returns {Promise<typeof authState>}
 */
export async function refreshAuthStatus() {
  const r = await apiFetch('/api/auth/status')
  const data = await r.json()
  authState.configured = data.configured
  authState.authenticated = data.authenticated
  return authState
}
