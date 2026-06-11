#!/usr/bin/env bash
#
# @file scripts/macos_build_install_dmg.sh
# @brief Build Sunshine.dmg and install it to /Applications.
# Make sure to stop the previous version first.
# Set SUNSHINE_INSTALL_DIR to install into a different existing directory.
#
# Recommended command-line args:
# --skip-notarize --skip-tests --num-processors=10

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."

readonly DMG_PATH="$PWD/build/cpack_artifacts/Sunshine.dmg"
readonly APP_NAME="Sunshine.app"
readonly INSTALL_DIR="${SUNSHINE_INSTALL_DIR:-/Applications}"
readonly INSTALL_PATH="$INSTALL_DIR/$APP_NAME"

# Help must exit this wrapper too, without installing a previously built DMG.
for arg in "$@"; do
  case "$arg" in
    -h*|--help) exec "$script_dir/macos_build.sh" --help ;;
  esac
done
for arg in "$@"; do
  case "$arg" in
    --step=all|--step=dmg) ;;
    --step=*)
      echo "Installation requires --step=all or --step=dmg." >&2
      exit 1
      ;;
  esac
done

mounted=false
mount_dir=""
staging_dir=""

# @brief Restore an interrupted replacement and release temporary resources.
# @return Preserve the original failure status, or report a cleanup failure.
cleanup() {
  local status=$?
  trap - EXIT

  if [[ -n "$staging_dir" ]]; then
    if [[ -e "$staging_dir/previous.app" || -L "$staging_dir/previous.app" ]]; then
      if [[ ! -e "$INSTALL_PATH" && ! -L "$INSTALL_PATH" ]]; then
        if ! mv "$staging_dir/previous.app" "$INSTALL_PATH"; then
          echo "Unable to restore the previous app. Backup retained at $staging_dir/previous.app" >&2
          staging_dir=""
          if (( status == 0 )); then status=1; fi
        fi
      fi
    fi
    if [[ -n "$staging_dir" ]]; then
      rm -rf "$staging_dir" || { if (( status == 0 )); then status=1; fi; }
    fi
  fi

  if [[ "$mounted" == true ]]; then
    if diskutil eject "$mount_dir" >/dev/null; then
      mounted=false
    else
      echo "Warning: failed to eject $mount_dir" >&2
      if (( status == 0 )); then status=1; fi
    fi
  fi
  if [[ -n "$mount_dir" && "$mounted" == false ]]; then
    # diskutil may already have removed the mount directory when ejecting.
    if [[ -d "$mount_dir" ]]; then
      rmdir "$mount_dir" || { if (( status == 0 )); then status=1; fi; }
    fi
  fi
  exit "$status"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

"$script_dir/macos_build.sh" "$@"

if [[ ! -f "$DMG_PATH" ]]; then
  echo "Build did not produce $DMG_PATH" >&2
  exit 1
fi

mount_dir="$(mktemp -d "${TMPDIR:-/tmp}/sunshine-mount.XXXXXX")"

# `yes` normally exits with SIGPIPE after hdiutil stops reading. Temporarily
# disable pipefail so the pipeline status reflects hdiutil rather than yes.
# The conditional also prevents errexit from bypassing the failure handler.
set +o pipefail
if yes | PAGER=cat hdiutil attach -quiet -nobrowse -mountpoint "$mount_dir" "$DMG_PATH"; then
  attach_status=0
else
  attach_status=$?
fi
set -o pipefail

if (( attach_status != 0 )); then
  echo "Failed to mount $DMG_PATH" >&2
  exit "$attach_status"
fi

mounted=true

# Stage on the destination filesystem so replacement and rollback use renames.
staging_dir="$(mktemp -d "$INSTALL_DIR/.sunshine-install.XXXXXX")"
ditto "$mount_dir/$APP_NAME" "$staging_dir/$APP_NAME"
if [[ ! -f "$staging_dir/$APP_NAME/Contents/Info.plist" ||
      ! -x "$staging_dir/$APP_NAME/Contents/MacOS/Sunshine" ]]; then
  echo "The DMG does not contain a complete $APP_NAME bundle." >&2
  exit 1
fi

diskutil eject "$mount_dir" >/dev/null
mounted=false

if [[ -e "$INSTALL_PATH" || -L "$INSTALL_PATH" ]]; then
  mv "$INSTALL_PATH" "$staging_dir/previous.app"
fi
mv "$staging_dir/$APP_NAME" "$INSTALL_PATH"

echo "Installed: $INSTALL_PATH"
