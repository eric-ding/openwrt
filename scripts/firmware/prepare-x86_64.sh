#!/bin/bash
set -euo pipefail

project=$(cd "$(dirname "$0")/../.." && pwd)
work=${1:?Usage: prepare-x86_64.sh EMPTY_BUILD_DIRECTORY}
lock="$project/config/x86_64/source-lock.json"
config="$project/config/x86_64/firmware.config"
[ ! -e "$work" ] || { echo 'Build directory exists; use a new empty directory.' >&2; exit 1; }
mkdir -p "$work"
work=$(cd "$work" && pwd)

python3 - "$lock" "$work" <<'PY'
import hashlib, json, pathlib, sys, urllib.request
lock = json.loads(pathlib.Path(sys.argv[1]).read_text())
work = pathlib.Path(sys.argv[2])
req = urllib.request.Request(lock['openwrt_archive_url'], headers={'User-Agent': 'openwrt-x86-build'})
with urllib.request.urlopen(req, timeout=180) as response:
    archive = response.read()
digest = hashlib.sha256(archive).hexdigest()
if digest != lock['openwrt_archive_sha256']:
    raise SystemExit('OpenWrt source archive SHA256 mismatch')
(work / 'openwrt-source.tar.gz').write_bytes(archive)
(work / 'source-lock.json').write_text(json.dumps(lock, indent=2) + '\n')
PY

tar -xzf "$work/openwrt-source.tar.gz" --strip-components=1 -C "$work"
actual=$(git -C "$work" rev-parse HEAD 2>/dev/null || true)
# GitHub source archives do not include .git; validate the pinned commit through the lock and archive hash.
[ -z "$actual" ] || [ "$actual" = "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["openwrt_commit"])' "$lock")" ]

openclash_dir="$work/.openclash-src"
git init -q "$openclash_dir"
git -C "$openclash_dir" remote add origin https://github.com/vernesong/OpenClash.git
openclash_commit=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["openclash_commit"])' "$lock")
git -C "$openclash_dir" fetch --quiet --depth=1 origin "$openclash_commit"
git -C "$openclash_dir" checkout --quiet --detach FETCH_HEAD
test "$(git -C "$openclash_dir" rev-parse HEAD)" = "$openclash_commit"
test "$(sed -n 's/^PKG_VERSION:=//p' "$openclash_dir/luci-app-openclash/Makefile")" = "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["openclash_tag"].removeprefix("v"))' "$lock")"
mkdir -p "$work/package"
cp -a "$openclash_dir/luci-app-openclash" "$work/package/"

cd "$work"
./scripts/feeds update packages luci
for feed in packages luci; do
  case "$feed" in
    packages) key=packages_feed_commit ;;
    luci) key=luci_feed_commit ;;
  esac
  revision=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$lock" "$key")
  git -C "feeds/$feed" fetch --quiet --depth=1 origin "$revision"
  git -C "feeds/$feed" checkout --quiet --detach FETCH_HEAD
  test "$(git -C "feeds/$feed" rev-parse HEAD)" = "$revision"
done
./scripts/feeds install -p packages bash curl ruby unzip
./scripts/feeds install -p luci luci luci-compat luci-i18n-base-zh-cn
cp "$config" .config
make defconfig 2>&1 | tee x86_64-defconfig.log

python3 - "$work/.config" <<'PY'
from pathlib import Path
import sys
config = set(Path(sys.argv[1]).read_text().splitlines())
required = {
    'CONFIG_TARGET_x86=y',
    'CONFIG_TARGET_x86_64=y',
    'CONFIG_TARGET_x86_64_DEVICE_generic=y',
    'CONFIG_TARGET_ROOTFS_EXT4FS=y',
    'CONFIG_TARGET_ROOTFS_PARTSIZE=1024',
    'CONFIG_GRUB_IMAGES=y',
    'CONFIG_PACKAGE_luci-app-openclash=y',
}
missing = sorted(required - config)
if missing:
    raise SystemExit('Required x86_64 selections missing: ' + ', '.join(missing))
if 'CONFIG_GRUB_EFI_IMAGES=y' in config:
    raise SystemExit('UEFI image unexpectedly enabled for SeaBIOS build')
kernel_config = Path(sys.argv[1]).parent / 'target/linux/x86/64/config-6.12'
kernel = set(kernel_config.read_text().splitlines())
virtio = {'CONFIG_VIRTIO=y', 'CONFIG_VIRTIO_PCI=y', 'CONFIG_VIRTIO_BLK=y',
          'CONFIG_VIRTIO_NET=y', 'CONFIG_SCSI_VIRTIO=y'}
missing_kernel = sorted(virtio - kernel)
if missing_kernel:
    raise SystemExit('OpenWrt x86_64 target is missing built-in PVE VirtIO drivers: ' + ', '.join(missing_kernel))
PY

./scripts/diffconfig.sh > x86_64-diffconfig.txt
python3 - "$lock" "$work" <<'PY'
import json, pathlib, subprocess, sys
lock = json.loads(pathlib.Path(sys.argv[1]).read_text())
work = pathlib.Path(sys.argv[2])
lock['packages_feed_commit_actual'] = subprocess.check_output(['git','-C',str(work/'feeds/packages'),'rev-parse','HEAD'],text=True).strip()
lock['luci_feed_commit_actual'] = subprocess.check_output(['git','-C',str(work/'feeds/luci'),'rev-parse','HEAD'],text=True).strip()
(work/'x86_64-build-inputs.json').write_text(json.dumps(lock,indent=2)+'\n')
PY
