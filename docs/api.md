# API

Sunshine has a RESTful API which can be used to interact with the service.

Unless otherwise specified, authentication is required for all API calls. You can authenticate using either basic authentication with the admin username and password, or with an API token that provides fine-grained access control.

@htmlonly
<script src="api.js"></script>
@endhtmlonly

## API Token Security Model and Best Practices

Sunshine API tokens are designed for security and fine-grained access control:

- **Token Creation:** When you generate an API token, Sunshine creates a secure random 32-character string. Only a cryptographic hash of the token is stored on disk and in memory. The raw token is shown to you only once—immediately after creation. If you lose it, you must generate a new token.
- **Security:** Because only the hash is stored, even if the state file is compromised, attackers cannot recover the original token value.
- **Principle of Least Privilege:** When creating a token, always grant access only to the specific API paths and HTTP methods required for your use case. Avoid giving broad or unnecessary permissions.
- **Token Management:**
  - Store your token securely after creation. Never share or log it.
  - Revoke tokens immediately if they are no longer needed or if you suspect they are compromised.
  - Use HTTPS to protect tokens in transit.
- **Listing and Revocation:** You can list all active tokens (metadata only, not the token value) and revoke any token at any time using the API.

API Tokens can also be managed in the Web UI under the "API Token" tab in the navigation bar.

See below for details on token endpoints and usage examples.

## GET /api/apps
@copydoc confighttp::getApps()

## POST /api/apps
@copydoc confighttp::saveApp()

## POST /api/apps/close
@copydoc confighttp::closeApp()

## DELETE /api/apps/{index}
@copydoc confighttp::deleteApp()

## GET /api/clients/list
@copydoc confighttp::getClients()

## POST /api/clients/unpair
@copydoc confighttp::unpair()

## POST /api/clients/unpair-all
@copydoc confighttp::unpairAll()

## GET /api/config
@copydoc confighttp::getConfig()

## GET /api/configLocale
@copydoc confighttp::getLocale()

## POST /api/config
@copydoc confighttp::saveConfig()

## POST /api/covers/upload
@copydoc confighttp::uploadCover()

## GET /api/logs
@copydoc confighttp::getLogs()

## POST /api/password
@copydoc confighttp::savePassword()

## POST /api/pin
@copydoc confighttp::savePin()

## POST /api/reset-display-device-persistence
@copydoc confighttp::resetDisplayDevicePersistence()

## POST /api/restart
@copydoc confighttp::restart()

## Authentication

All API calls require authentication. You can use either:
- **Basic Authentication**: Use your admin username and password.
- **API Token (recommended for automation)**: Use a generated token with fine-grained access control.

### Generating an API Token

**POST /api/token**

Authenticate with Basic Auth. The request body should specify the allowed API paths and HTTP methods for the token:

```json
{
  "scopes": [
    { "path": "/api/apps", "methods": ["GET", "POST"] },
    { "path": "/api/logs", "methods": ["GET"] }
  ]
}
```

**Response:**
```json
{ "token": "...your-new-token..." }
```
> The token is only shown once. Store it securely.

### Using an API Token

Send the token in the `Authorization` header:
```
Authorization: Bearer <token>
```

The token grants access only to the specified paths and HTTP methods.

### Managing API Tokens

- **List tokens:** `GET /api/tokens` (shows metadata, not token values)
- **Revoke token:** `DELETE /api/token/{hash}`

<div class="section_buttons">

| Previous                                    |                                  Next |
|:--------------------------------------------|--------------------------------------:|
| [Performance Tuning](performance_tuning.md) | [Troubleshooting](troubleshooting.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
