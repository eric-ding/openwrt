#!/usr/bin/env bash
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || exit 2
# Mock only rc.common/procd configuration calls; never run the client.
mockdir=$(mktemp -d)
trap 'rm -rf "$mockdir"' EXIT
printf '%s\n' 'json_load() { :; }' 'json_get_var() { printf -v "$1" "%s" "${BOOT_FLAG:-0}"; }' > "$mockdir/jshn.sh"
printf '%s\n' '#!/bin/sh' 'echo "{}"' > "$mockdir/rpc"
chmod +x "$mockdir/rpc"
sed -e "s|/usr/share/libubox/jshn.sh|$mockdir/jshn.sh|g" -e "s|/usr/libexec/rpcd/njitclient|$mockdir/rpc|g" config/njit/package-managed/files/njitclient.init > "$mockdir/init"
. "$mockdir/init"
config_load() { :; }
config_get_bool() { printf -v "$1" '%s' 0; }
config_get() { printf -v "$1" '%s' wan; }
start() { echo 'unexpected boot start' >&2; exit 1; }
procd_open_instance() { echo 'unexpected instance' >&2; exit 1; }
boot
start_service
! rg 'killall|udhcpc|/etc/init.d/(network|firewall)' config/njit/package-managed/files/njitclient.init
boot_calls=0
start() { boot_calls=$((boot_calls+1)); test "${NJIT_BOOT:-0}" = 1; }
BOOT_FLAG=1
boot
[[ $boot_calls == 1 ]]
BOOT_FLAG=0
boot
[[ $boot_calls == 1 ]]
! rg 'procd_set_param respawn' config/njit/package-managed/files/njitclient.init
printf '%s\n' 'default-disabled and opt-in boot service checks passed' 
