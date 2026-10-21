#!/usr/bin/env bash
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || { echo 'Build only in GitHub Actions'; exit 2; }
root=$PWD
work=$RUNNER_TEMP/k3-openclash-deps
out=$root/build/openclash-deps-artifacts
mkdir -p "$work" "$out"
sdk=openwrt-sdk-25.12.6-bcm53xx-generic_gcc-14.3.0_musl_eabi.Linux-x86_64
archive=$sdk.tar.zst
curl --fail --location --retry 3 "https://downloads.openwrt.org/releases/25.12.6/targets/bcm53xx/generic/$archive" -o "$work/$archive"
printf '291c0899a045a963aa5f610e885b9eb335cfeb5c8478c9c431770ba9cdbc98bf  %s\n' "$work/$archive" | sha256sum -c -
tar --zstd -xf "$work/$archive" -C "$work"
cd "$work/$sdk"
./scripts/feeds update base
./scripts/feeds install -p base dnsmasq
recipe=$(readlink -f package/feeds/base/dnsmasq/Makefile)
# Separate release so the optional-feature variant is explicit in package metadata.
python3 - "$recipe" <<'PY'
from pathlib import Path
import re,sys
p=Path(sys.argv[1]);s=p.read_text()
s,n=re.subn(r'^PKG_RELEASE:=.*$', 'PKG_RELEASE:=2', s, count=1, flags=re.M)
assert n==1
p.write_text(s)
PY
cat > .config <<'CONFIG'
CONFIG_TARGET_bcm53xx=y
CONFIG_TARGET_bcm53xx_generic=y
# CONFIG_ALL is not set
# CONFIG_ALL_NONSHARED is not set
# CONFIG_ALL_KMODS is not set
# CONFIG_PACKAGE_dnsmasq is not set
CONFIG_PACKAGE_dnsmasq-full=m
CONFIG_PACKAGE_dnsmasq_full_dhcp=y
CONFIG_PACKAGE_dnsmasq_full_dhcpv6=y
CONFIG_PACKAGE_dnsmasq_full_dnssec=y
CONFIG_PACKAGE_dnsmasq_full_auth=y
CONFIG_PACKAGE_dnsmasq_full_nftset=y
CONFIG_PACKAGE_dnsmasq_full_noid=y
CONFIG_PACKAGE_dnsmasq_full_tftp=y
# CONFIG_PACKAGE_dnsmasq_full_ipset is not set
# CONFIG_PACKAGE_dnsmasq_full_conntrack is not set
CONFIG
make defconfig
cp .config "$out/sdk.config"
! grep -q '^CONFIG_PACKAGE_dnsmasq_full_conntrack=y' .config
grep -q '^CONFIG_PACKAGE_dnsmasq_full_nftset=y' .config
make package/dnsmasq/compile -j2 V=s 2>&1 | tee "$root/build/openclash-deps-build.log"
find bin -type f -name 'dnsmasq-full-*.apk' -exec cp '{}' "$out/" ';'
package=$(find "$out" -maxdepth 1 -name 'dnsmasq-full-*.apk' -print -quit)
test -n "$package"
mkdir "$out/payload"
"$PWD/staging_dir/host/bin/apk" extract --allow-untrusted --no-chown --destination "$out/payload" "$package"
tc=$PWD/staging_dir/toolchain-arm_cortex-a9_gcc-14.3.0_musl_eabi
target=$PWD/staging_dir/target-arm_cortex-a9_musl_eabi
"$tc/bin/arm-openwrt-linux-muslgnueabi-readelf" -h -d "$out/payload/usr/sbin/dnsmasq" > "$out/elf.txt"
timeout 20 qemu-arm -cpu cortex-a9 -L "$tc" -E LD_LIBRARY_PATH="$target/usr/lib" "$out/payload/usr/sbin/dnsmasq" --version > "$out/features.txt"
grep -q 'no-conntrack' "$out/features.txt"
! grep -q 'no-nftset' "$out/features.txt"
grep -q 'nftset' "$out/features.txt"
cp "$recipe" "$out/dnsmasq.Makefile"
cp feeds.conf.default "$out/feeds.conf.default"
printf 'source_commit=%s\nrun_id=%s\nbase=OpenWrt-25.12.6\nfeatures=nftset,dhcp,dhcpv6,dnssec,no-conntrack,no-ipset\n' "$GITHUB_SHA" "$GITHUB_RUN_ID" > "$out/build-inputs.txt"
cd "$out"
sha256sum *.apk > SHA256SUMS
cat features.txt
