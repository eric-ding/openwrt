#!/usr/bin/env bash
# Dedicated GitHub Actions SDK workspace, never a local build.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo 'Build in GitHub Actions'; exit 2; }
root=$PWD
work=$RUNNER_TEMP/njit-sdk
mkdir -p "$work" build/njit-managed-artifacts
sdk=openwrt-sdk-25.12.6-bcm53xx-generic_gcc-14.3.0_musl_eabi.Linux-x86_64
archive=$sdk.tar.zst
curl --fail --location --retry 3 "https://downloads.openwrt.org/releases/25.12.6/targets/bcm53xx/generic/$archive" -o "$work/$archive"
printf '291c0899a045a963aa5f610e885b9eb335cfeb5c8478c9c431770ba9cdbc98bf  %s\n' "$work/$archive" | sha256sum -c -
tar --zstd -xf "$work/$archive" -C "$work"
cd "$work/$sdk"
cp -R "$root/config/njit/package-managed" package/njit8021xclient
./scripts/feeds update base
./scripts/feeds install -p base libpcap openssl
cat > .config <<'CONFIG'
CONFIG_TARGET_bcm53xx=y
CONFIG_TARGET_bcm53xx_generic=y
# CONFIG_ALL is not set
# CONFIG_ALL_NONSHARED is not set
# CONFIG_ALL_KMODS is not set
CONFIG_PACKAGE_njit8021xclient=m
# CONFIG_OPENSSL_ENGINE is not set
CONFIG
make defconfig
make package/njit8021xclient/compile -j2 V=s 2>&1 | tee "$root/build/njit-managed-build.log"
find bin -type f -name '*njit*.apk' -exec cp '{}' "$root/build/njit-managed-artifacts/" ';'
cd "$root/build/njit-managed-artifacts"
compgen -G '*.apk' >/dev/null
sha256sum *.apk > SHA256SUMS
cp "$root/config/njit/package-managed/patches/100-managed-runtime.patch" .
printf '%s\n' 'EXPERIMENTAL: cloud checks do not prove real network authentication. Do not deploy automatically.' > STATUS.txt
