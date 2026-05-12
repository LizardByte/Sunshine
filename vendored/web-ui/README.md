# Vendored web UI assets

This directory contains prebuilt static web UI assets from the official
LizardByte/Sunshine portable release.

## Why this exists

SwitchDesk's engine is a fork of Sunshine. We do not intend to modify or
maintain the web UI — it will be replaced in Phase 2 with token-based
session start from the SwitchDesk control plane.

The web UI source build (Vite + npm) is fragile on developer Windows
machines outside of Sunshine's exact CI environment. Rather than fight
the web UI build, we bypass it: we vendor the prebuilt assets here and
configure CMake to use them in place of building from source.

This trade-off is acceptable because:

1. We will delete this entire subsystem in Phase 2.
2. The web UI is not part of SwitchDesk's customer-facing surface.
3. The C++ engine code we actually want to modify builds cleanly without
   the web UI build pipeline.

## Source

These assets come from `Sunshine/assets/web/` inside the official
Sunshine-Windows-AMD64-portable.zip release.

## License

These assets are GPL-3.0 (same as upstream Sunshine). The vendored copy
is distributed under the same terms.

## Updating

Don't. If upstream Sunshine updates these assets, we don't track it.
In Phase 2 the entire web UI is replaced anyway.
