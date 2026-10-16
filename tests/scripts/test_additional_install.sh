#!/bin/sh

## @file tests/scripts/test_additional_install.sh
## @brief Exercise the Flatpak installer with sandboxed host commands and packaged input files.

set -eu

scenario=${1:-success}
project_root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
test_root=$(mktemp -d /tmp/sunshine-flatpak-install.XXXXXX)
case "$test_root" in
  /tmp/sunshine-flatpak-install.*) ;;
  *) exit 1 ;;
esac
trap 'rm -rf -- "$test_root"' EXIT HUP INT TERM

mkdir -p "$test_root/bin" "$test_root/fixtures" "$test_root/host/modules-load.d" "$test_root/host/udev/rules.d"
# Match the LF files packaged on Linux when testing a Windows checkout in MSYS2.
tr -d '\r' < "$project_root/src_assets/linux/misc/60-sunshine.conf" > "$test_root/fixtures/60-sunshine.conf"
tr -d '\r' < "$project_root/src_assets/linux/misc/60-sunshine.rules" > "$test_root/fixtures/60-sunshine.rules"
printf '%s\n' '[Service]' > "$test_root/fixtures/service"
: > "$test_root/calls"

if [ "$scenario" = literal ]; then
  for filename in 60-sunshine.conf 60-sunshine.rules; do
    cat >> "$test_root/fixtures/$filename" <<'EOF'
# Literal shell syntax: ' "%s\n" $(touch substitution-ran) `touch backtick-ran`
# Backslashes: \c \n; percent signs: %s %b; trailing apostrophe: '
EOF
  done
fi

cat > "$test_root/bin/cat" <<'EOF'
#!/bin/sh
case "$1" in
  /app/share/sunshine/modules-load.d/60-sunshine.conf)
    exec /bin/cat "$TEST_ROOT/fixtures/60-sunshine.conf"
    ;;
  /app/share/sunshine/udev/rules.d/60-sunshine.rules)
    [ "$TEST_FAILURE" != missing ] || exit 7
    exec /bin/cat "$TEST_ROOT/fixtures/60-sunshine.rules"
    ;;
  *) exit 1 ;;
esac
EOF

cat > "$test_root/bin/cp" <<'EOF'
#!/bin/sh
[ "$1" = /app/share/sunshine/systemd/user/app-dev.lizardbyte.app.Sunshine.service ] || exit 1
exec /bin/cp "$TEST_ROOT/fixtures/service" "$2"
EOF

cat > "$test_root/bin/flatpak-spawn" <<'EOF'
#!/bin/sh
set -eu
[ "$1" = --host ] && [ "$2" = pkexec ] || exit 1
shift 2
if [ "$1" = sh ]; then
  [ "$2" = -c ] || exit 1
  case "$3" in
    */etc/modules-load.d/60-sunshine.conf*) operation=modules ;;
    */etc/udev/rules.d/60-sunshine.rules*) operation=rules ;;
    *) exit 1 ;;
  esac
  printf '%s\n' "write-$operation" >> "$TEST_ROOT/calls"
  [ "$TEST_FAILURE" != "$operation" ] || exit 7
  # Redirect only the fixed host destination into the test directory.
  host_command=$(printf '%s' "$3" | sed "s|/etc/modules-load.d/60-sunshine.conf|$TEST_ROOT/host/modules-load.d/60-sunshine.conf|g; s|/etc/udev/rules.d/60-sunshine.rules|$TEST_ROOT/host/udev/rules.d/60-sunshine.rules|g")
  shift 3
  exec /bin/sh -c "$host_command" "$@"
fi
printf '%s\n' "$*" >> "$TEST_ROOT/calls"
case "$*" in
  'modprobe uhid') ;;
  'udevadm control --reload-rules') [ "$TEST_FAILURE" != reload ] || exit 7 ;;
  'udevadm trigger --property-match=DEVNAME=/dev/uinput') ;;
  'udevadm trigger --property-match=DEVNAME=/dev/uhid') ;;
  'udevadm trigger --subsystem-match=hidraw') ;;
  'udevadm trigger --subsystem-match=input') ;;
  *) exit 1 ;;
esac
EOF
chmod +x "$test_root/bin/"*

TEST_ROOT=$test_root
TEST_FAILURE=$scenario
test_home=$test_root/home
export TEST_ROOT TEST_FAILURE

status=0
(cd "$test_root" && env HOME="$test_home" PATH="$test_root/bin:$PATH" sh "$project_root/packaging/linux/flatpak/scripts/additional-install.sh") > "$test_root/output" 2>&1 || status=$?

## @brief Report the installer output when a regression assertion fails.
fail() {
  printf '%s\n' "$1" >&2
  /bin/cat "$test_root/output" >&2
  exit 1
}

case "$scenario" in
  success|literal)
    [ "$status" -eq 0 ] || fail "Installer failed: $status"
    cmp "$test_root/fixtures/60-sunshine.conf" "$test_root/host/modules-load.d/60-sunshine.conf" || fail 'Module configuration was changed.'
    cmp "$test_root/fixtures/60-sunshine.rules" "$test_root/host/udev/rules.d/60-sunshine.rules" || fail 'Udev rules were changed.'
    grep -q '^Virtual input permissions have been updated\.$' "$test_root/output" || fail 'Completion message is missing.'
    expected_calls=8
    ;;
  modules) expected_calls=1 ;;
  missing) expected_calls=2 ;;
  rules) expected_calls=3 ;;
  reload) expected_calls=4 ;;
  *) fail "Unknown scenario: $scenario" ;;
esac

if [ "$scenario" != success ] && [ "$scenario" != literal ]; then
  [ "$status" -ne 0 ] || fail 'Installer reported success after a failure.'
  if grep -q 'Virtual input permissions have been updated' "$test_root/output"; then
    fail 'Installer printed a completion message after a failure.'
  fi
fi

[ "$(wc -l < "$test_root/calls")" -eq "$expected_calls" ] || fail 'Installer ran an unexpected number of host commands.'
if [ -e "$test_root/substitution-ran" ] || [ -e "$test_root/backtick-ran" ]; then
  fail 'File contents were executed as shell commands.'
fi
cmp "$test_root/fixtures/service" "$test_home/.config/systemd/user/app-dev.lizardbyte.app.Sunshine.service" || fail 'User service was not installed.'
printf 'Flatpak installer scenario passed: %s\n' "$scenario"
