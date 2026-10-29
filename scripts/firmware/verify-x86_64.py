#!/usr/bin/env python3
"""Verify the PVE SeaBIOS x86_64 OpenWrt artifact without installing it."""
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys

work, output = map(Path, sys.argv[1:3])
target = work / 'bin/targets/x86/64'
images = list(target.glob('*-generic-ext4-combined.img.gz'))
if len(images) != 1:
    raise SystemExit(f'Expected one SeaBIOS generic ext4 combined image, found {len(images)}')
manifests = list(target.glob('*-generic.manifest'))
if len(manifests) != 1:
    raise SystemExit(f'Expected one generic package manifest, found {len(manifests)}')
manifest = manifests[0].read_text().splitlines()
installed = {line.split(' - ',1)[0]: line.split(' - ',1)[1] for line in manifest if ' - ' in line}
if installed.get('luci-app-openclash') != '0.48.002':
    raise SystemExit('OpenClash v0.48.002 is missing from the image manifest')
for package in ('kmod-virtio-net','kmod-virtio-scsi','kmod-tun','kmod-nft-tproxy'):
    if package not in installed:
        raise SystemExit(f'Required PVE/OpenClash package missing: {package}')
if any('mihomo' in name.lower() or name.lower() == 'clash' for name in installed):
    raise SystemExit('A proxy core was unexpectedly bundled in the image')
with gzip.open(images[0], 'rb') as stream:
    header = stream.read(512)
if len(header) != 512 or header[510:512] != b'\x55\xaa':
    raise SystemExit('Combined image is not a valid BIOS/MBR boot image')
if output.exists():
    raise SystemExit('Output directory already exists; refusing mixed artifacts')
output.mkdir(parents=True)
for path in (images[0], manifests[0], work/'x86_64-diffconfig.txt', work/'x86_64-build-inputs.json'):
    shutil.copy2(path, output/path.name)
summary = {
    'image': images[0].name,
    'bytes_compressed': images[0].stat().st_size,
    'sha256': hashlib.sha256(images[0].read_bytes()).hexdigest(),
    'boot_scheme': 'SeaBIOS/legacy BIOS, MBR',
    'filesystem': 'ext4',
    'openclash': installed['luci-app-openclash'],
    'mihomo_core_bundled': False,
    'pve_virtio_net': installed['kmod-virtio-net'],
    'pve_virtio_scsi': installed['kmod-virtio-scsi'],
    'hardware_boot_test': 'not performed',
}
(output/'verification.json').write_text(json.dumps(summary, indent=2)+'\n')
(output/'SHA256SUMS').write_text(f"{summary['sha256']}  {images[0].name}\n")
print(json.dumps(summary, indent=2))
