#!/usr/bin/env bash
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || exit 2
src=$PWD/config/njit/package-managed/src
out=$PWD/build/njit-managed-tests
flags=(-std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -I "$src")
# Paths are compile-time constants, not remotely supplied request arguments.
defines=('-DNJIT_CREDENTIALS="/tmp/njit-rpc-fixture/private/credentials"' '-DNJIT_CONFIG_DIR="/tmp/njit-rpc-fixture/private"' '-DNJIT_UI_RUNDIR="/tmp/njit-rpc-fixture/ui"' '-DNJIT_RUNDIR="/tmp/njit-rpc-fixture/runtime"' '-DNJIT_UCI_DIR="/tmp/njit-rpc-fixture/uci"' '-DNJIT_SERVICE="/tmp/njit-rpc-fixture/service"' '-DNJIT_BINARY="/tmp/njit-rpc-fixture/client"' '-DNJIT_UBUS="/tmp/njit-rpc-fixture/ubus"' '-DNJIT_CARRIER="/tmp/njit-rpc-fixture/carrier"')
cc "${flags[@]}" "${defines[@]}" "$src/njit_rpc.c" "$src/njit_credentials.c" "$src/njit_options.c" -ljson-c -o "$out/rpc-test"
cc -Wall -Wextra -Werror config/njit/tests/managed/rpc-fixture-client.c -o "$out/fixture-client"
sudo install -d -m 700 /tmp/njit-rpc-fixture
sudo install -m 700 "$out/fixture-client" /tmp/njit-rpc-fixture/client
# Test creates the root itself, remove only its initially empty directory after copying the binary aside.
sudo mv /tmp/njit-rpc-fixture/client /tmp/njit-fixture-client
sudo rmdir /tmp/njit-rpc-fixture
# The test needs the stub after creating its private root.
sudo unshare --net python3 config/njit/tests/managed/rpc-test.py "$out/rpc-test"
