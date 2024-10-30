# API

Sunshine has a RESTful API which can be used to interact with the service.

Unless otherwise specified, authentication is required for all API calls. You can authenticate using
basic authentication with the admin username and password.

## GET /api/apps
@copydoc confighttp::getApps()

## GET /api/logs
@copydoc confighttp::getLogs()

## POST /api/apps
@copydoc confighttp::saveApp()

## DELETE /api/apps{index}
@copydoc confighttp::deleteApp()

## POST /api/covers/upload
@copydoc confighttp::uploadCover()

## GET /api/config
@copydoc confighttp::getConfig()

## GET /api/configLocale
@copydoc confighttp::getLocale()

## POST /api/config
@copydoc confighttp::saveConfig()

## POST /api/restart
@copydoc confighttp::restart()

## POST /api/password
@copydoc confighttp::savePassword()

## POST /api/pin
@copydoc confighttp::savePin()

## POST /api/clients/unpair-all
@copydoc confighttp::unpairAll()

## POST /api/clients/unpair
@copydoc confighttp::unpair()

## GET /api/clients/list
@copydoc confighttp::listClients()

## GET /api/apps/close
@copydoc confighttp::closeApp()

<div class="section_buttons">

| Previous                                    |                                  Next |
|:--------------------------------------------|--------------------------------------:|
| [Performance Tuning](performance_tuning.md) | [Troubleshooting](troubleshooting.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
