# API

Sunshine has a RESTful API which can be used to interact with the service.

Unless otherwise specified, authentication is required for all API calls. You can authenticate using
basic authentication with the admin username and password.

## CSRF Protection

State-changing API endpoints (POST, DELETE) are protected against Cross-Site Request Forgery (CSRF) attacks.

**For Web Browsers:**
- Requests from same-origin (configured via `csrf_allowed_origins`) are automatically allowed
- Cross-origin requests require a CSRF token

**For Non-Browser Applications:**
- Non-browser clients (e.g. `curl`, scripts, custom apps) are **exempt** from CSRF protection
- CSRF attacks require a browser to silently attach credentials to a cross-origin request — this threat
  does not apply to non-browser clients that explicitly provide credentials with every request
- Requests with no `Origin` or `Referer` header (as is typical for non-browser clients) are automatically
  allowed without a CSRF token

**Example (browser-equivalent cross-origin request):**
```bash
# Get CSRF token
curl -u user:pass https://localhost:47990/api/csrf-token

# Use token in request
curl -u user:pass -H "X-CSRF-Token: your_token_here" \
  -X POST https://localhost:47990/api/restart
```

@htmlonly
<script src="api.js"></script>
@endhtmlonly

## GET /api/csrf-token
@copydoc confighttp::service_t::getCSRFToken()

## GET /api/apps
@copydoc confighttp::service_t::getApps()

## POST /api/apps
@copydoc confighttp::service_t::saveApp()

## POST /api/apps/close
@copydoc confighttp::service_t::closeApp()

## DELETE /api/apps/{index}
@copydoc confighttp::service_t::deleteApp()

## GET /api/browse
@copydoc confighttp::service_t::browseDirectory()

## GET /api/clients/list
@copydoc confighttp::service_t::getClients()

## POST /api/clients/unpair
@copydoc confighttp::service_t::unpair()

## POST /api/clients/unpair-all
@copydoc confighttp::service_t::unpairAll()

## POST /api/clients/update
@copydoc confighttp::service_t::updateClient()

## GET /api/config
@copydoc confighttp::service_t::getConfig()

## GET /api/configLocale
@copydoc confighttp::service_t::getLocale()

## POST /api/config
@copydoc confighttp::service_t::saveConfig()

## GET /api/covers/{index}
@copydoc confighttp::service_t::getCover()

## POST /api/covers/upload
@copydoc confighttp::service_t::uploadCover()

## GET /api/logs
@copydoc confighttp::service_t::getLogs()

## POST /api/password
@copydoc confighttp::service_t::savePassword()

## POST /api/pin
@copydoc confighttp::service_t::savePin()

## POST /api/reset-display-device-persistence
@copydoc confighttp::service_t::resetDisplayDevicePersistence()

## POST /api/reset-portal-token
@copydoc confighttp::service_t::resetPortalToken()

## POST /api/restart
@copydoc confighttp::service_t::restart()

## GET /api/virtual-input/status
@copydoc confighttp::service_t::getVirtualInputStatus()
