/**
 * @file ApiTokenManager.vue
 * @brief Vue component for managing API tokens in Sunshine
 */
<template>
  <div class="token-page">
    <h1>{{$t('auth.title')}}</h1>

    <div id="generate-token-section" class="card">
      <h2>{{$t('auth.generate_new_token')}}</h2>
      <form @submit.prevent="generateToken" class="token-form">
        <div class="scopes-list">
          <div v-for="(scope, idx) in scopes" :key="idx" class="scope-row">
            <select v-model="scope.path" @change="scope.methods = []" required>
              <option value="" disabled>{{$t('auth.select_api_path')}}</option>
              <option v-for="route in apiRoutes" :key="route.path" :value="route.path">
                {{ route.path }}
              </option>
            </select>
            <select v-model="scope.methods" multiple size="3" :disabled="!scope.path" required>
              <option v-for="m in getMethodsForPath(scope.path)" :key="m" :value="m">
                {{ m }}
              </option>
            </select>
            <button type="button" @click="removeScope(idx)">{{$t('auth.remove')}}</button>
          </div>
        </div>
        <button type="button" @click="addScope">{{$t('auth.add_scope')}}</button>
        <button type="submit">{{$t('auth.generate_token')}}</button>
      </form>
      <div v-if="tokenResult" class="token-box">
        <span class="token-warning">{{$t('auth.token_success')}}</span>
        <code>{{ tokenResult }}</code>
      </div>
    </div>
    
    <div id="active-tokens-section" class="card">
      <h2>{{$t('auth.active_tokens')}}</h2>
      <div class="token-table-wrapper">
        <table class="token-table">
          <thead>
            <tr><th>{{$t('auth.hash')}}</th><th>{{$t('auth.username')}}</th><th>{{$t('auth.created')}}</th><th>{{$t('auth.scopes')}}</th><th></th></tr>
          </thead>
          <tbody>
            <tr v-if="!tokens.length"><td colspan="5" style="text-align: center;">{{$t('auth.no_active_tokens')}}</td></tr>
            <tr v-for="t in tokens" :key="t.hash">
              <td>{{ t.hash.substring(0, 8) }}...</td>
              <td>{{ t.username }}</td>
              <td>{{ formatDate(t.created_at) }}</td>
              <td>
                <span v-for="(s, i) in t.scopes" :key="i">
                  {{ s.path }}: [{{ s.methods.join(', ') }}]<br />
                </span>
              </td>
              <td><button class="danger-btn" @click="revokeToken(t.hash)">{{$t('auth.revoke')}}</button></td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
    
    <div id="test-token-section" class="card">
      <h2>{{$t('auth.test_api_token')}}</h2>
      <div class="token-tester">
        <form @submit.prevent="testToken">
          <div>
            <label for="testPath">{{$t('auth.api_path_get_only')}}</label>
            <select id="testPath" v-model="testPath" required>
            <option value="" disabled>{{$t('auth.select_api_path_to_test')}}</option>
              <option v-for="route in apiRoutes.filter(r => r.methods.includes('GET'))" :key="route.path" :value="route.path">
                {{ route.path }}
              </option>
            </select>
          </div>
          <div>
            <label for="testTokenInput">{{$t('auth.token')}}</label>
            <input 
              id="testTokenInput" 
              v-model="testTokenInput" 
              type="password" 
              autocomplete="off" 
              :placeholder="$t('auth.paste_token_here')" 
              required 
            />
          </div>
          <button type="submit">{{$t('auth.test_token')}}</button>
        </form>
        <div v-if="testResult || testError" class="mt-4">
          <b>{{$t('auth.result')}}</b>
          <div v-if="testError" class="alert alert-danger mt-2">{{ testError }}</div>
          <pre v-if="testResult" class="mt-2">{{ testResult }}</pre>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { defineComponent } from 'vue'

/**
 * List of available API routes and their supported methods
 * @type {Array<{path: string, methods: string[]}>}
 */
const API_ROUTES = [
  { path: "/api/pin", methods: ["POST"] },
  { path: "/api/apps", methods: ["GET", "POST"] },
  { path: "/api/logs", methods: ["GET"] },
  { path: "/api/config", methods: ["GET", "POST"] },
  { path: "/api/configLocale", methods: ["GET"] },
  { path: "/api/restart", methods: ["POST"] },
  { path: "/api/reset-display-device-persistence", methods: ["POST"] },
  { path: "/api/password", methods: ["POST"] },
  { path: "/api/apps/([0-9]+)", methods: ["DELETE"] },
  { path: "/api/clients/unpair-all", methods: ["POST"] },
  { path: "/api/clients/list", methods: ["GET"] },
  { path: "/api/clients/unpair", methods: ["POST"] },
  { path: "/api/apps/close", methods: ["POST"] },
  { path: "/api/covers/upload", methods: ["POST"] },
  { path: "/api/token", methods: ["POST"] },
  { path: "/api/tokens", methods: ["GET"] },
  { path: "/api/token/([a-fA-F0-9]+)", methods: ["DELETE"] }
]

export default defineComponent({
  name: 'ApiTokenManager',
  data() {
    return {
      scopes: [{ path: '', methods: [] }],
      tokenResult: '',
      tokens: [],
      apiRoutes: API_ROUTES,
      // Token tester state
      testPath: '',
      testTokenInput: '',
      testResult: '',
      testError: ''
    }
  },
  methods: {
    /**
     * Add a new empty scope to the scopes list
     * @returns {void}
     */
    addScope() {
      this.scopes.push({ path: '', methods: [] })
    },

    /**
     * Remove a scope from the scopes list
     * @param {number} idx - Index of the scope to remove
     * @returns {void}
     */
    removeScope(idx) {
      if (this.scopes.length > 1) {
        this.scopes.splice(idx, 1)
      }
    },

    /**
     * Get available HTTP methods for a given API path
     * @param {string} path - The API path
     * @returns {string[]} Array of HTTP methods
     */
    getMethodsForPath(path) {
      const found = this.apiRoutes.find(r => r.path === path)
      return found ? found.methods : ["GET", "POST", "DELETE", "PATCH", "PUT"]
    },

    /**
     * Generate a new API token with selected scopes
     * @returns {Promise<void>}
     */
    async generateToken() {
      const filtered = this.scopes.filter(s => s.path && s.methods.length)
      if (!filtered.length) {
        alert(this.$t('auth.please_specify_scope'))
        return
      }
      
      try {
        const res = await fetch('/api/token', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ scopes: filtered })
        })
        const data = await res.json()
        
        if (res.ok && data.token) {
          this.tokenResult = data.token
          this.loadTokens()
        } else {
          this.tokenResult = `Error: ${data.error || this.$t('auth.failed_to_generate_token')}`
        }
      } catch(e) {
        this.tokenResult = `${this.$t('auth.request_failed')}: ${e.message}`
      }
    },
    
    /**
     * Load active tokens from the server
     * @returns {Promise<void>}
     */
    async loadTokens() {
      try {
        const res = await fetch('/api/tokens')
        if(res.ok) {
          this.tokens = await res.json()
        } else {
          console.error("Failed to load tokens")
          this.tokens = []
        }
      } catch(e) {
        console.error("Error fetching tokens:", e)
        this.tokens = []
      }
    },
    
    /**
     * Revoke an active token
     * @param {string} hash - The token hash to revoke
     * @returns {Promise<void>}
     */
    async revokeToken(hash) {
      if (!confirm(this.$t('auth.confirm_revoke'))) return
      
      try {
        const res = await fetch(`/api/token/${hash}`, { method: 'DELETE' })
        if(res.ok) {
          this.loadTokens()
        } else {
          alert(this.$t('auth.failed_to_revoke_token'))
        }
      } catch(e) {
        alert(`${this.$t('auth.error_revoking_token')}: ${e.message}`)
      }
    },
    
    /**
     * Format timestamp to localized date string
     * @param {number} ts - Unix timestamp
     * @returns {string} Formatted date string
     */
    formatDate(ts) {
      return new Date(ts * 1000).toLocaleString()
    },
    
    /**
     * Test an API token against a selected endpoint
     * @returns {Promise<void>}
     */
    async testToken() {
      this.testResult = ''
      this.testError = ''
      
      if (!this.testPath || !this.testTokenInput) {
      this.testError = this.$t('auth.select_api_path_and_token')
        return
      }
      
      try {
        const res = await fetch(this.testPath, {
          method: 'GET',
          headers: {
            'Authorization': 'Bearer ' + this.testTokenInput
          },
          credentials: 'omit'
        })

        const contentType = res.headers.get("content-type")
        let bodyText = await res.text()
        let isJson = contentType && contentType.indexOf("application/json") !== -1 && bodyText
        
        if (isJson) {
          try {
            this.testResult = JSON.stringify(JSON.parse(bodyText), null, 2)
          } catch {
            this.testResult = bodyText
          }
        } else {
          this.testResult = bodyText
        }

        if (!res.ok) {
          this.testError = `HTTP ${res.status}: ${res.statusText}`
        } else {
          this.testError = ''
        }

      } catch (e) {
        this.testError = `${this.$t('auth.request_failed')}: ${e.message}`
      }
    }
  },
  
  /**
   * Component mounted lifecycle hook
   * @returns {void}
   */
  mounted() {
    this.loadTokens();
    // Set the document title on mount
    if (this.$t) {
      document.title = this.$t('auth.title');
    }
    // Watch for locale changes and update title
    if (this.$i18n && this.$i18n.watchLocale) {
      this.$i18n.watchLocale(() => {
        document.title = this.$t('auth.title');
      });
    } else if (this.$i18n && this.$i18n.locale) {
      this.$watch(() => this.$i18n.locale, () => {
        document.title = this.$t('auth.title');
      });
    }
  }
})
</script>
