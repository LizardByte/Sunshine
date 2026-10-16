# Client Connection Management Implementation Plan

## Scope and minimal-design decisions

- Manage a **paired client identity**, not individual transport sessions. A client is keyed by its existing UUID/certificate; kicking it terminates all active sessions carrying that certificate.
- Persist `mouse`, `keyboard`, and `controller` allowances beside the existing `enabled` flag in Sunshine's paired-client state. Missing fields default to `true`, preserving existing state files and behavior.
- Reuse the existing Troubleshooting client list. Add two small operations, `/api/clients/permissions` and `/api/clients/kick`. Leave `/api/clients/update` unchanged. A request that omits `enabled` would re-enable the client there, so permissions get their own endpoint.
- Keep all authorization in the existing authenticated Web UI. State-changing requests retain the current CSRF checks.
- Explicitly exclude friend accounts, internet presence/discovery, NAT traversal, relays, join approval, and GameStream/Moonlight protocol changes.

## Implementation

1. **Extend the existing paired-client record**
   - In [`src/nvhttp.cpp`](src/nvhttp.cpp), add three default-true permission fields to `named_cert_t`, serialize them under `root.named_devices`, load absent fields as `true`, and return them from `get_all_clients()`.
   - Add a documented permission value type and narrowly scoped get/update functions in [`src/nvhttp.h`](src/nvhttp.h). Keep certificate enable/disable independent from peripheral permissions.
   - Update all three values atomically under the existing `client_auth_mutex()` and save once per request.

2. **Enforce permissions in the existing input path**
   - In [`src/input.h`](src/input.h) and [`src/input.cpp`](src/input.cpp), store three atomic permission flags on the retained `input_t`. This is already retained by paired certificate, so it naturally applies across reconnects and all sessions for that client.
   - Initialize permissions when [`src/stream.cpp`](src/stream.cpp) allocates a session's input state, and expose a small setter so live changes take effect without reconnecting.
   - Gate mouse/touch/pen, keyboard/text, and controller handlers with `global_setting && client_permission`; do not alter packet formats or platform backends.
   - When an allowed permission becomes denied, call the existing input reset path to prevent stuck keys, buttons, or controller state. Document the minimal implementation's tradeoff: keyboard/mouse reset is process-wide today, so another connected user may need to repress a held key/button at that instant.

3. **Expose active state and kick through the RTSP owner**
   - In [`src/rtsp.h`](src/rtsp.h) and [`src/rtsp.cpp`](src/rtsp.cpp), add:
     - a read-only `is_client_active(cert)` query over the existing synchronized `_session_slots`.
   - Live permission changes go through `input::set_permissions(cert, ...)`. It uses the retained-input map, which is already keyed by certificate, so RTSP needs no propagation helper.
   - Reuse `terminate_sessions_by_cert()` for kick. Do not stop the app while another client remains connected, do not disable the certificate, and do not unpair it.

4. **Keep the HTTP surface small**
   - In [`src/confighttp.cpp`](src/confighttp.cpp):
     - enrich `/api/clients/list` with `active` and the three persisted allowances;
     - add authenticated, CSRF-protected `POST /api/clients/permissions` requiring `uuid`, one permission name, and `allowed`, then apply that single permission to live input;
     - add authenticated, CSRF-protected `POST /api/clients/kick` accepting a paired-client UUID and resolving it through `get_cert_by_uuid()`.
   - Return a clear failure for missing/unknown UUIDs and malformed/non-boolean fields. Preserve existing disable behavior separately from kick.
   - Add the kick endpoint to [`docs/api.md`](docs/api.md) and update the handlers' required Doxygen/API examples.

5. **Modify only the existing client-management UI**
   - In [`src_assets/common/assets/web/Troubleshooting.vue`](src_assets/common/assets/web/Troubleshooting.vue), retain the current paired-client card and add an active/inactive indicator, three labeled permission switches, and a Kick button enabled only while active.
   - Refresh the list after kick/update; on request failure, restore the displayed switch state and show a concise error.
   - Add only English strings in [`src_assets/common/assets/web/public/assets/locale/en.json`](src_assets/common/assets/web/public/assets/locale/en.json), per repository localization policy. Do not edit language variants or other translations.

## Regression coverage and verification

- Extend [`tests/unit/test_nvhttp_client_auth.cpp`](tests/unit/test_nvhttp_client_auth.cpp) for default-allow migration, persistence/reload, independent enabled/permission updates, unknown UUIDs, and concurrent state access.
- Extend [`tests/unit/test_input.cpp`](tests/unit/test_input.cpp) to cover allowed and denied mouse, keyboard/text, touch/pen, controller, and immediate reset behavior while preserving global input switches.
- Add focused RTSP coverage in [`tests/unit/test_rtsp.cpp`](tests/unit/test_rtsp.cpp) for certificate-active lookup and propagation, using minimal test-only registration hooks if direct coverage is otherwise impossible.
- Extend [`tests/unit/test_confighttp.cpp`](tests/unit/test_confighttp.cpp) for authentication, CSRF, validation, list fields, update, and kick behavior.
- Extend [`tests/web/Troubleshooting.test.js`](tests/web/Troubleshooting.test.js) for rendering active state, permission updates, kick, refresh, and errors.
- Run `uv sync --locked`, the focused native `test_sunshine` filters, web tests, then the full native/web suites. Run `uv run --locked lb-update-clang-format`, inspect the diff for unrelated formatting, and run the relevant documentation/lint checks.

## Expected change size and review boundaries

- Expected production surface: roughly 8 C++/header files, 1 Vue file, 1 English locale file, and API documentation; tests add approximately 4 files touched.
- Expected scale: several hundred lines including tests and Doxygen, not a protocol or architectural rewrite.
- Keep the implementation in reviewable commits if later authorized: persistence/API, input enforcement/runtime, then UI/tests. Do not mix unrelated cleanup or rename existing concepts.

## Work checklist

- [ ] Add backward-compatible persisted per-client peripheral permissions and focused state tests.
- [ ] Enforce and update permissions through retained client input state, including safe reset behavior.
- [ ] Add active-state lookup, permission propagation, and an authenticated kick endpoint using existing certificate termination.
- [ ] Extend the existing Troubleshooting client card and English localization only.
- [ ] Add native/web/API regression coverage and run formatting, tests, docs, and lint checks.
