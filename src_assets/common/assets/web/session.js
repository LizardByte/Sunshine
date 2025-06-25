/**
 * Session management utilities for Sunshine frontend
 */

class SessionManager {
  constructor() {
    this.sessionToken = null;
    this.refreshTimeout = null;
  }

  /**
   * Get the current session token from storage
   */
  getToken() {
    if (this.sessionToken) {
      return this.sessionToken;
    }
    
    // Check localStorage first (remember me), then sessionStorage
    this.sessionToken = localStorage.getItem('session_token') || 
                       sessionStorage.getItem('session_token');
    return this.sessionToken;
  }

  /**
   * Store the session token
   */
  setToken(token, remember = false) {
    this.sessionToken = token;
    
    // Clear from both storages first
    localStorage.removeItem('session_token');
    sessionStorage.removeItem('session_token');
    
    // Store in appropriate location
    const storage = remember ? localStorage : sessionStorage;
    storage.setItem('session_token', token);
    
    // Schedule token refresh
    this.scheduleRefresh();
  }

  /**
   * Clear the session token
   */
  clearToken() {
    this.sessionToken = null;
    localStorage.removeItem('session_token');
    sessionStorage.removeItem('session_token');
    
    if (this.refreshTimeout) {
      clearTimeout(this.refreshTimeout);
      this.refreshTimeout = null;
    }
  }

  /**
   * Check if user is logged in
   */
  isLoggedIn() {
    return !!this.getToken();
  }

  /**
   * Make an authenticated fetch request
   */
  async fetch(url, options = {}) {
    const token = this.getToken();
    
    // For login endpoint, don't add auth header
    if (url.includes('/api/auth/login')) {
      return fetch(url, options);
    }
    
    // Add session token if available
    if (token) {
      options.headers = {
        ...options.headers,
        'Authorization': `Session ${token}`
      };
    }
    
    const response = await fetch(url, options);
    
    // If unauthorized and we have a token, clear it and redirect to login
    if (response.status === 401 && token) {
      this.clearToken();
      this.redirectToLogin();
    }
    
    return response;
  }

  /**
   * Refresh the session token
   */
  async refreshToken() {
    if (!this.getToken()) {
      return false;
    }

    try {
      const response = await this.fetch('./api/auth/refresh', {
        method: 'POST'
      });

      if (response.ok) {
        const data = await response.json();
        if (data.status && data.token) {
          // Determine if this was stored with remember me
          const remember = !!localStorage.getItem('session_token');
          this.setToken(data.token, remember);
          return true;
        }
      }
    } catch (error) {
      console.error('Token refresh failed:', error);
    }

    // Refresh failed, clear token
    this.clearToken();
    return false;
  }

  /**
   * Schedule automatic token refresh
   */
  scheduleRefresh() {
    if (this.refreshTimeout) {
      clearTimeout(this.refreshTimeout);
    }
    
    // Refresh token every 20 hours (4 hours before expiry)
    this.refreshTimeout = setTimeout(() => {
      this.refreshToken();
    }, 20 * 60 * 60 * 1000);
  }

  /**
   * Logout user
   */
  async logout() {
    if (this.getToken()) {
      try {
        await this.fetch('./api/auth/logout', {
          method: 'POST'
        });
      } catch (error) {
        console.error('Logout request failed:', error);
      }
    }
    
    this.clearToken();
    this.redirectToLogin();
  }

  /**
   * Redirect to login page
   */
  redirectToLogin() {
    const currentPath = window.location.pathname + window.location.search;
    if (currentPath !== '/login' && !currentPath.startsWith('/login?')) {
      window.location.href = `/login?redirect=${encodeURIComponent(currentPath)}`;
    }
  }

  /**
   * Initialize session management
   */
  init() {
    const token = this.getToken();
    if (token) {
      this.scheduleRefresh();
    }

    // Set up global fetch wrapper
    if (typeof window !== 'undefined') {
      window.sessionFetch = this.fetch.bind(this);
    }
  }
}

// Create global instance
const sessionManager = new SessionManager();

// Initialize on load
if (typeof window !== 'undefined') {
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => sessionManager.init());
  } else {
    sessionManager.init();
  }
}

export default sessionManager;
