/**
 * @file ApiTokenManager.test.js
 * @brief Unit tests for the API Token Manager component
 */

import { mount } from '@vue/test-utils'
import ApiTokenManager from '@/ApiTokenManager.vue'
import { flushPromises } from '@vue/test-utils'

describe('ApiTokenManager', () => {
  let wrapper

  beforeEach(() => {
    // Reset fetch mock
    fetch.mockClear()
    
    // Mock successful token fetch by default
    fetch.mockResolvedValue({
      ok: true,
      json: () => Promise.resolve([])
    })
    
    wrapper = mount(ApiTokenManager)
  })
  describe('Component initialization', () => {
    it('should render the main title', () => {
      expect(wrapper.find('h1').text()).toBe('API Token Management')
    })

    it('should render all three main sections', () => {
      expect(wrapper.find('#generate-token-section').exists()).toBe(true)
      expect(wrapper.find('#active-tokens-section').exists()).toBe(true)
      expect(wrapper.find('#test-token-section').exists()).toBe(true)
    })

    it('should initialize with one empty scope', () => {
      expect(wrapper.vm.scopes).toHaveLength(1)
      expect(wrapper.vm.scopes[0]).toEqual({ path: '', methods: [] })
    })

    it('should initialize with empty token states', () => {
      expect(wrapper.vm.tokenResult).toBe('')
      expect(wrapper.vm.tokens).toEqual([])
      expect(wrapper.vm.testPath).toBe('')
      expect(wrapper.vm.testTokenInput).toBe('')
      expect(wrapper.vm.testResult).toBe('')
      expect(wrapper.vm.testError).toBe('')
    })

    it('should initialize with API routes', () => {
      expect(wrapper.vm.apiRoutes).toBeDefined()
      expect(wrapper.vm.apiRoutes.length).toBeGreaterThan(0)
      expect(wrapper.vm.apiRoutes).toEqual(expect.arrayContaining([
        expect.objectContaining({
          path: expect.any(String),
          methods: expect.any(Array)
        })
      ]))
    })

    it('should have correct API routes structure', () => {
      const expectedRoutes = [
        { path: "/api/pin", methods: ["POST"] },
        { path: "/api/apps", methods: ["GET", "POST"] },
        { path: "/api/logs", methods: ["GET"] },
        { path: "/api/config", methods: ["GET", "POST"] },
        { path: "/api/configLocale", methods: ["GET"] }
      ]

      expectedRoutes.forEach(expectedRoute => {
        const foundRoute = wrapper.vm.apiRoutes.find(r => r.path === expectedRoute.path)
        expect(foundRoute).toBeDefined()
        expect(foundRoute.methods).toEqual(expectedRoute.methods)
      })
    })

    it('should load tokens on mount', () => {
      expect(fetch).toHaveBeenCalledWith('/api/tokens')
    })
  })
  describe('Scope management', () => {
    it('should add a new scope when addScope is called', async () => {
      await wrapper.vm.addScope()
      
      expect(wrapper.vm.scopes).toHaveLength(2)
      expect(wrapper.vm.scopes[1]).toEqual({ path: '', methods: [] })
    })

    it('should remove a scope when removeScope is called', async () => {
      // Add another scope first
      await wrapper.vm.addScope()
      expect(wrapper.vm.scopes).toHaveLength(2)
      
      // Remove the first scope
      await wrapper.vm.removeScope(0)
      expect(wrapper.vm.scopes).toHaveLength(1)
    })

    it('should remove scope from middle of array', async () => {
      // Add two more scopes
      await wrapper.vm.addScope()
      await wrapper.vm.addScope()
      expect(wrapper.vm.scopes).toHaveLength(3)
      
      // Remove the middle scope
      await wrapper.vm.removeScope(1)
      expect(wrapper.vm.scopes).toHaveLength(2)
    })

    it('should not remove the last scope', async () => {
      expect(wrapper.vm.scopes).toHaveLength(1)
      
      await wrapper.vm.removeScope(0)
      expect(wrapper.vm.scopes).toHaveLength(1)
    })

    it('should get correct methods for a given path', () => {
      const methods = wrapper.vm.getMethodsForPath('/api/apps')
      expect(methods).toEqual(['GET', 'POST'])
      
      const unknownMethods = wrapper.vm.getMethodsForPath('/unknown/path')
      expect(unknownMethods).toEqual(['GET', 'POST', 'DELETE', 'PATCH', 'PUT'])
    })

    it('should get methods for all defined API routes', () => {
      const routes = [
        { path: '/api/pin', expected: ['POST'] },
        { path: '/api/logs', expected: ['GET'] },
        { path: '/api/config', expected: ['GET', 'POST'] },
        { path: '/api/configLocale', expected: ['GET'] },
        { path: '/api/restart', expected: ['POST'] }
      ]

      routes.forEach(({ path, expected }) => {
        expect(wrapper.vm.getMethodsForPath(path)).toEqual(expected)
      })
    })
  })

  describe('Token generation', () => {
    it('should show alert when no scopes are configured', async () => {
      global.alert = jest.fn()
      
      await wrapper.vm.generateToken()
      
      expect(global.alert).toHaveBeenCalledWith(
        'Please specify at least one path and corresponding method(s).'
      )
    })

    it('should generate token with valid scopes', async () => {
      const mockToken = 'test-token-123'
      fetch.mockResolvedValueOnce({
        ok: true,
        json: () => Promise.resolve({ token: mockToken })
      })

      // Set up valid scope
      wrapper.vm.scopes = [{ path: '/api/apps', methods: ['GET'] }]
      
      await wrapper.vm.generateToken()
      
      expect(fetch).toHaveBeenCalledWith('/api/token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ scopes: [{ path: '/api/apps', methods: ['GET'] }] })
      })
      
      expect(wrapper.vm.tokenResult).toBe(mockToken)
    })

    it('should handle token generation success without token field', async () => {
      fetch.mockResolvedValueOnce({
        ok: true,
        json: () => Promise.resolve({ message: 'success but no token' })
      })

      wrapper.vm.scopes = [{ path: '/api/apps', methods: ['GET'] }]
      
      await wrapper.vm.generateToken()
      
      expect(wrapper.vm.tokenResult).toBe('Error: Failed to generate token.')
    })

    it('should handle token generation errors with error message', async () => {
      fetch.mockResolvedValueOnce({
        ok: false,
        json: () => Promise.resolve({ error: 'Invalid request' })
      })

      wrapper.vm.scopes = [{ path: '/api/apps', methods: ['GET'] }]
      
      await wrapper.vm.generateToken()
      
      expect(wrapper.vm.tokenResult).toBe('Error: Invalid request')
    })

    it('should handle token generation errors without error message', async () => {
      fetch.mockResolvedValueOnce({
        ok: false,
        json: () => Promise.resolve({})
      })

      wrapper.vm.scopes = [{ path: '/api/apps', methods: ['GET'] }]
      
      await wrapper.vm.generateToken()
      
      expect(wrapper.vm.tokenResult).toBe('Error: Failed to generate token.')
    })

    it('should handle network errors during token generation', async () => {
      fetch.mockRejectedValueOnce(new Error('Network error'))

      wrapper.vm.scopes = [{ path: '/api/apps', methods: ['GET'] }]
      
      await wrapper.vm.generateToken()
      
      expect(wrapper.vm.tokenResult).toBe('Request failed: Network error')
    })

    it('should call loadTokens after successful token generation', async () => {
      const mockToken = 'test-token-123'
      fetch.mockResolvedValueOnce({
        ok: true,
        json: () => Promise.resolve({ token: mockToken })
      })

      const loadTokensSpy = jest.spyOn(wrapper.vm, 'loadTokens')
      wrapper.vm.scopes = [{ path: '/api/apps', methods: ['GET'] }]
      
      await wrapper.vm.generateToken()
      
      expect(loadTokensSpy).toHaveBeenCalled()
    })
  })

  describe('Token management', () => {
    it('should load tokens successfully', async () => {
      const mockTokens = [
        {
          hash: 'abc123def456',
          username: 'testuser',
          created_at: 1640995200,
          scopes: [{ path: '/api/apps', methods: ['GET'] }]
        }
      ]
      
      fetch.mockResolvedValueOnce({
        ok: true,
        json: () => Promise.resolve(mockTokens)
      })
      
      await wrapper.vm.loadTokens()
      
      expect(wrapper.vm.tokens).toEqual(mockTokens)
    })

    it('should handle token loading errors', async () => {
      fetch.mockResolvedValueOnce({
        ok: false
      })
      
      await wrapper.vm.loadTokens()
      
      expect(wrapper.vm.tokens).toEqual([])
    })

    it('should handle network errors during token loading', async () => {
      fetch.mockRejectedValueOnce(new Error('Network error'))
      
      await wrapper.vm.loadTokens()
      
      expect(wrapper.vm.tokens).toEqual([])
    })

    it('should revoke token with user confirmation', async () => {
      global.confirm = jest.fn(() => true)
      fetch.mockResolvedValueOnce({ ok: true })
      
      const loadTokensSpy = jest.spyOn(wrapper.vm, 'loadTokens')
      
      await wrapper.vm.revokeToken('abc123')
      
      expect(global.confirm).toHaveBeenCalledWith(
        'Are you sure you want to revoke this token? This action cannot be undone.'
      )
      expect(fetch).toHaveBeenCalledWith('/api/token/abc123', { method: 'DELETE' })
      expect(loadTokensSpy).toHaveBeenCalled()
    })

    it('should not revoke token without user confirmation', async () => {
      global.confirm = jest.fn(() => false)
      
      await wrapper.vm.revokeToken('abc123')
      
      expect(fetch).not.toHaveBeenCalledWith('/api/token/abc123', { method: 'DELETE' })
    })

    it('should handle revoke token errors', async () => {
      global.confirm = jest.fn(() => true)
      global.alert = jest.fn()
      fetch.mockResolvedValueOnce({ ok: false })
      
      await wrapper.vm.revokeToken('abc123')
      
      expect(global.alert).toHaveBeenCalledWith('Failed to revoke token.')
    })

    it('should handle network errors during token revocation', async () => {
      global.confirm = jest.fn(() => true)
      global.alert = jest.fn()
      fetch.mockRejectedValueOnce(new Error('Network error'))
      
      await wrapper.vm.revokeToken('abc123')
      
      expect(global.alert).toHaveBeenCalledWith('Error revoking token: Network error')
    })
  })

  describe('Token testing', () => {
    beforeEach(() => {
      wrapper.vm.testPath = '/api/apps'
      wrapper.vm.testTokenInput = 'test-token'
    })

    it('should show error when path or token is missing', async () => {
      wrapper.vm.testPath = ''
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testError).toBe('Please select an API path and provide a token.')
    })

    it('should show error when token is missing but path is provided', async () => {
      wrapper.vm.testTokenInput = ''
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testError).toBe('Please select an API path and provide a token.')
    })

    it('should test token successfully with JSON response', async () => {
      const mockResponse = { apps: [] }
      fetch.mockResolvedValueOnce({
        ok: true,
        headers: {
          get: (name) => name === 'content-type' ? 'application/json' : null
        },
        text: () => Promise.resolve(JSON.stringify(mockResponse))
      })
      
      await wrapper.vm.testToken()
      
      expect(fetch).toHaveBeenCalledWith('/api/apps', {
        method: 'GET',
        headers: { 'Authorization': 'Bearer test-token' },
        credentials: 'omit'
      })
      
      expect(wrapper.vm.testResult).toBe(JSON.stringify(mockResponse, null, 2))
      expect(wrapper.vm.testError).toBe('')
    })

    it('should handle malformed JSON response gracefully', async () => {
      const malformedJson = '{"apps": [invalid json'
      fetch.mockResolvedValueOnce({
        ok: true,
        headers: {
          get: (name) => name === 'content-type' ? 'application/json' : null
        },
        text: () => Promise.resolve(malformedJson)
      })
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testResult).toBe(malformedJson)
      expect(wrapper.vm.testError).toBe('')
    })

    it('should handle JSON response with empty body', async () => {
      fetch.mockResolvedValueOnce({
        ok: true,
        headers: {
          get: (name) => name === 'content-type' ? 'application/json' : null
        },
        text: () => Promise.resolve('')
      })
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testResult).toBe('')
      expect(wrapper.vm.testError).toBe('')
    })

    it('should handle non-JSON responses', async () => {
      const mockResponse = 'Plain text response'
      fetch.mockResolvedValueOnce({
        ok: true,
        headers: {
          get: () => 'text/plain'
        },
        text: () => Promise.resolve(mockResponse)
      })
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testResult).toBe(mockResponse)
    })

    it('should handle HTTP error responses', async () => {
      fetch.mockResolvedValueOnce({
        ok: false,
        status: 401,
        statusText: 'Unauthorized',
        headers: {
          get: () => 'application/json'
        },
        text: () => Promise.resolve('{"error": "Invalid token"}')
      })
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testError).toBe('HTTP 401: Unauthorized')
    })

    it('should handle network errors during token testing', async () => {
      fetch.mockRejectedValueOnce(new Error('Network error'))
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testError).toBe('Request failed: Network error')
    })

    it('should clear previous results before testing', async () => {
      // Set previous results
      wrapper.vm.testResult = 'previous result'
      wrapper.vm.testError = 'previous error'

      fetch.mockResolvedValueOnce({
        ok: true,
        headers: {
          get: () => 'text/plain'
        },
        text: () => Promise.resolve('new result')
      })
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testResult).toBe('new result')
      expect(wrapper.vm.testError).toBe('')
    })

    it('should handle edge case where content-type is null but body is JSON-like', async () => {
      const jsonLikeResponse = '{"valid": "json"}'
      fetch.mockResolvedValueOnce({
        ok: true,
        headers: {
          get: () => null // content-type header returns null
        },
        text: () => Promise.resolve(jsonLikeResponse)
      })
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testResult).toBe(jsonLikeResponse)
      expect(wrapper.vm.testError).toBe('')
    })

    it('should handle case where content-type contains application/json but body is empty', async () => {
      fetch.mockResolvedValueOnce({
        ok: true,
        headers: {
          get: (name) => name === 'content-type' ? 'application/json; charset=utf-8' : null
        },
        text: () => Promise.resolve('')
      })
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testResult).toBe('')
      expect(wrapper.vm.testError).toBe('')
    })

    it('should handle case where content-type and body conditions create specific branch', async () => {
      // This tests the specific condition: contentType && contentType.indexOf("application/json") !== -1 && bodyText
      fetch.mockResolvedValueOnce({
        ok: true,
        headers: {
          get: (name) => name === 'content-type' ? 'text/plain; charset=utf-8' : null
        },
        text: () => Promise.resolve('non-json response')
      })
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testResult).toBe('non-json response')
      expect(wrapper.vm.testError).toBe('')
    })
  })
  describe('Utility functions', () => {
    it('should format dates correctly', () => {
      const timestamp = 1640995200 // 2022-01-01 00:00:00 UTC
      const formatted = wrapper.vm.formatDate(timestamp)
      
      // The exact format depends on locale, but it should be a string
      expect(typeof formatted).toBe('string')
      expect(formatted.length).toBeGreaterThan(0)
    })

    it('should format different timestamps correctly', () => {
      const timestamps = [
        0,           // 1970-01-01
        946684800,   // 2000-01-01
        1577836800,  // 2020-01-01
        2147483647   // 2038-01-19 (max 32-bit timestamp)
      ]

      timestamps.forEach(ts => {
        const formatted = wrapper.vm.formatDate(ts)
        expect(typeof formatted).toBe('string')
        expect(formatted.length).toBeGreaterThan(0)
        expect(formatted).not.toBe('Invalid Date')
      })
    })

    it('should handle negative timestamps', () => {
      const formatted = wrapper.vm.formatDate(-86400) // 1969-12-31
      expect(typeof formatted).toBe('string')
      expect(formatted.length).toBeGreaterThan(0)
    })

    it('should handle zero timestamp', () => {
      const formatted = wrapper.vm.formatDate(0) // 1970-01-01
      expect(typeof formatted).toBe('string')
      expect(formatted.length).toBeGreaterThan(0)
    })
  })
  describe('Component UI interactions', () => {    
    it('should add scope when "Add Scope" button is clicked', async () => {
      const buttons = wrapper.findAll('button[type="button"]')
      const addButton = buttons.find(button => button.text() === 'Add Scope')
      expect(addButton).toBeTruthy()
      
      await addButton.trigger('click')
      
      expect(wrapper.vm.scopes).toHaveLength(2)
    })

    it('should remove scope when "Remove" button is clicked', async () => {
      // Add a second scope first
      await wrapper.vm.addScope()
      expect(wrapper.vm.scopes).toHaveLength(2)
      
      const removeButtons = wrapper.findAll('button[type="button"]').filter(button => 
        button.text() === 'Remove'
      )
      expect(removeButtons.length).toBeGreaterThan(0)
      
      await removeButtons[0].trigger('click')
      expect(wrapper.vm.scopes).toHaveLength(1)
    })

    it('should submit form when "Generate Token" button is clicked', async () => {
      const generateSpy = jest.spyOn(wrapper.vm, 'generateToken')
      const form = wrapper.find('.token-form')
      
      await form.trigger('submit')
      
      expect(generateSpy).toHaveBeenCalled()
    })

    it('should test token when test form is submitted', async () => {
      const testSpy = jest.spyOn(wrapper.vm, 'testToken')
      const testForm = wrapper.find('.token-tester form')
      
      await testForm.trigger('submit')
      
      expect(testSpy).toHaveBeenCalled()
    })

    it('should display token result when available', async () => {
      wrapper.vm.tokenResult = 'test-token-result'
      await wrapper.vm.$nextTick()
      
      const tokenBox = wrapper.find('.token-box')
      expect(tokenBox.exists()).toBe(true)
      expect(tokenBox.text()).toContain('test-token-result')
    })

    it('should not display token box when no result', async () => {
      wrapper.vm.tokenResult = ''
      await wrapper.vm.$nextTick()
      
      const tokenBox = wrapper.find('.token-box')
      expect(tokenBox.exists()).toBe(false)
    })

    it('should display "No active tokens" when tokens array is empty', async () => {
      wrapper.vm.tokens = []
      await wrapper.vm.$nextTick()
      
      const noTokensRow = wrapper.find('tbody tr')
      expect(noTokensRow.text()).toContain('No active tokens.')
    })

    it('should display active tokens when available', async () => {
      wrapper.vm.tokens = [{
        hash: 'abc123def456',
        username: 'testuser',
        created_at: 1640995200,
        scopes: [{ path: '/api/apps', methods: ['GET'] }]
      }]
      await wrapper.vm.$nextTick()
      
      const tokenRows = wrapper.findAll('tbody tr')
      expect(tokenRows.length).toBe(1)
      expect(tokenRows[0].text()).toContain('abc123de...')
      expect(tokenRows[0].text()).toContain('testuser')
    })

    it('should display multiple tokens correctly', async () => {
      wrapper.vm.tokens = [
        {
          hash: 'abc123def456',
          username: 'user1',
          created_at: 1640995200,
          scopes: [{ path: '/api/apps', methods: ['GET'] }]
        },
        {
          hash: 'def456ghi789',
          username: 'user2',
          created_at: 1640995300,
          scopes: [{ path: '/api/logs', methods: ['GET'] }]
        }
      ]
      await wrapper.vm.$nextTick()
      
      const tokenRows = wrapper.findAll('tbody tr')
      expect(tokenRows.length).toBe(2)
      expect(tokenRows[0].text()).toContain('abc123de...')
      expect(tokenRows[0].text()).toContain('user1')
      expect(tokenRows[1].text()).toContain('def456gh...')
      expect(tokenRows[1].text()).toContain('user2')
    })

    it('should trigger revoke when revoke button is clicked', async () => {
      const revokeSpy = jest.spyOn(wrapper.vm, 'revokeToken')
      wrapper.vm.tokens = [{
        hash: 'abc123def456',
        username: 'testuser',
        created_at: 1640995200,
        scopes: [{ path: '/api/apps', methods: ['GET'] }]
      }]
      await wrapper.vm.$nextTick()
      
      const revokeButton = wrapper.find('.danger-btn')
      await revokeButton.trigger('click')
      
      expect(revokeSpy).toHaveBeenCalledWith('abc123def456')
    })

    it('should update scope path selection', async () => {
      const pathSelect = wrapper.find('select')
      await pathSelect.setValue('/api/apps')
      
      expect(wrapper.vm.scopes[0].path).toBe('/api/apps')
    })

    it('should update methods selection', async () => {
      // First set a path
      wrapper.vm.scopes[0].path = '/api/apps'
      await wrapper.vm.$nextTick()
      
      const methodsSelect = wrapper.findAll('select')[1]
      await methodsSelect.setValue(['GET'])
      
      expect(wrapper.vm.scopes[0].methods).toContain('GET')
    })

    it('should display test results when available', async () => {
      wrapper.vm.testResult = 'Test response data'
      await wrapper.vm.$nextTick()
      
      const resultPre = wrapper.find('pre')
      expect(resultPre.exists()).toBe(true)
      expect(resultPre.text()).toContain('Test response data')
    })

    it('should display test errors when available', async () => {
      wrapper.vm.testError = 'Test error message'
      await wrapper.vm.$nextTick()
      
      const errorDiv = wrapper.find('.alert-danger')
      expect(errorDiv.exists()).toBe(true)
      expect(errorDiv.text()).toContain('Test error message')
    })
  })
  describe('Data validation', () => {
    it('should filter out empty scopes during token generation', async () => {
      wrapper.vm.scopes = [
        { path: '', methods: [] },
        { path: '/api/apps', methods: ['GET'] },
        { path: '/api/logs', methods: [] }
      ]
      
      const spy = jest.spyOn(wrapper.vm, 'generateToken')
      global.alert = jest.fn()
      
      await wrapper.vm.generateToken()
      
      // Should only process the valid scope
      expect(fetch).toHaveBeenCalledWith('/api/token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ scopes: [{ path: '/api/apps', methods: ['GET'] }] })
      })
    })

    it('should filter out scopes with empty methods array', async () => {
      wrapper.vm.scopes = [
        { path: '/api/apps', methods: [] },
        { path: '/api/logs', methods: ['GET'] }
      ]
      
      await wrapper.vm.generateToken()
      
      expect(fetch).toHaveBeenCalledWith('/api/token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ scopes: [{ path: '/api/logs', methods: ['GET'] }] })
      })
    })

    it('should filter out scopes with empty path', async () => {
      wrapper.vm.scopes = [
        { path: '', methods: ['GET'] },
        { path: '/api/logs', methods: ['GET'] }
      ]
      
      await wrapper.vm.generateToken()
      
      expect(fetch).toHaveBeenCalledWith('/api/token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ scopes: [{ path: '/api/logs', methods: ['GET'] }] })
      })
    })

    it('should clear methods when path changes', async () => {
      const scopeSelect = wrapper.find('select')
      wrapper.vm.scopes[0].methods = ['GET', 'POST']
      
      // Simulate path change
      await scopeSelect.setValue('/api/logs')
      await scopeSelect.trigger('change')
      
      expect(wrapper.vm.scopes[0].methods).toEqual([])
    })

    it('should handle multiple scopes with different validation states', async () => {
      wrapper.vm.scopes = [
        { path: '/api/apps', methods: ['GET', 'POST'] },
        { path: '', methods: ['GET'] },
        { path: '/api/logs', methods: [] },
        { path: '/api/config', methods: ['GET'] }
      ]
      
      await wrapper.vm.generateToken()
      
      expect(fetch).toHaveBeenCalledWith('/api/token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ 
          scopes: [
            { path: '/api/apps', methods: ['GET', 'POST'] },
            { path: '/api/config', methods: ['GET'] }
          ] 
        })
      })
    })
  })
  describe('Component lifecycle', () => {
    it('should call loadTokens when component is mounted', () => {
      // Create a new wrapper to test the mounted hook
      const loadTokensSpy = jest.fn()
      const TestComponent = {
        ...ApiTokenManager,
        methods: {
          ...ApiTokenManager.methods,
          loadTokens: loadTokensSpy
        }
      }
      
      mount(TestComponent)
      expect(loadTokensSpy).toHaveBeenCalled()
    })

    it('should handle mount with failing loadTokens', () => {
      fetch.mockRejectedValueOnce(new Error('Mount error'))
      
      // This should not throw
      expect(() => mount(ApiTokenManager)).not.toThrow()
    })
  })

  describe('Edge cases and error boundaries', () => {    it('should handle testToken with all falsy conditions', async () => {
      wrapper.vm.testPath = null
      wrapper.vm.testTokenInput = null
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testError).toBe('Please select an API path and provide a token.')
    })

    it('should handle generateToken with empty filtered scopes array', async () => {
      wrapper.vm.scopes = [
        { path: '', methods: ['GET'] },
        { path: '', methods: [] },
        { path: 'test', methods: [] }
      ]
      
      global.alert = jest.fn()
      await wrapper.vm.generateToken()
      
      expect(global.alert).toHaveBeenCalledWith(
        'Please specify at least one path and corresponding method(s).'
      )
    })

    it('should handle removeScope with invalid index gracefully', async () => {
      const originalLength = wrapper.vm.scopes.length
      
      // Try to remove with invalid index
      await wrapper.vm.removeScope(-1)
      await wrapper.vm.removeScope(999)
      
      expect(wrapper.vm.scopes.length).toBe(originalLength)
    })

    it('should test all API route paths systematically', () => {
      const allPaths = wrapper.vm.apiRoutes.map(route => route.path)
      
      allPaths.forEach(path => {
        const methods = wrapper.vm.getMethodsForPath(path)
        expect(Array.isArray(methods)).toBe(true)
        expect(methods.length).toBeGreaterThan(0)
      })
    })
  })
  describe('Additional coverage for remaining edge cases', () => {
    it('should handle formatDate with various timestamp formats', () => {
      // Test various timestamp formats
      expect(wrapper.vm.formatDate(0)).toBeTruthy()
      expect(wrapper.vm.formatDate(1234567890)).toBeTruthy()
      expect(wrapper.vm.formatDate(Date.now() / 1000)).toBeTruthy()
    })

    it('should test all functions are callable', () => {
      // Ensure all methods exist and are functions
      expect(typeof wrapper.vm.addScope).toBe('function')
      expect(typeof wrapper.vm.removeScope).toBe('function')
      expect(typeof wrapper.vm.getMethodsForPath).toBe('function')
      expect(typeof wrapper.vm.generateToken).toBe('function')
      expect(typeof wrapper.vm.loadTokens).toBe('function')
      expect(typeof wrapper.vm.revokeToken).toBe('function')
      expect(typeof wrapper.vm.formatDate).toBe('function')
      expect(typeof wrapper.vm.testToken).toBe('function')
    })

    it('should handle getMethodsForPath with edge cases', () => {      // Test with various path inputs
      expect(wrapper.vm.getMethodsForPath('')).toEqual(['GET', 'POST', 'DELETE', 'PATCH', 'PUT'])
      expect(wrapper.vm.getMethodsForPath(null)).toEqual(['GET', 'POST', 'DELETE', 'PATCH', 'PUT'])
      expect(wrapper.vm.getMethodsForPath(undefined)).toEqual(['GET', 'POST', 'DELETE', 'PATCH', 'PUT'])
      expect(wrapper.vm.getMethodsForPath('/nonexistent')).toEqual(['GET', 'POST', 'DELETE', 'PATCH', 'PUT'])
    })

    it('should handle component state initialization comprehensively', () => {
      const newWrapper = mount(ApiTokenManager)
      
      // Verify all initial state
      expect(newWrapper.vm.scopes).toHaveLength(1)
      expect(newWrapper.vm.scopes[0]).toEqual({ path: '', methods: [] })
      expect(newWrapper.vm.tokenResult).toBe('')
      expect(newWrapper.vm.tokens).toEqual([])
      expect(newWrapper.vm.testPath).toBe('')
      expect(newWrapper.vm.testTokenInput).toBe('')
      expect(newWrapper.vm.testResult).toBe('')
      expect(newWrapper.vm.testError).toBe('')
      expect(newWrapper.vm.apiRoutes).toBeDefined()
      expect(Array.isArray(newWrapper.vm.apiRoutes)).toBe(true)
    })

    it('should handle testToken with empty string inputs specifically', async () => {
      wrapper.vm.testPath = ''
      wrapper.vm.testTokenInput = ''
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testError).toBe('Please select an API path and provide a token.')
    })

    it('should handle testToken with whitespace-only inputs', async () => {
      wrapper.vm.testPath = '   '
      wrapper.vm.testTokenInput = '   '
      
      // Whitespace strings are truthy, so they will pass the initial check
      // and proceed to make the fetch call, which will fail since fetch is not mocked for this path
      fetch.mockRejectedValueOnce(new Error('Network error'))
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testError).toBe('Request failed: Network error')
    })

    it('should verify mounted hook behavior', () => {
      const loadTokensSpy = jest.spyOn(ApiTokenManager.methods, 'loadTokens')
      
      mount(ApiTokenManager)
      
      expect(loadTokensSpy).toHaveBeenCalled()
      
      loadTokensSpy.mockRestore()
    })

    it('should handle rapid successive method calls', async () => {
      // Test rapid calls to methods to ensure state consistency
      wrapper.vm.addScope()
      wrapper.vm.addScope()
      wrapper.vm.addScope()
      
      expect(wrapper.vm.scopes).toHaveLength(4)
      
      wrapper.vm.removeScope(1)
      wrapper.vm.removeScope(1)
      
      expect(wrapper.vm.scopes).toHaveLength(2)
    })
    
    it('should handle all JSON parsing edge cases in testToken', async () => {
      // Test case where content-type is JSON but parse fails and recovers
      fetch.mockResolvedValueOnce({
        ok: true,
        headers: {
          get: (name) => name === 'content-type' ? 'application/json' : null
        },
        text: () => Promise.resolve('{"broken json":}')
      })
      
      wrapper.vm.testPath = '/api/apps'
      wrapper.vm.testTokenInput = 'test-token'
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testResult).toBe('{"broken json":}')
      expect(wrapper.vm.testError).toBe('')
    })

    it('should handle edge case where res.ok is true but still shows error', async () => {
      fetch.mockResolvedValueOnce({
        ok: true,
        status: 200,
        statusText: 'OK',
        headers: {
          get: () => 'text/plain'
        },
        text: () => Promise.resolve('Success response')
      })
      
      wrapper.vm.testPath = '/api/apps'
      wrapper.vm.testTokenInput = 'test-token'
      
      await wrapper.vm.testToken()
      
      expect(wrapper.vm.testResult).toBe('Success response')
      expect(wrapper.vm.testError).toBe('')
    })
  })
  describe('Final coverage improvements',  () => {
    it('should achieve 100% function coverage by testing mounted lifecycle directly', async () => {
      // Create a spy on loadTokens before mounting
      const loadTokensSpy = jest.spyOn(ApiTokenManager.methods, 'loadTokens')
      
      // Mount the component which triggers mounted()
      const testWrapper = mount(ApiTokenManager);

      await flushPromises();
      
      // Verify mounted was called (indirectly through loadTokens being called)
      expect(loadTokensSpy).toHaveBeenCalled()
      
      // Test the component was mounted correctly
      expect(testWrapper.vm).toBeDefined()
      expect(testWrapper.vm.scopes).toHaveLength(1)
      
      loadTokensSpy.mockRestore()
    })

    it('should test data function initialization', () => {
      // Test the data function directly
      const data = ApiTokenManager.data()
      
      expect(data.scopes).toHaveLength(1)
      expect(data.scopes[0]).toEqual({ path: '', methods: [] })
      expect(data.tokenResult).toBe('')
      expect(data.tokens).toEqual([])
      expect(data.testPath).toBe('')
      expect(data.testTokenInput).toBe('')
      expect(data.testResult).toBe('')
      expect(data.testError).toBe('')
      expect(data.apiRoutes).toBeDefined()
    })
  })
})
