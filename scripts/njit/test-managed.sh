#!/usr/bin/env bash
# GitHub-hosted Linux only. All fixtures synthetic; network namespace has no WAN.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo 'Run this test suite in GitHub Actions'; exit 2; }
root=$PWD
out=$root/build/njit-managed-tests
mkdir -p "$out"
src=$root/config/njit/package-managed/src
tests=$root/config/njit/tests/managed
flags=(-std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -I "$src")
cc "${flags[@]}" "$tests/credentials-test.c" "$src/njit_credentials.c" -o "$out/credentials-test"
cc "${flags[@]}" "$tests/frames-test.c" "$src/njit_frames.c" -o "$out/frames-test"
cc "${flags[@]}" '-DNJIT_RUNDIR="/tmp/njit-runtime-test"' "$tests/runtime-test.c" "$src/njit_runtime.c" "$src/njit_options.c" -ljson-c -o "$out/runtime-test"
sudo unshare --net "$out/credentials-test"
"$out/frames-test"
sudo unshare --net "$out/runtime-test"
cc "${flags[@]}" '-DNJIT_RUNDIR="/tmp/njit-policy-runtime"' '-DNJIT_CONFIG_DIR="/tmp/njit-policy-config"' '-DNJIT_CARRIER="/tmp/njit-policy-config/carrier"' "$tests/policy-test.c" "$src/njit_runtime.c" "$src/njit_options.c" -ljson-c -o "$out/policy-test"
sudo timeout 15 unshare --net "$out/policy-test"
# Compile main, even though it is not linked into the fixture binaries.
cc "${flags[@]}" -c "$src/main.c" -o "$out/main.o"
