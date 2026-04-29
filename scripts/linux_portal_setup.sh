#!/bin/bash
# Regenerates the XDG portal restore token for Sunshine running as a systemd service.
#
# On headless Wayland systems, the portal Start call requires user confirmation
# via a monitor selection dialog. This script:
#   1. Starts the Sunshine service with portal capture (hangs waiting for approval)
#   2. Launches a temporary KMS instance so you can connect via Moonlight
#   3. You approve the portal popup from the KMS session
#   4. Cleans up and verifies the service is running with a persisted token
#
# Usage: linux_portal_setup.sh [--service NAME] [--port PORT]

set -euo pipefail

# --- Defaults ---
SERVICE=""
KMS_PORT=48990

# --- Parse args ---
while [[ $# -gt 0 ]]; do
    case $1 in
        --service) SERVICE="$2"; shift 2 ;;
        --port) KMS_PORT="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--service NAME] [--port PORT]"
            echo "  --service  Systemd user service name (auto-detected if omitted)"
            echo "  --port     Port for temporary KMS instance (default: 48990)"
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# --- Detect sunshine binary ---
SUNSHINE_BIN=$(command -v sunshine 2>/dev/null) || {
    echo "ERROR: sunshine not found in PATH"
    exit 1
}
SUNSHINE_REAL=$(readlink -f "$SUNSHINE_BIN")

# --- Detect config directory (mirrors sunshine's appdata() logic) ---
if [[ -n "${CONFIGURATION_DIRECTORY:-}" ]]; then
    CONF_DIR="$CONFIGURATION_DIRECTORY/sunshine"
elif [[ -n "${XDG_CONFIG_HOME:-}" ]]; then
    CONF_DIR="$XDG_CONFIG_HOME/sunshine"
elif [[ -d "$HOME/.var/app/dev.lizardbyte.app.Sunshine/config/sunshine" ]]; then
    CONF_DIR="$HOME/.var/app/dev.lizardbyte.app.Sunshine/config/sunshine"
else
    CONF_DIR="$HOME/.config/sunshine"
fi
# Follow symlink if config dir is one
CONF_DIR=$(readlink -f "$CONF_DIR")
CONF="$CONF_DIR/sunshine.conf"
TOKEN="$CONF_DIR/portal_token"

if [[ ! -f "$CONF" ]]; then
    echo "ERROR: Config not found: $CONF"
    exit 1
fi

# --- Detect service name ---
if [[ -z "$SERVICE" ]]; then
    CANDIDATES=$(systemctl --user list-unit-files --type=service --no-legend '*unshine*' 2>/dev/null | awk '{print $1}')
    COUNT=$(echo "$CANDIDATES" | grep -c . || true)
    if [[ "$COUNT" -eq 0 ]]; then
        echo "ERROR: No sunshine systemd user service found. Specify with --service NAME"
        exit 1
    elif [[ "$COUNT" -eq 1 ]]; then
        SERVICE="$CANDIDATES"
    else
        echo "Multiple sunshine services found:"
        echo "$CANDIDATES" | nl
        read -rp "Select number: " SEL
        SERVICE=$(echo "$CANDIDATES" | sed -n "${SEL}p")
    fi
fi
echo "Using service: $SERVICE"

# --- Detect Wayland env ---
WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}"
DISPLAY="${DISPLAY:-}"
DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=/run/user/$(id -u)/bus}"

# Try to get env from running session processes if not set
if [[ -z "$WAYLAND_DISPLAY" ]]; then
    for proc in plasmashell gnome-shell gnome-session sway hyprland weston xdg-desktop-portal; do
        PID=$(pgrep -u "$(id -u)" "$proc" 2>/dev/null | head -1) || continue
        [[ -z "$PID" ]] && continue
        WAYLAND_DISPLAY=$(sudo cat "/proc/$PID/environ" 2>/dev/null | tr '\0' '\n' | grep '^WAYLAND_DISPLAY=' | cut -d= -f2) || true
        if [[ -n "$WAYLAND_DISPLAY" ]]; then
            [[ -z "$DISPLAY" ]] && DISPLAY=$(sudo cat "/proc/$PID/environ" 2>/dev/null | tr '\0' '\n' | grep '^DISPLAY=' | cut -d= -f2) || true
            break
        fi
    done
fi
if [[ -z "$WAYLAND_DISPLAY" ]]; then
    echo "ERROR: Cannot determine WAYLAND_DISPLAY. Is a Wayland compositor running?"
    exit 1
fi
[[ -z "$DISPLAY" ]] && DISPLAY=":0"

export WAYLAND_DISPLAY DISPLAY DBUS_SESSION_BUS_ADDRESS

# --- Detect IP ---
IP=$(ip -4 addr show scope global | grep -oP 'inet \K[\d.]+' | head -1)
[[ -z "$IP" ]] && IP="<your-ip>"

# --- Cleanup trap ---
KMS_PID=""
TMP_CONF=""
cleanup() {
    [[ -n "$KMS_PID" ]] && kill "$KMS_PID" 2>/dev/null
    [[ -n "$TMP_CONF" ]] && rm -f "$TMP_CONF"
    sudo setcap -r "$SUNSHINE_REAL" 2>/dev/null || true
}
trap cleanup EXIT

# --- Stop everything ---
echo "Stopping sunshine..."
systemctl --user stop "$SERVICE" 2>/dev/null || true
systemctl --user reset-failed "$SERVICE" 2>/dev/null || true
pkill -x sunshine 2>/dev/null || true
sleep 2

# --- Remove old token ---
rm -f "$TOKEN"

# --- Ensure config is portal ---
if grep -q '^capture = ' "$CONF"; then
    sed -i 's/^capture = .*/capture = portal/' "$CONF"
else
    echo "capture = portal" >> "$CONF"
fi

# --- Start portal service (will hang on Start waiting for popup) ---
echo "Starting $SERVICE with portal capture (waiting for approval)..."
systemctl --user start "$SERVICE" &

# --- Prepare temp KMS config ---
TMP_CONF=$(mktemp /tmp/sunshine-kms-XXXX.conf)
cp "$CONF" "$TMP_CONF"
sed -i 's/^capture = .*/capture = kms/' "$TMP_CONF"
if grep -q '^port = ' "$TMP_CONF"; then
    sed -i "s/^port = .*/port = $KMS_PORT/" "$TMP_CONF"
else
    echo "port = $KMS_PORT" >> "$TMP_CONF"
fi

# --- Set cap_sys_admin for KMS ---
echo "Setting cap_sys_admin on $SUNSHINE_REAL..."
sudo setcap cap_sys_admin+p "$SUNSHINE_REAL"

# --- Launch temp KMS ---
nohup sunshine "$TMP_CONF" >/dev/null 2>&1 &
KMS_PID=$!
sleep 8

if ! kill -0 "$KMS_PID" 2>/dev/null; then
    echo "ERROR: KMS instance failed to start"
    exit 1
fi

echo ""
echo ">>> Temporary KMS instance running"
echo ">>> Add to Moonlight: $IP:$KMS_PORT"
echo ">>> Connect, approve the portal monitor popup, then press Enter"
read -r

sleep 3
if [[ ! -f "$TOKEN" ]]; then
    echo "ERROR: Portal token not saved. Did you approve the popup?"
    exit 1
fi

# --- Cleanup temp KMS ---
echo "Cleaning up..."
kill "$KMS_PID" 2>/dev/null; KMS_PID=""
sudo setcap -r "$SUNSHINE_REAL" 2>/dev/null || true

sleep 5
if systemctl --user is-active --quiet "$SERVICE"; then
    echo ""
    echo "=== SUCCESS ==="
    echo "Portal service running with saved token."
    echo ">>> Add to Moonlight: $IP (default port)"
else
    echo ""
    echo "=== WARNING ==="
    echo "Service not yet active. Check: journalctl --user -u $SERVICE"
fi
