# Sunshine Security Architecture Survey & Forensic Vulnerability Audit

**Target Codebase**: `LizardByte/Sunshine`  
**Fork**: `Fandry96/Sunshine`  
**Audit Scope**: Web Management Interface (Port 47990), GameStream Pairing Handshake (Port 47989), Authentication, and Input Sanitization  
**Standard**: Responsible Disclosure & Upstream Mergeable Security Assessment  
**Date**: 2026-09-14  

---

## Table of Contents
1. [Executive Summary](#1-executive-summary)
2. [Threat Model & Network Topology](#2-threat-model--network-topology)
3. [Stateless Authentication Architecture & Cookie Analysis](#3-stateless-authentication-architecture--cookie-analysis)
4. [Vulnerability Catalog (CVSS v3.1 & CWE Classifications)](#4-vulnerability-catalog-cvss-v31--cwe-classifications)
   - [Finding 1: DOM/Stored XSS in Release Notes Display (CWE-79, CVSS 8.8 High)](#finding-1-domstored-xss-in-release-notes-display-cwe-79-cvss-88-high)
   - [Finding 2: Missing CSRF Validation on Cover Upload Endpoint (CWE-352, CVSS 7.5 High)](#finding-2-missing-csrf-validation-on-cover-upload-endpoint-cwe-352-cvss-75-high)
   - [Finding 3: GameStream Pairing Session Table Exhaustion DoS (CWE-400, CVSS 7.5 High)](#finding-3-gamestream-pairing-session-table-exhaustion-dos-cwe-400-cvss-75-high)
   - [Finding 4: Insecure CSRF Token Origin/Referer Bypass & Non-Constant-Time Token Comparison (CWE-352, CWE-208, CVSS 5.3 Medium)](#finding-4-insecure-csrf-token-originreferer-bypass--non-constant-time-token-comparison-cwe-352-cwe-208-cvss-53-medium)
   - [Finding 5: Configuration File CRLF Injection in `saveConfig` (CWE-93, CVSS 7.2 High)](#finding-5-configuration-file-crlf-injection-in-saveconfig-cwe-93-cvss-72-high)
   - [Finding 6: Missing Standard HTTP Security Headers (CWE-693, CVSS 4.3 Medium)](#finding-6-missing-standard-http-security-headers-cwe-693-cvss-43-medium)
5. [Remediation Status & Patch Implementation](#5-remediation-status--patch-implementation)
6. [Verification Methodology](#6-verification-methodology)

---

## 1. Executive Summary

A comprehensive source-code forensic security audit was conducted against Sunshine's local web administration service (`confighttp`) and GameStream pairing protocol implementation (`nvhttp`).

Sunshine is an open-source self-hosted game stream host for Moonlight. Because Sunshine executes with privileges to launch applications, configure input devices, and stream audio/video from the host operating system, security vulnerabilities in its web administrative surface carry critical implications.

### Key Discoveries
1. **Stateless HTTP Basic Auth Over TLS**: Sunshine deliberately avoids cookie-based session management. Protected endpoints rely entirely on browser-cached HTTP Basic Authentication headers (`Authorization: Basic ...`) transmitted over TLS on port 47990. As a result, cookie flags (`HttpOnly`, `SameSite`, `Secure`) are absent by design rather than omitted due to oversight.
2. **DOM-Based XSS in Release Notes (`index.html`)**: In the release notes view, markdown descriptions retrieved from GitHub API were rendered directly into the DOM using `v-html="convertMarkdownToHtml(...)"` with `marked` configured with `sanitize: false`. A malicious or hijacked release note could execute arbitrary JavaScript in the operator's authenticated browser context, allowing an attacker to configure and launch arbitrary executables via `/api/apps` (Remote Code Execution).
3. **Missing CSRF Protection on Cover Uploads (`confighttp.cpp`)**: While 14 other mutating REST endpoints enforce CSRF validation, `POST /api/covers/upload` (`uploadCover`) completely lacked `validate_csrf_token()`, allowing cross-origin requests to write arbitrary files to the host filesystem.
4. **GameStream Pairing Slot Exhaustion DoS (`nvhttp.cpp`)**: The pairing session table enforces a strict limit of `MAX_PENDING_PAIRING_SESSIONS = 32` with a 5-minute timeout. Because `GET /pair?phrase=getservercert` is unauthenticated and lacks IP rate-limiting, an attacker can consume all 32 slots in milliseconds, blocking all legitimate client pairing (HTTP 503).
5. **CSRF Header Suppression Bypass & Timing Side-Channel**: Requests omitting both `Origin` and `Referer` headers bypassed CSRF checks entirely. Additionally, CSRF token validation and password hash comparisons used standard string inequality (`!=`), leaking timing information.
6. **Configuration Directive CRLF Injection**: `POST /api/config` serialized user-supplied JSON keys directly to `sunshine.conf` without sanitizing newlines (`\n`), allowing injection of unauthorized directives.
7. **Missing Defensive Security Headers**: Default responses lacked `X-Content-Type-Options: nosniff` and `Referrer-Policy: strict-origin-when-cross-origin`.

---

## 2. Threat Model & Network Topology

Sunshine operates two distinct HTTP/HTTPS listening services:

```
+-------------------------------------------------------------------------+
|                               HOST OS                                   |
|                                                                         |
|  +---------------------------+       +-------------------------------+  |
|  |    GameStream Service     |       |       Web Management UI       |  |
|  |          (nvhttp)         |       |         (confighttp)          |  |
|  |  TCP 47989 / TCP 47984    |       |           TCP 47990           |  |
|  +-------------+-------------+       +---------------+---------------+  |
|                ^                                     ^                  |
+----------------|-------------------------------------|------------------+
                 |                                     |
                 | (GameStream Handshake)              | (HTTPS Management)
                 |                                     |
       +---------+--------+                  +---------+--------+
       | Moonlight Client |                  |  Admin Browser   |
       +------------------+                  +------------------+
```

1. **GameStream Service (`nvhttp`)**:
   - Base Port: TCP 47989 (HTTP) and TCP 47984 (HTTPS).
   - Handles client discovery, PIN pairing negotiation, server certificate exchange, and RTSP session initiation.
   - Authentication: Unauthenticated initial pairing handshake; authenticated via client X.509 certificates and AES-128 cryptographic tokens post-pairing.
2. **Web Management UI (`confighttp`)**:
   - Port: TCP 47990 (HTTPS, calculated as base port 47989 + `PORT_HTTPS = 1`).
   - Powered by embedded `SimpleWeb::Server<SimpleWeb::HTTPS>` with TLS encryption.
   - Serves Vue 3.5 single-page and multi-page assets, REST API endpoints, and configuration mutators.
   - Authentication: Stateless HTTP Basic Authentication over TLS.

---

## 3. Stateless Authentication Architecture & Cookie Analysis

### 3.1 Architectural Design: Absence of Session Cookies
During our forensic audit, a ripgrep scan of the entire Sunshine codebase (`src/`, `src_assets/`) for `Set-Cookie`, `cookie`, or session identifier tokens confirmed **zero** cookie-based session mechanisms.

- **Authentication Primitive**: When a client requests any protected resource without credentials, `confighttp::authenticate()` invokes `confighttp::send_unauthorized()`, returning:
  ```http
  HTTP/1.1 401 Unauthorized
  WWW-Authenticate: Basic realm="Sunshine Gamestream Host", charset="UTF-8"
  Content-Type: application/json
  ```
- **Browser State**: The web browser natively prompts the user for username and password, caches the credentials in memory for the duration of the browser session, and automatically transmits the `Authorization: Basic <base64>` header on every subsequent request to `https://<host>:47990/`.
- **Stateless Verification**: Every incoming HTTP request is parsed independently in `confighttp::authenticate()` (`src/confighttp.cpp`):
  1. Base64-decodes the `Authorization` header value.
  2. Extracts username and password separated by `:`.
  3. Computes `SHA256(password + salt)`.
  4. Compares the username and password hash against `config::sunshine.username` and `config::sunshine.password`.

### 3.2 Formal Finding on Cookie Attributes (`HttpOnly`, `SameSite`, `Secure`)
Because Sunshine does not issue HTTP cookies (`Set-Cookie`) or maintain server-side cookie sessions:
- `HttpOnly`: **Not Applicable (Absent by Design)**.
- `SameSite`: **Not Applicable (Absent by Design)**.
- `Secure`: **Not Applicable (Absent by Design)**.

Instead, transport security is enforced globally at the TLS layer on port 47990, and Cross-Site Request Forgery (CSRF) is addressed via an application-level token mechanism (`X-CSRF-Token` and allowed origin validation).

### 3.3 Authentication Limitations
1. **No Session Revocation**: Because HTTP Basic Authentication relies on the browser's native credential cache, Sunshine cannot force a client logout by sending a response header. The `/logout` page merely displays instructional text advising the user to close their browser.
2. **Single-Round SHA-256 Storage**: Passwords are hashed with `SHA256(password + salt)` (`httpcommon.cpp:101`). Single-round SHA-256 lacks memory-hardness and modern work factors (unlike Argon2id or PBKDF2), making the stored password hash susceptible to offline GPU cracking if `sunshine.conf` is leaked.
3. **No Rate-Limiting or Account Lockout**: Failed authentication attempts return 401 immediately with no IP-level rate limiting or progressive exponential backoff.

---

## 4. Vulnerability Catalog (CVSS v3.1 & CWE Classifications)

| Finding ID | Title | CWE | CVSS v3.1 Vector | Score | Severity |
|:---|:---|:---|:---|:---:|:---:|
| **SEC-01** | DOM/Stored XSS in Release Notes Display | CWE-79 | `CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:C/C:H/I:H/A:N` | **8.8** | **High** |
| **SEC-02** | Missing CSRF Validation on Cover Upload Endpoint | CWE-352 | `CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:U/C:N/I:H/A:H` | **7.5** | **High** |
| **SEC-03** | GameStream Pairing Session Table Exhaustion DoS | CWE-400 | `CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:H` | **7.5** | **High** |
| **SEC-04** | Insecure CSRF Origin/Referer Bypass & Timing Attack | CWE-352, CWE-208 | `CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:U/C:L/I:L/A:N` | **5.3** | **Medium** |
| **SEC-05** | CRLF Configuration Directive Injection in `saveConfig` | CWE-93 | `CVSS:3.1/AV:N/AC:L/PR:H/UI:N/S:U/C:H/I:H/A:H` | **7.2** | **High** |
| **SEC-06** | Missing Standard Defensive HTTP Security Headers | CWE-693 | `CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:U/C:L/I:N/A:N` | **4.3** | **Medium** |

---

### Finding 1: DOM/Stored XSS in Release Notes Display
- **CWE**: CWE-79 (Improper Neutralization of Input During Web Page Generation - Cross-Site Scripting)
- **CVSS v3.1**: `8.8` (High) — `CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:C/C:H/I:H/A:N`
- **Affected File**: `src_assets/common/assets/web/index.html` (Lines 94, 114, 147-153)
- **Description**:
  The home dashboard (`index.html`) fetches release notes directly from GitHub's public API (`https://api.github.com/repos/LizardByte/Sunshine/releases/latest`) and renders them into the DOM using Vue's raw HTML directive:
  ```html
  <div class="markdown-body release-notes" v-html="convertMarkdownToHtml(githubVersion.release.body)"></div>
  ```
  The markdown parser was explicitly configured to allow arbitrary HTML tags:
  ```javascript
  marked.setOptions({
    breaks: true,
    gfm: true,
    headerIds: true,
    mangle: false,
    sanitize: false // Allowed unescaped script and HTML injection!
  });
  ```
- **Exploitation & Impact**:
  If an upstream GitHub release description contains malicious HTML or JavaScript (via compromised GitHub credentials, malicious release notes, or an in-transit proxy compromise), the payload executes immediately in the browser of anyone viewing Sunshine's home page.
  Because Sunshine's Web UI exposes endpoints to register applications with arbitrary executable commands (`POST /api/apps`), an attacker executing script in the authenticated UI context can invoke `/api/apps` and launch arbitrary system binaries, escalating from XSS to full **Remote Code Execution (RCE)** on the host.
- **Remediation**:
  Removed `sanitize: false` from `marked.setOptions`. Implemented a robust client-side HTML sanitizer `sanitizeHtml()` utilizing the browser's native `DOMParser` to whitelist safe markup tags (`p`, `h1`-`h6`, `code`, `pre`, `a`, `ul`, `li`, etc.), strip all event handlers (`on*`), disallow dangerous protocols (`javascript:`, `vbscript:`, `data:`), and sanitize the output before passing to `v-html`.

---

### Finding 2: Missing CSRF Validation on Cover Upload Endpoint
- **CWE**: CWE-352 (Cross-Site Request Forgery)
- **CVSS v3.1**: `7.5` (High) — `CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:U/C:N/I:H/A:H`
- **Affected File**: `src/confighttp.cpp` (Lines 1589-1636, `uploadCover`)
- **Description**:
  All mutating configuration endpoints (`saveApp`, `deleteApp`, `saveConfig`, `savePin`, `updateClient`, etc.) invoke `validate_csrf_token(response, request, client_id)`. However, `confighttp::uploadCover()` completely omitted this check:
  ```cpp
  void uploadCover(const resp_https_t &response, const req_https_t &request) {
    if (!check_content_type(response, request, "application/json")) {
      return;
    }
    if (!authenticate(response, request)) {
      return;
    }
    // MISSING: validate_csrf_token()!
    std::stringstream ss;
    ss << request->content.rdbuf();
    ...
  ```
- **Exploitation & Impact**:
  An attacker hosting a malicious webpage visited by an authenticated Sunshine administrator can trigger forged cross-origin `POST` requests to `https://localhost:47990/api/covers/upload`. The attacker can supply arbitrary base64 image data or instruct Sunshine to download remote files, overwriting files in Sunshine's covers directory (`<appdata>/covers/<key>.png`) and causing arbitrary disk consumption or cover vandalism.
- **Remediation**:
  Injected `validate_csrf_token(response, request, client_id)` at the entry of `uploadCover()` immediately following authentication verification.

---

### Finding 3: GameStream Pairing Session Table Exhaustion DoS
- **CWE**: CWE-400 (Uncontrolled Resource Consumption)
- **CVSS v3.1**: `7.5` (High) — `CVSS:3.1/AV:N/AC:L/PR:N/UI:N/S:U/C:N/I:N/A:H`
- **Affected File**: `src/nvhttp.h` (Line 57), `src/nvhttp.cpp` (Line 603)
- **Description**:
  The GameStream service on port 47989 accepts unauthenticated pairing requests:
  `GET /pair?uniqueid=<uuid>&phrase=getservercert&salt=<salt>&clientcert=<cert>`
  Each request allocates a pairing session in `map_id_sess`. Sunshine enforces:
  ```cpp
  constexpr std::size_t MAX_PENDING_PAIRING_SESSIONS = 32;
  constexpr auto PAIRING_SESSION_TIMEOUT = std::chrono::minutes {5};
  ```
  When `map_id_sess.size() >= MAX_PENDING_PAIRING_SESSIONS`, `nvhttp::insert_pair_session()` returns `pair_session_insert_e::FULL`, and the server sends HTTP 503 (`Too many pending pairing sessions`).
- **Exploitation & Impact**:
  Because no IP-level quota or rate limiting exists, an attacker on the local network or internet (if port 47989 is forwarded) can send 32 rapid requests with distinct `uniqueid` values. All 32 session slots are filled, locking out legitimate Moonlight clients from pairing for 5 full minutes. Repeating the burst every 5 minutes maintains a persistent Denial of Service.
- **Remediation**:
  Enforce an IP-level quota (e.g., maximum 2 pending pairing sessions per client IP) and reduce `PAIRING_SESSION_TIMEOUT` from 5 minutes to 60-120 seconds.

---

### Finding 4: Insecure CSRF Token Origin/Referer Bypass & Non-Constant-Time Token Comparison
- **CWE**: CWE-352 (Cross-Site Request Forgery), CWE-208 (Observable Timing Discrepancy)
- **CVSS v3.1**: `5.3` (Medium) — `CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:U/C:L/I:L/A:N`
- **Affected File**: `src/confighttp.cpp` (Lines 783, 824-829)
- **Description**:
  1. **Header Suppression Bypass**: `confighttp::validate_csrf_token()` previously contained:
     ```cpp
     if (origin_it == request->header.end() && referer_it == request->header.end()) {
       return true; // Bypass CSRF validation!
     }
     ```
     This logic intended to facilitate CLI/cURL scripts. However, a malicious web page can suppress the `Referer` header using `<meta name="referrer" content="no-referrer">` or `rel="noreferrer"`. When both headers are omitted or stripped, the server permitted state-changing cross-origin requests with zero CSRF token verification.
  2. **Timing Side-Channel**: In `validate_stored_csrf_token()`:
     ```cpp
     if (token_it->second.token != provided_token)
     ```
     Standard `std::string::operator!=` returns `false` on the first non-matching character, introducing a timing side-channel (CWE-208) that can leak token characters under high-resolution timing measurement. A similar comparison existed in `authenticate()` for password hashes.
- **Remediation**:
  1. Removed the missing header bypass. Requests must either possess a valid `Origin` or `Referer` matching `csrf_allowed_origins`, or provide a valid CSRF token in the `X-CSRF-Token` header.
  2. Implemented `constant_time_equals()` backed by OpenSSL `CRYPTO_memcmp()` to perform constant-time verification of CSRF tokens and password hashes.

---

### Finding 5: Configuration File CRLF Injection in `saveConfig`
- **CWE**: CWE-93 (Improper Neutralization of CRLF Sequences - 'CRLF Injection')
- **CVSS v3.1**: `7.2` (High) — `CVSS:3.1/AV:N/AC:L/PR:H/UI:N/S:U/C:H/I:H/A:H`
- **Affected File**: `src/confighttp.cpp` (Lines 1488-1502, `saveConfig`)
- **Description**:
  In `saveConfig`, submitted JSON keys and values are written directly to `sunshine.conf` using line-by-line streaming:
  ```cpp
  for (const auto &[k, v] : input_tree.items()) {
    config_stream << k << " = " << (v.is_string() ? v.get<std::string>() : v.dump()) << std::endl;
  }
  file_handler::write_file(config::sunshine.config_file.c_str(), config_stream.str());
  ```
- **Exploitation & Impact**:
  If a parameter value contains carriage returns or newlines (`\r\n` or `\n`), an authenticated user or an attacker exploiting chained vulnerabilities can inject unauthorized configuration keys into `sunshine.conf`. For instance, submitting:
  `{"log_path": "logs\norigin_web_ui_allowed = 0\n"}`
  injects directives that alter server network binding or disable security guardrails upon daemon restart.
- **Remediation**:
  Validate submitted configuration keys against an allowed-list schema and strip control characters (`\r`, `\n`) from all string values prior to file serialization.

---

### Finding 6: Missing Standard HTTP Security Headers
- **CWE**: CWE-693 (Protection Mechanism Failure)
- **CVSS v3.1**: `4.3` (Medium) — `CVSS:3.1/AV:N/AC:L/PR:N/UI:R/S:U/C:L/I:N/A:N`
- **Affected File**: `src/confighttp.cpp`
- **Description**:
  Sunshine only emitted `X-Frame-Options: DENY` and `Content-Security-Policy: frame-ancestors 'none';`. It lacked:
  - `X-Content-Type-Options: nosniff` (allowing browsers to MIME-sniff response payloads).
  - `Referrer-Policy: strict-origin-when-cross-origin` (potentially leaking path or token information in external HTTP requests).
- **Remediation**:
  Implemented centralized `add_security_headers()` function in `src/confighttp.cpp` that injects:
  - `X-Frame-Options: DENY`
  - `Content-Security-Policy: frame-ancestors 'none';`
  - `X-Content-Type-Options: nosniff`
  - `Referrer-Policy: strict-origin-when-cross-origin`
  across all response handlers (`send_response`, `send_unauthorized`, `send_redirect`, `not_found`, `bad_request`, `getPage`, `getFaviconImage`, `getSunshineLogoImage`, `getAsset`, `getCover`, and `getLogs`).

---

## 5. Remediation Status & Patch Implementation

The following table summarizes the implementation status of each identified issue:

| Finding | Vulnerability | Component | Implementation Status | Patch Location |
|:---|:---|:---|:---:|:---|
| **SEC-01** | DOM XSS via Markdown Release Notes | Web UI | **FIXED** | `src_assets/common/assets/web/index.html` |
| **SEC-02** | Missing CSRF on `uploadCover` | C++ Server | **FIXED** | `src/confighttp.cpp:1614` |
| **SEC-03** | Pairing Session Exhaustion DoS | GameStream | Documented | Upstream Roadmap (`src/nvhttp.cpp`) |
| **SEC-04** | CSRF Header Suppression & Timing Leaks | C++ Server | **FIXED** | `src/confighttp.cpp:532, 802, 846` |
| **SEC-05** | Config File CRLF Injection | C++ Server | Documented | Upstream Roadmap (`src/confighttp.cpp`) |
| **SEC-06** | Missing HTTP Security Headers | C++ Server | **FIXED** | `src/confighttp.cpp:549` & `src/confighttp.h:60` |

---

## 6. Verification Methodology

Verification of the defensive remediations is automated through `Sunshine/verify_security.py`, testing both static source code invariants and server endpoint configuration.

### Dual-Endpoint Verification Requirement
In compliance with the K3 Dual-Input Acceptance Constraint, verification must test at least two distinct, non-adjacent endpoints:
1. Primary Configuration Endpoint: `/api/config`
2. Mutating File Upload Endpoint: `/api/covers/upload`

Both endpoints are validated to confirm:
- Presence of mandatory CSRF token checks.
- Presence of defensive HTTP security headers (`X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`, `Referrer-Policy: strict-origin-when-cross-origin`).
- Rejection of header-suppressed unauthenticated cross-origin requests.
- Absence of insecure session cookie issuance.
