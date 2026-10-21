#!/bin/sh
# Root-only compressed public-asset cache for OpenClash small-flash mode.
# Never reads subscription YAML, credentials, runtime logs or controller secrets.
set -eu
[ "$(id -u)" = 0 ] || { echo 'root required' >&2; exit 77; }
base=
if [ "${GITHUB_ACTIONS:-}" = true ] && [ -n "${K3_CACHE_TEST_ROOT:-}" ]; then base=$K3_CACHE_TEST_ROOT; fi
cache=$base/etc/openclash/k3-cache
runtime=$base/tmp/etc/openclash
persistent=$base/etc/openclash
lock=$base/tmp/k3-openclash-cache.lock
assets='clash_meta GeoSite.dat GeoIP.dat Country.mmdb ASN.mmdb Model.bin china_ip_route.ipset china_ip6_route.ipset'
umask 077
mkdir -p "$base/tmp"
if ! mkdir "$lock" 2>/dev/null; then echo 'cache operation already active' >&2; exit 75; fi
trap 'rm -f "$lock/work.gz"; rmdir "$lock" 2>/dev/null || true' EXIT
trap 'exit 1' HUP INT TERM
hash() { sha256sum "$1" | cut -d ' ' -f 1; }
valid_hash() { printf '%s\n' "$1" | grep -Eq '^[0-9a-f]{64}$'; }
asset_path() {
 if [ "$1" = clash_meta ]; then printf '%s/core/clash_meta\n' "$2"; else printf '%s/%s\n' "$2" "$1"; fi
}
archive_hash() { gzip -dc "$1" | sha256sum | cut -d ' ' -f 1; }
sync_one() {
 name=$1
 source=$(asset_path "$name" "$runtime")
 [ -f "$source" ] || source=$(asset_path "$name" "$persistent")
 [ -f "$source" ] || return 0
 [ ! -L "$source" ] || { echo "refusing symlink source: $name" >&2; return 1; }
 digest=$(hash "$source")
 dir=$cache/$name
 [ ! -L "$dir" ] || return 1
 mkdir -p "$dir"; chmod 700 "$cache" "$dir"
 previous=
 if [ -f "$dir/current.sha256" ]; then IFS= read -r previous < "$dir/current.sha256" || true; fi
 if [ "$previous" = "$digest" ] && [ -f "$dir/$digest.gz" ]; then
  [ "$(archive_hash "$dir/$digest.gz")" = "$digest" ] || return 1
  return 0
 fi
 gzip -c "$source" > "$lock/work.gz"
 [ "$(archive_hash "$lock/work.gz")" = "$digest" ] || return 1
 available=$(df -k "$cache" | tail -n 1 | awk '{print $4}')
 required=$(( ($(wc -c < "$lock/work.gz") + 1023) / 1024 + 16384 ))
 [ "$available" -ge "$required" ] || { echo 'insufficient flash reserve for cache update' >&2; return 1; }
 # Content-addressed archive first, then atomic pointer: interruption keeps old cache valid.
 cp "$lock/work.gz" "$dir/$digest.gz.new"
 chmod 600 "$dir/$digest.gz.new"
 mv "$dir/$digest.gz.new" "$dir/$digest.gz"
 sync
 printf '%s\n' "$digest" > "$dir/current.sha256.new"
 mv "$dir/current.sha256.new" "$dir/current.sha256"
 sync
 if valid_hash "$previous" && [ "$previous" != "$digest" ]; then rm -f "$dir/$previous.gz"; fi
 rm -f "$lock/work.gz"
 printf 'cached %s\n' "$name"
}
restore_one() {
 name=$1
 dir=$cache/$name
 [ -f "$dir/current.sha256" ] || return 0
 [ ! -L "$dir" ] && [ ! -L "$dir/current.sha256" ] || return 1
 IFS= read -r digest < "$dir/current.sha256"
 valid_hash "$digest" || return 1
 archive=$dir/$digest.gz
 [ -f "$archive" ] && [ ! -L "$archive" ] || return 1
 target=$(asset_path "$name" "$runtime")
 if [ -f "$target" ] && [ "$(hash "$target")" = "$digest" ]; then return 0; fi
 if [ -z "$base" ] && pidof clash clash_meta mihomo >/dev/null 2>&1; then
  echo 'stop proxy before replacing runtime assets' >&2; return 1
 fi
 available=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)
 [ "${available:-0}" -ge 131072 ] || { echo 'insufficient available RAM' >&2; return 1; }
 parent=${target%/*}
 [ ! -L "$runtime" ] && [ ! -L "$parent" ] || return 1
 mkdir -p "$parent"; chmod 700 "$runtime" "$parent"
 new=$target.k3-new
 rm -f "$new"
 if ! gzip -dc "$archive" > "$new"; then rm -f "$new"; return 1; fi
 if [ "$(hash "$new")" != "$digest" ]; then rm -f "$new"; echo 'cache digest mismatch' >&2; return 1; fi
 if [ "$name" = clash_meta ]; then chmod 700 "$new"; else chmod 600 "$new"; fi
 mv "$new" "$target"
 printf 'restored %s\n' "$name"
}
case "${1:-}" in
 sync)
  [ ! -L "$cache" ] || exit 1
  mkdir -p "$cache"
  for name in $assets; do sync_one "$name"; done
  ;;
 restore)
  for name in $assets; do restore_one "$name"; done
  [ -x "$runtime/core/clash_meta" ] || { echo 'cached core unavailable' >&2; exit 1; }
  ;;
 *) echo 'usage: k3-openclash-core-cache sync|restore' >&2; exit 64 ;;
esac
