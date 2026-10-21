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
cp -R "$root/config/njit/luci-app-njitclient" package/luci-app-njitclient
./scripts/feeds update base
./scripts/feeds install -p base libpcap openssl libjson-c
./scripts/feeds update luci
./scripts/feeds install -p luci luci-base
cat > .config <<'CONFIG'
CONFIG_TARGET_bcm53xx=y
CONFIG_TARGET_bcm53xx_generic=y
# CONFIG_ALL is not set
# CONFIG_ALL_NONSHARED is not set
# CONFIG_ALL_KMODS is not set
CONFIG_PACKAGE_njit8021xclient=m
CONFIG_PACKAGE_luci-app-njitclient=m
# CONFIG_OPENSSL_ENGINE is not set
CONFIG
make defconfig
make package/njit8021xclient/prepare V=s
source_dir=$(find build_dir -type d -name "njit8021xclient-dd28c17f*" -print -quit)
test -n "$source_dir"
native="$root/build/njit-managed-tests"
flags=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -I "$source_dir/src" -I "$root/config/njit/package-managed/src")
cc "${flags[@]}" "$root/config/njit/tests/managed/responses-test.c" \
  "$source_dir/src/fillmd5-libcrypto.c" "$source_dir/src/aes.c" "$source_dir/src/h3c_AES_MD5.c" \
  "$root/config/njit/package-managed/src/njit_frames.c" "$root/config/njit/package-managed/src/njit_runtime.c" "$root/config/njit/package-managed/src/njit_options.c" \
  -Wl,--wrap=pcap_sendpacket -lpcap -lcrypto -ljson-c -o "$native/responses-test"
sudo unshare --net "$native/responses-test"
cc "${flags[@]}" "$root/config/njit/tests/md5-vectors.c" "$source_dir/src/fillmd5-libcrypto.c" -lcrypto -o "$native/md5-test"
sudo unshare --net "$native/md5-test"
make package/njit8021xclient/compile -j2 V=s 2>&1 | tee "$root/build/njit-managed-build.log"
make package/luci-app-njitclient/compile -j2 V=s 2>&1 | tee -a "$root/build/njit-managed-build.log"
find bin -type f -name '*njit*.apk' -exec cp '{}' "$root/build/njit-managed-artifacts/" ';'
target=$PWD/staging_dir/target-arm_cortex-a9_musl_eabi
tc=$PWD/staging_dir/toolchain-arm_cortex-a9_gcc-14.3.0_musl_eabi
artifact=$root/build/njit-managed-artifacts
mkdir -p "$artifact/payload"
package=$(find "$artifact" -maxdepth 1 -name 'njit8021xclient-*.apk' -print -quit)
"$PWD/staging_dir/host/bin/apk" extract --allow-untrusted --no-chown --destination "$artifact/payload" "$package"
file "$artifact/payload/usr/sbin/njit-client" | tee "$artifact/elf.txt"
"$tc/bin/arm-openwrt-linux-muslgnueabi-readelf" -h -A -d "$artifact/payload/usr/sbin/njit-client" >> "$artifact/elf.txt"
set +e
timeout 20 qemu-arm -cpu cortex-a9 -L "$tc" -E LD_LIBRARY_PATH="$target/usr/lib" "$artifact/payload/usr/sbin/njit-client" > "$artifact/nonroot-check.txt" 2>&1
status=$?
set -e
test "$status" = 77
grep -q 'root required' "$artifact/nonroot-check.txt"
# Verify the ARM RPC binary too; root guard must execute after resolving libraries.
"$tc/bin/arm-openwrt-linux-muslgnueabi-readelf" -h -d "$artifact/payload/usr/libexec/rpcd/njitclient" > "$artifact/rpc-elf.txt"
set +e
timeout 20 qemu-arm -cpu cortex-a9 -L "$tc" -E LD_LIBRARY_PATH="$target/usr/lib" "$artifact/payload/usr/libexec/rpcd/njitclient" list > "$artifact/rpc-nonroot-check.txt" 2>&1
status=$?
set -e
test "$status" = 77
ui_package=$(find "$artifact" -maxdepth 1 -name 'luci-app-njitclient-*.apk' -print -quit)
test -n "$ui_package"
mkdir -p "$artifact/ui-payload"
"$PWD/staging_dir/host/bin/apk" extract --allow-untrusted --no-chown --destination "$artifact/ui-payload" "$ui_package"
cmp "$root/config/njit/luci-app-njitclient/htdocs/luci-static/resources/view/network/njitclient.js" "$artifact/ui-payload/www/luci-static/resources/view/network/njitclient.js"
cmp "$root/config/njit/luci-app-njitclient/root/usr/share/rpcd/acl.d/luci-app-njitclient.json" "$artifact/ui-payload/usr/share/rpcd/acl.d/luci-app-njitclient.json"
cmp "$root/config/njit/luci-app-njitclient/root/usr/share/luci/menu.d/luci-app-njitclient.json" "$artifact/ui-payload/usr/share/luci/menu.d/luci-app-njitclient.json"
printf 'source_commit=%s\nrun_id=%s\nbase=OpenWrt-25.12.6-bcm53xx/generic\n' "$GITHUB_SHA" "$GITHUB_RUN_ID" > "$artifact/build-inputs.txt"
cd "$root/build/njit-managed-artifacts"
compgen -G '*.apk' >/dev/null
sha256sum *.apk > SHA256SUMS
cp "$root/config/njit/package-managed/patches/100-managed-runtime.patch" .
printf '%s\n' 'EXPERIMENTAL: cloud checks do not prove real network authentication. Do not deploy automatically.' > STATUS.txt
