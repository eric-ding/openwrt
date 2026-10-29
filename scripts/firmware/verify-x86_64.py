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
for package in ('dnsmasq-full','bash','curl','ca-bundle','ip-full','ruby','ruby-yaml',
                'kmod-tun','kmod-nft-tproxy','luci-compat','unzip'):
    if package not in installed:
        raise SystemExit(f'Required OpenClash package dependency missing: {package}')
if any('mihomo' in name.lower() or name.lower() == 'clash' for name in installed):
    raise SystemExit('A proxy core was unexpectedly bundled in the image')
kernel_configs = list(work.glob('build_dir/target-*/linux-x86_64/linux-*/.config'))
if len(kernel_configs) != 1:
    raise SystemExit(f'Expected one built x86_64 kernel config, found {len(kernel_configs)}')
kernel = set(kernel_configs[0].read_text().splitlines())
virtio = ('CONFIG_VIRTIO=y','CONFIG_VIRTIO_PCI=y','CONFIG_VIRTIO_BLK=y',
          'CONFIG_VIRTIO_NET=y','CONFIG_SCSI_VIRTIO=y')
missing_kernel = [item for item in virtio if item not in kernel]
if missing_kernel:
    raise SystemExit('Built kernel lacks PVE VirtIO drivers: ' + ', '.join(missing_kernel))
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
    'pve_virtio_net': 'built into kernel',
    'pve_virtio_blk': 'built into kernel',
    'pve_virtio_scsi': 'built into kernel',
    'pve_virtio_pci': 'built into kernel',
    'hardware_boot_test': 'not performed',
}
(output/'verification.json').write_text(json.dumps(summary, indent=2)+'\n')
(output/'SHA256SUMS').write_text(f"{summary['sha256']}  {images[0].name}\n")
print(json.dumps(summary, indent=2))
