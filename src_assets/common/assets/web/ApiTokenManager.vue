/**
 * @file ApiTokenManager.vue
 * @brief Vue component for managing API tokens in Sunshine
 */
<template>
  <div class="token-page">
    <h1>API Token Management</h1>

    <div id="generate-token-section" class="card">
      <h2>Generate New Token</h2>
      <form @submit.prevent="generateToken" class="token-form">
        <div class="scopes-list">
          <div v-for="(scope, idx) in scopes" :key="idx" class="scope-row">
            <select v-model="scope.path" @change="scope.methods = []" required>
              <option value="" disabled>Select API Path</option>
              <option v-for="route in apiRoutes" :key="route.path" :value="route.path">
                {{ route.path }}
              </option>
            </select>
            <select v-model="scope.methods" multiple size="3" :disabled="!scope.path" required>
              <option v-for="m in getMethodsForPath(scope.path)" :key="m" :value="m">
                {{ m }}
              </option>
            </select>
            <button type="button" @click="removeScope(idx)">Remove</button>
          </div>
        </div>
        <button type="button" @click="addScope">Add Scope</button>
        <button type="submit">Generate Token</button>
      </form>
      <div v-if="tokenResult" class="token-box">
        <span class="token-warning">Success! Copy this token now as you will not see it again:</span>
        <code>{{ tokenResult }}</code>
      </div>
    </div>
    
    <div id="active-tokens-section" class="card">
      <h2>Active Tokens</h2>
      <div class="token-table-wrapper">
        <table class="token-table">
          <thead>
            <tr><th>Hash</th><th>Username</th><th>Created</th><th>Scopes</th><th></th></tr>
          </thead>
          <tbody>
            <tr v-if="!tokens.length"><td colspan="5" style="text-align: center;">No active tokens.</td></tr>
            <tr v-for="t in tokens" :key="t.hash">
              <td>{{ t.hash.substring(0, 8) }}...</td>
              <td>{{ t.username }}</td>
              <td>{{ formatDate(t.created_at) }}</td>
              <td>
                <span v-for="(s, i) in t.scopes" :key="i">
                  {{ s.path }}: [{{ s.methods.join(', ') }}]<br />
                </span>
              </td>
              <td><button class="danger-btn" @click="revokeToken(t.hash)">Revoke</button></td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
    
    <div id="test-token-section" class="card">
      <h2>Test API Token</h2>
      <div class="token-tester">
        <form @submit.prevent="testToken">
          <div>
            <label for="testPath">API Path (GET requests only)</label>
            <select id="testPath" v-model="testPath" required>
              <option value="" disabled>Select API Path to Test</option>
              <option v-for="route in apiRoutes.filter(r => r.methods.includes('GET'))" :key="route.path" :value="route.path">
                {{ route.path }}
              </option>
            </select>
          </div>
          <div>
            <label for="testTokenInput">Token</label>
            <input 
              id="testTokenInput" 
              v-model="testTokenInput" 
              type="password" 
              autocomplete="off" 
              placeholder="Paste API token here" 
              required 
            />
          </div>
          <button type="submit">Test Token</button>
        </form>
        <div v-if="testResult || testError" class="mt-4">
          <b>Result:</b>
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
 * @brief List of available API routes and their supported methods
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
     * @brief Add a new empty scope to the scopes list
     */
    addScope() {
      this.scopes.push({ path: '', methods: [] })
    },
    
    /**
     * @brief Remove a scope from the scopes list
     * @param {number} idx - Index of the scope to remove
     */
    removeScope(idx) {
      if (this.scopes.length > 1) {
        this.scopes.splice(idx, 1)
      }
    },
    
    /**
     * @brief Get available HTTP methods for a given API path
     * @param {string} path - The API path
     * @returns {string[]} Array of HTTP methods
     */
    getMethodsForPath(path) {
      const found = this.apiRoutes.find(r => r.path === path)
      return found ? found.methods : ["GET", "POST", "DELETE", "PATCH", "PUT"]
    },
    
    /**
     * @brief Generate a new API token with selected scopes
     */
    async generateToken() {
      const filtered = this.scopes.filter(s => s.path && s.methods.length)
      if (!filtered.length) {
        alert('Please specify at least one path and corresponding method(s).')
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
          this.tokenResult = `Error: ${data.error || 'Failed to generate token.'}`
        }
      } catch(e) {
        this.tokenResult = `Request failed: ${e.message}`
      }
    },
    
    /**
     * @brief Load active tokens from the server
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
     * @brief Revoke an active token
     * @param {string} hash - The token hash to revoke
     */
    async revokeToken(hash) {
      if (!confirm('Are you sure you want to revoke this token? This action cannot be undone.')) return
      
      try {
        const res = await fetch(`/api/token/${hash}`, { method: 'DELETE' })
        if(res.ok) {
          this.loadTokens()
        } else {
          alert("Failed to revoke token.")
        }
      } catch(e) {
        alert(`Error revoking token: ${e.message}`)
      }
    },
    
    /**
     * @brief Format timestamp to localized date string
     * @param {number} ts - Unix timestamp
     * @returns {string} Formatted date string
     */
    formatDate(ts) {
      return new Date(ts * 1000).toLocaleString()
    },
    
    /**
     * @brief Test an API token against a selected endpoint
     */
    async testToken() {
      this.testResult = ''
      this.testError = ''
      
      if (!this.testPath || !this.testTokenInput) {
        this.testError = 'Please select an API path and provide a token.'
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
        this.testError = `Request failed: ${e.message}`
      }
    }
  },
  
  /**
   * @brief Component mounted lifecycle hook
   */
  mounted() {
    this.loadTokens()
  }
})
</script>
