#!/usr/bin/env bash
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || exit 2
# Mock only rc.common/procd configuration calls; never run the client.
. config/njit/package-managed/files/njitclient.init
config_load() { :; }
config_get_bool() { printf -v "$1" '%s' 0; }
config_get() { printf -v "$1" '%s' wan; }
start() { echo 'unexpected boot start' >&2; exit 1; }
procd_open_instance() { echo 'unexpected instance' >&2; exit 1; }
boot
start_service
! rg 'killall|udhcpc|/etc/init.d/(network|firewall)' config/njit/package-managed/files/njitclient.init
printf '%s\n' 'default-disabled service checks passed'
