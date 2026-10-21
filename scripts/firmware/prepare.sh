#!/bin/bash
# 在独立 Linux 构建目录运行，不访问路由器。
set -euo pipefail
project=$(cd "$(dirname "$0")/../.." && pwd)
work=${1:?Usage: prepare.sh EMPTY_BUILD_DIRECTORY}
[ ! -e "$work" ] || { echo '构建目录已存在，请选用新的独立目录。' >&2; exit 1; }
mkdir -p "$work"
work=$(cd "$work" && pwd)
python3 - "$project/config/k3/source-lock.json" "$work" <<'PY'
import hashlib,json,pathlib,sys,urllib.request
lock=json.load(open(sys.argv[1])); work=pathlib.Path(sys.argv[2])
request=urllib.request.Request(lock['archive_url'],headers={'User-Agent':'k3-openwrt-build'})
with urllib.request.urlopen(request,timeout=120) as r: data=r.read()
if hashlib.sha256(data).hexdigest()!=lock['archive_sha256']:
    raise SystemExit('OpenWrt source SHA256 mismatch')
(work/'source.tar.gz').write_bytes(data)
PY
tar -xzf "$work/source.tar.gz" --strip-components=1 -C "$work"
cd "$work"
mkdir -p package/njit8021xclient
cp "$project/config/njit/package-dd28c17f/Makefile" package/njit8021xclient/
# 使用发行版自带的固定 feeds 提交，不跟踪分支最新版本。
./scripts/feeds update packages luci
./scripts/feeds install luci luci-i18n-base-zh-cn libpcap openssl
cp "$project/config/k3/firmware.config" .config
make defconfig 2>&1 | tee k3-defconfig.log
if grep -q "recursive dependency detected" k3-defconfig.log; then
  echo "Kconfig dependency cycle; refusing to build" >&2
  exit 1
fi
python3 - <<'PY'
from pathlib import Path
s=Path('.config').read_text()
for item in ('CONFIG_TARGET_bcm53xx_generic_DEVICE_phicomm_k3=y',
             'CONFIG_PACKAGE_njit8021xclient=y','CONFIG_PACKAGE_luci=y',
             'CONFIG_TARGET_ROOTFS_INITRAMFS=y'):
    if item not in s.splitlines(): raise SystemExit('Required selection missing: '+item)
PY
mkdir -p files/etc files/usr/share/doc/njit-client
cp "$project/config/k3/source-lock.json" files/etc/k3-source-lock.json
cat > files/usr/share/doc/njit-client/README <<'TXT'
NJIT client integrated from bitdust/njit8021xclient dd28c17f.
No credentials or automatic authentication service are included.
Configure only after verifying the actual WAN authentication interface.
The upstream CLI accepts credentials in argv; do not put real credentials
in shell history, logs, screenshots, firmware images, or public repositories.
Do not start a second authentication client while another is running.
TXT
./scripts/diffconfig.sh > k3-diffconfig.txt
cp "$project/config/k3/source-lock.json" k3-source-lock.json
{
  echo 'SOURCE=official tagged archive; checksum verified'
  cat feeds.conf.default
  for feed in packages luci; do
    printf '%s ' "$feed"
    git -C "feeds/$feed" rev-parse HEAD
  done
} > k3-build-inputs.txt
