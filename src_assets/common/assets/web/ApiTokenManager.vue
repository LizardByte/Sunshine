<template>
  <div class="container py-3 token-page">
    <h1 class="h3 mb-4">{{ $t('auth.title') }}</h1>

    <!-- Generate token -->
    <div id="generate-token-section" class="card mb-4">
      <div class="card-header d-flex align-items-center">
        <h2 class="h5 mb-0">{{ $t('auth.generate_new_token') }}</h2>
      </div>
      <div class="card-body">
        <p v-if="$te('auth.generate_token_help')" class="form-text mb-3">{{ $t('auth.generate_token_help') }}</p>

        <form @submit.prevent="generateToken" novalidate>
          <div class="vstack gap-3">
            <div v-for="(scope, idx) in scopes" :key="scope.id ?? idx" class="row g-3 align-items-end">
              <div class="col-12 col-md-5">
                <label :for="'scope-path-' + idx" class="form-label">{{ $t('auth.select_api_path') }}</label>
                <select :id="'scope-path-' + idx" class="form-select" v-model="scope.path"
                  @change="onScopePathChange(scope)">
                  <option value="" disabled>{{ $t('auth.select_api_path') }}</option>
                  <option v-for="route in apiRoutes.filter(r => r.selectable !== false)" :key="route.path"
                    :value="route.path">
                    {{ route.path }}
                  </option>
                </select>
              </div>

              <div class="col-12 col-md-5">
                <label :for="'scope-methods-' + idx" class="form-label">{{ $t('auth.scopes') }}</label>
                <select :id="'scope-methods-' + idx" class="form-select" v-model="scope.methods" multiple size="4"
                  :disabled="!scope.path">
                  <option v-for="m in getMethodsForPath(scope.path)" :key="m" :value="m">{{ m }}</option>
                </select>
              </div>

              <div class="col-12 col-md-2 d-grid">
                <button type="button" class="btn btn-danger" :aria-label="$t('auth.remove')"
                  @click="removeScope(idx)" :disabled="scopes.length === 1 && !scope.path && !scope.methods?.length">
                  {{ $t('auth.remove') }}
                </button>
              </div>
            </div>

            <div class="d-flex align-items-center gap-2">
              <button type="button" class="btn btn-primary" @click="addScope">
                {{ $t('auth.add_scope') }}
              </button>
              <span class="text-body-secondary small" v-if="isGenerateDisabled">
                {{ $t('auth.generate_disabled_hint') }}
              </span>
            </div>

            <div v-if="validScopes.length" class="mt-1">
              <strong class="me-2">{{ $t('auth.selected_scopes') }}:</strong>
              <span v-for="(s, i) in validScopes" :key="i" class="d-inline-block me-2 mb-2">
                <span class="badge text-bg-secondary me-1">{{ s.path }}</span>
                <span v-for="m in s.methods" :key="m" class="badge text-bg-info text-uppercase me-1">{{ m }}</span>
              </span>
            </div>

            <div class="d-flex gap-2">
              <button type="submit" class="btn btn-primary" :disabled="isGenerateDisabled || isGenerating">
                <span v-if="!isGenerating">{{ $t('auth.generate_token') }}</span>
                <span v-else>{{ $t('auth.loading') }}</span>
              </button>
              <button type="button" class="btn btn-secondary" @click="resetForm" :disabled="isGenerating">
                {{ $t('_common.cancel') }}
              </button>
            </div>

            <div v-if="displayedToken" class="alert alert-success mt-2" role="status" aria-live="polite">
              <div class="mb-2 fw-medium">{{ $t('auth.token_success') }}</div>
              <div class="input-group">
                <input type="text" class="form-control" :value="displayedToken" readonly />
                <button type="button" class="btn btn-success" @click="copyToken" :disabled="tokenCopied"
                  :title="$t('auth.copy_token')">
                  {{ tokenCopied ? $t('auth.token_copied') : $t('auth.copy_token') }}
                </button>
              </div>
            </div>
          </div>
        </form>
      </div>
    </div>

    <!-- Active tokens -->
    <div id="active-tokens-section" class="card mb-4">
      <div class="card-header d-flex align-items-center">
        <h2 class="h5 mb-0">{{ $t('auth.active_tokens') }}</h2>
        <button type="button" class="btn btn-secondary ms-auto" @click="loadTokens" :disabled="isLoadingTokens">
          {{ $t('auth.refresh') }}
        </button>
      </div>
      <div class="card-body">
        <div class="row g-3 align-items-end mb-3">
          <div class="col-12 col-md-6">
            <label class="form-label">{{ $t('auth.search_tokens') }}</label>
            <input type="text" class="form-control" v-model="tokenFilter" :placeholder="$t('auth.search_tokens')"
              autocomplete="off" @input="onFilterInput" />
          </div>
          <div class="col-6 col-md-3">
            <label class="form-label">{{ $t('auth.sort_field') }}</label>
            <select class="form-select" v-model="sortField">
              <option value="created_at">{{ $t('auth.created') }}</option>
              <option value="username">{{ $t('auth.username') }}</option>
              <option value="hash">{{ $t('auth.hash') }}</option>
            </select>
          </div>
          <div class="col-6 col-md-3">
            <label class="form-label">{{ $t('auth.sort_direction') }}</label>
            <select class="form-select" v-model="sortDir">
              <option value="desc">{{ $t('auth.desc') }}</option>
              <option value="asc">{{ $t('auth.asc') }}</option>
            </select>
          </div>
        </div>

        <div class="table-responsive">
          <table class="table table-sm align-middle">
            <thead>
              <tr>
                <th>{{ $t('auth.hash') }}</th>
                <th>{{ $t('auth.username') }}</th>
                <th>{{ $t('auth.created') }}</th>
                <th>{{ $t('auth.scopes') }}</th>
                <th class="text-end"></th>
              </tr>
            </thead>
            <tbody>
              <tr v-if="isLoadingTokens">
                <td colspan="5" class="text-center">{{ $t('auth.loading') }}</td>
              </tr>
              <tr v-else-if="!sortedTokens.length">
                <td colspan="5" class="text-center">
                  {{ tokens.length ? $t('auth.no_matching_tokens') : $t('auth.no_active_tokens') }}
                </td>
              </tr>
              <tr v-for="t in sortedTokens" :key="t.hash">
                <td class="text-truncate" style="max-width: 160px;">
                  <div class="d-flex align-items-center gap-2">
                    <code class="text-truncate" :title="t.hash">{{ t.hash }}</code>
                    <button type="button" class="btn btn-secondary btn-sm" @click.prevent="copyHash(t.hash)"
                      :disabled="copiedHash === t.hash">
                      {{ copiedHash === t.hash ? $t('auth.hash_copied') : $t('auth.copy_hash') }}
                    </button>
                  </div>
                </td>
                <td class="text-truncate" style="max-width: 160px;">{{ t.username }}</td>
                <td :title="formatFullDate(t.created_at)">{{ formatDate(t.created_at) }}</td>
                <td>
                  <div class="d-flex flex-wrap gap-1">
                    <span v-for="(s, i) in t.scopes" :key="i" class="d-inline-flex align-items-center">
                      <span class="badge text-bg-secondary me-1">{{ s.path }}</span>
                      <span class="badge text-bg-info text-uppercase me-1" v-for="m in s.methods" :key="m">{{ m
                        }}</span>
                    </span>
                  </div>
                </td>
                <td class="text-end">
                  <button class="btn btn-danger btn-sm" @click="revokeToken(t.hash)"
                    :disabled="revoking === t.hash">
                    {{ $t('auth.revoke') }}
                  </button>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </div>

    <!-- Test token -->
    <div id="test-token-section" class="card mb-4">
      <div class="card-header">
        <h2 class="h5 mb-0">{{ $t('auth.test_api_token') }}</h2>
      </div>
      <div class="card-body">
        <p v-if="$te('auth.testing_help')" class="form-text">{{ $t('auth.testing_help') }}</p>
        <form @submit.prevent="testToken" class="row g-3">
          <div class="col-12 col-md-6">
            <label for="testPath" class="form-label">{{ $t('auth.api_path_get_only') }}</label>
            <select id="testPath" class="form-select" v-model="testPath" required>
              <option value="" disabled>{{ $t('auth.select_api_path_to_test') }}</option>
              <option v-for="route in apiRoutes.filter(r => (r.selectable !== false) && r.methods.includes('GET'))"
                :key="route.path" :value="route.path">
                {{ route.path }}
              </option>
            </select>
          </div>
          <div class="col-12 col-md-6">
            <label for="testTokenInput" class="form-label">{{ $t('auth.token') }}</label>
            <input id="testTokenInput" v-model="testTokenInput" type="password" class="form-control" autocomplete="off"
              :placeholder="$t('auth.paste_token_here')" required />
          </div>
          <div class="col-12">
            <button type="submit" class="btn btn-primary" :disabled="isTesting || !testPath || !testTokenInput">
              <span v-if="!isTesting">{{ $t('auth.test_token') }}</span>
              <span v-else>{{ $t('auth.loading') }}</span>
            </button>
          </div>
        </form>

        <div v-if="testResult || testError" class="mt-3">
          <div class="fw-semibold mb-2">{{ $t('auth.result') }}</div>
          <div v-if="testError" class="alert alert-danger">{{ testError }}</div>
          <pre v-if="testResult" class="bg-body-tertiary p-2 rounded small mb-0">{{ testResult }}</pre>
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
      displayedToken: '',
      tokenCopied: false,
      tokens: [],
      apiRoutes: API_ROUTES,
      tokenFilter: '',
      debouncedFilter: '',
      sortField: 'created_at',
      sortDir: 'desc',
      copiedHash: '',
      isGenerating: false,
      isLoadingTokens: false,
      revoking: '',
      // Token tester state
      testPath: '',
      testTokenInput: '',
      testResult: '',
      testError: '',
      isTesting: false
    }
  },
  computed: {
    validScopes() { return this.scopes.filter(s => s.path && s.methods && s.methods.length) },
    isGenerateDisabled() { return !this.validScopes.length || this.isGenerating },
    filteredTokens() {
      const filter = (this.debouncedFilter || '').trim().toLowerCase()
      if (!filter) return this.tokens
      return this.tokens.filter(t =>
        t.username.toLowerCase().includes(filter) ||
        t.hash.toLowerCase().includes(filter) ||
        (t.scopes || []).some(s => s.path.toLowerCase().includes(filter))
      )
    },
    sortedTokens() {
      const arr = [...this.filteredTokens]
      arr.sort((a, b) => {
        let av, bv
        if (this.sortField === 'created_at') { av = a.created_at; bv = b.created_at }
        else if (this.sortField === 'username') { av = a.username.toLowerCase(); bv = b.username.toLowerCase() }
        else { av = a.hash.toLowerCase(); bv = b.hash.toLowerCase() }
        if (av < bv) return this.sortDir === 'asc' ? -1 : 1
        if (av > bv) return this.sortDir === 'asc' ? 1 : -1
        return 0
      })
      return arr
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
    onScopePathChange(scope) {
      scope.methods = []
    },
    /**
     * Remove a scope from the scopes list
     * @param {number} idx - Index of the scope to remove
     * @returns {void}
     */
    removeScope(idx) {
      if (this.scopes.length > 1) {
        this.scopes.splice(idx, 1)
      } else {
        // Clear instead of removing the last row entirely for better UX
        this.scopes[0] = { path: '', methods: [] }
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
     * Reset the token generation form to its initial state
     * @returns {void}
     */
    resetForm() {
      this.scopes = [{ path: '', methods: [] }]
      this.tokenResult = ''
      this.displayedToken = ''
      this.tokenCopied = false
    },

    /**
     * Generate a new API token with selected scopes
     * @returns {Promise<void>}
     */
    async generateToken() {
      const filtered = this.validScopes
      if (!filtered.length) {
        alert(this.$t('auth.please_specify_scope'))
        return
      }
      this.isGenerating = true
      this.tokenCopied = false
      try {
        const res = await fetch('/api/token', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ scopes: filtered })
        })
        const data = await res.json().catch(() => ({}))
        if (res.ok && data.token) {
          this.tokenResult = data.token
          this.displayedToken = data.token
          await this.loadTokens()
        } else {
          this.tokenResult = `Error: ${data.error || this.$t('auth.failed_to_generate_token')}`
          this.displayedToken = ''
        }
      } catch (e) {
        this.tokenResult = `${this.$t('auth.request_failed')}: ${e.message}`
        this.displayedToken = ''
      } finally {
        this.isGenerating = false
      }
    },

    /**
     * Load active tokens from the server
     * @returns {Promise<void>}
     */
    async loadTokens() {
      this.isLoadingTokens = true
      try {
        const res = await fetch('/api/tokens')
        if (res.ok) {
          this.tokens = await res.json()
        } else {
          console.error('Failed to load tokens')
          this.tokens = []
        }
      } catch (e) {
        console.error('Error fetching tokens:', e)
        this.tokens = []
      } finally {
        this.isLoadingTokens = false
      }
    },

    /**
     * Revoke an active token
     * @param {string} hash - The token hash to revoke
     * @returns {Promise<void>}
     */
    async revokeToken(hash) {
      if (!confirm(this.$t('auth.confirm_revoke'))) return
      this.revoking = hash
      try {
        const res = await fetch(`/api/token/${hash}`, { method: 'DELETE' })
        if (res.ok) {
          await this.loadTokens()
        } else {
          alert(this.$t('auth.failed_to_revoke_token'))
        }
      } catch (e) {
        alert(`${this.$t('auth.error_revoking_token')}: ${e.message}`)
      } finally {
        this.revoking = ''
      }
    },

    /**
     * Format timestamp to localized date string
     * @param {number} ts - Unix timestamp
     * @returns {string} Formatted date string
     */
    formatDate(ts) {
      const d = new Date(ts * 1000)
      return d.toLocaleDateString() + ' ' + d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
    },

    /**
     * Format timestamp to full localized date and time string
     * @param {number} ts - Unix timestamp
     * @returns {string} Full formatted date string
     */
    formatFullDate(ts) {
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
      this.isTesting = true
      try {
        const res = await fetch(this.testPath, {
          method: 'GET',
          headers: { 'Authorization': 'Bearer ' + this.testTokenInput },
          credentials: 'omit'
        })
        const contentType = res.headers.get('content-type')
        let bodyText = await res.text()
        let isJson = contentType && contentType.indexOf('application/json') !== -1 && bodyText
        if (isJson) {
          try { this.testResult = JSON.stringify(JSON.parse(bodyText), null, 2) } catch { this.testResult = bodyText }
        } else { this.testResult = bodyText }
        if (!res.ok) {
          this.testError = `HTTP ${res.status}: ${res.statusText}`
        } else { this.testError = '' }
      } catch (e) {
        this.testError = `${this.$t('auth.request_failed')}: ${e.message}`
      } finally {
        this.isTesting = false
      }
    },
    copyToken() {
      if (!this.displayedToken) return
      navigator.clipboard?.writeText(this.displayedToken).then(() => {
        this.tokenCopied = true
        setTimeout(() => { this.tokenCopied = false }, 3000)
      })
    },
    onFilterInput() {
      clearTimeout(this._filterTimer)
      this._filterTimer = setTimeout(() => { this.debouncedFilter = this.tokenFilter }, 180)
    },
    copyHash(hash) {
      navigator.clipboard?.writeText(hash).then(() => {
        this.copiedHash = hash
        setTimeout(() => { if (this.copiedHash === hash) this.copiedHash = '' }, 2000)
      })
    }
  },
  mounted() {
    this.loadTokens()
    if (this.$t) { document.title = this.$t('auth.title') }
    if (this.$i18n && this.$i18n.watchLocale) {
      this.$i18n.watchLocale(() => { document.title = this.$t('auth.title') })
    } else if (this.$i18n && this.$i18n.locale) {
      this.$watch(() => this.$i18n.locale, () => { document.title = this.$t('auth.title') })
    }
  }
})
</script>

<style scoped>
.token-page h1 {
  margin-bottom: 1rem;
}

.help {
  font-size: 0.9rem;
  opacity: 0.8;
}

.scope-row {
  display: flex;
  flex-wrap: wrap;
  gap: 0.75rem;
  align-items: flex-end;
  margin-bottom: 0.5rem;
}

.scope-col {
  display: flex;
  flex-direction: column;
  min-width: 200px;
}

.scope-label {
  font-size: 0.75rem;
  text-transform: uppercase;
  letter-spacing: .05em;
  opacity: .7;
  margin-bottom: 0.15rem;
}

.scope-actions {
  display: flex;
  align-items: center;
}

.scope-toolbar {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 0.5rem;
}

.scope-summary {
  font-size: 0.85rem;
  line-height: 1.4;
}

.method-chip-group {
  display: inline-block;
  margin-right: .5rem;
  margin-top: .25rem;
}

.path-chip {
  background: var(--color-bg-alt, #333);
  padding: 2px 6px;
  border-radius: 4px 0 0 4px;
  font-weight: 600;
}

.method-chip {
  background: var(--color-accent, #555);
  padding: 2px 6px;
  border-radius: 0 4px 4px 0;
  margin-left: 1px;
  font-size: 0.7rem;
  text-transform: uppercase;
}

.token-box {
  background: rgba(255, 255, 255, 0.05);
  padding: 0.75rem;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 6px;
}

.token-box code {
  display: block;
  word-break: break-all;
  margin: 0.35rem 0;
}

.token-actions {
  display: flex;
  align-items: center;
  gap: .75rem;
}

.copy-feedback {
  font-size: 0.75rem;
  color: var(--color-success, #4caf50);
}

.token-filter input {
  width: 100%;
  max-width: 320px;
}

.filter-row {
  display: flex;
  flex-wrap: wrap;
  gap: .75rem;
  align-items: flex-end;
}

.sort-controls {
  display: flex;
  gap: .5rem;
}

.sort-controls label {
  display: flex;
  flex-direction: column;
  font-size: .65rem;
  text-transform: uppercase;
  letter-spacing: .05em;
  opacity: .75;
}

.hash-wrapper {
  cursor: pointer;
  display: inline-block;
  min-width: 70px;
}

.hash-wrapper:focus {
  outline: 1px solid var(--color-accent, #555);
  outline-offset: 2px;
}

.mini-btn {
  font-size: .6rem;
  margin-left: .25rem;
  padding: 2px 4px;
}

.scope-cell {
  display: flex;
  flex-wrap: wrap;
  gap: .4rem;
}

.scope-pill {
  display: flex;
  flex-wrap: wrap;
  background: rgba(255, 255, 255, 0.06);
  padding: 2px 4px;
  border-radius: 4px;
}

.scope-pill+.scope-pill {
  margin-top: 3px;
}

.pill-path {
  font-weight: 600;
  margin-right: 4px;
}

.pill-method {
  background: var(--color-accent, #444);
  padding: 0 4px;
  border-radius: 3px;
  margin-right: 2px;
  font-size: 0.65rem;
  text-transform: uppercase;
}

.tester-grid {
  display: flex;
  flex-wrap: wrap;
  gap: 1rem;
}

.flex-row {
  display: flex;
  align-items: center;
  gap: .75rem;
}

.ml-auto {
  margin-left: auto;
}

.ml-2 {
  margin-left: .5rem;
}

.mt-2 {
  margin-top: .5rem;
}

.mt-3 {
  margin-top: .75rem;
}

.mt-4 {
  margin-top: 1rem;
}

.text-muted {
  opacity: .6;
  font-size: 0.75rem;
}

@media (max-width: 680px) {
  .scope-row {
    flex-direction: column;
    align-items: stretch;
  }

  .scope-actions {
    justify-content: flex-end;
  }

  .tester-grid {
    flex-direction: column;
  }
}
</style>
