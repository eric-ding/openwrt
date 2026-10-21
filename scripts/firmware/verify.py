#!/usr/bin/env python3
"""验证本地构建输出，绝不安装或刷写。"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import zlib


def check_trx(path, *, nand):
    data = path.read_bytes()
    if len(data) < 28:
        raise ValueError('TRX too short')
    magic, size, crc, flags, first, second, third = struct.unpack_from('<7I', data)
    if magic != 0x30524448 or size != len(data):
        raise ValueError('TRX magic or size mismatch')
    if (zlib.crc32(data[12:]) ^ 0xffffffff) != crc:
        raise ValueError('TRX CRC mismatch')
    if first != 28 or third != 0:
        raise ValueError('Unexpected TRX offsets')
    if nand:
        if second != 0x400000 or data[second:second+4] != b'UBI#':
            raise ValueError('Unexpected NAND kernel/UBI boundary')
        if struct.unpack_from('>II', data, second+16) != (2048, 4096):
            raise ValueError('Unexpected UBI geometry')
        if size > 0x07c00000:
            raise ValueError('Image exceeds firmware partition')
    return {'file': path.name, 'bytes': size, 'sha256': hashlib.sha256(data).hexdigest(),
            'trx_crc_valid': True, 'partitions': [first, second, third]}


def main():
    work, output = map(Path, sys.argv[1:3])
    target = work / 'bin/targets/bcm53xx/generic'
    firmware = list(target.glob('*phicomm_k3-squashfs.trx'))
    ram = list(target.glob('*phicomm_k3-initramfs.trx'))
    if len(firmware) != 1 or len(ram) != 1:
        raise SystemExit('Expected one K3 squashfs TRX and one initramfs TRX')
    report = [check_trx(firmware[0], nand=True), check_trx(ram[0], nand=False)]
    manifests = list(target.glob('*.manifest'))
    if not any(any(line.startswith('njit8021xclient - ') for line in p.read_text().splitlines())
               for p in manifests):
        raise SystemExit('njit missing from image manifest')
    binaries = list(work.glob('build_dir/target-*/root-bcm53xx/usr/sbin/njit-client'))
    if len(binaries) != 1:
        raise SystemExit('Expected one installed njit binary')
    binary = binaries[0]
    elf = binary.read_bytes()
    if elf[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', elf, 18)[0] != 40:
        raise SystemExit('njit is not ARM32 little endian ELF')
    if struct.unpack_from('<I', elf, 36)[0] & 0x400:
        raise SystemExit('Unexpected hard-float ABI in njit')
    root = binary.parents[2]
    if not (root / 'lib/ld-musl-arm.so.1').is_symlink() and not (root / 'lib/ld-musl-arm.so.1').is_file():
        raise SystemExit('ARM musl loader missing from staged root')
    if os.getuid() == 0:
        raise SystemExit('Non-root smoke test must run as an unprivileged build user')
    result = subprocess.run(['qemu-arm', '-cpu', 'cortex-a9', '-L', str(root), str(binary)],
                            capture_output=True, timeout=15)
    if result.returncode != 255 or 'root'.encode() not in result.stderr:
        raise SystemExit('njit non-root smoke test failed')
    if output.exists():
        raise SystemExit('Output directory already exists; refusing mixed artifacts')
    output.mkdir(parents=True)
    for p in [*firmware, *ram, *manifests, work/'.config', work/'k3-diffconfig.txt',
              work/'k3-source-lock.json', work/'k3-build-inputs.txt']:
        shutil.copy2(p, output / ('full.config' if p.name == '.config' else p.name))
    for p in target.glob('profiles.json'):
        shutil.copy2(p, output/p.name)
    if (target/'packages').is_dir():
        with tarfile.open(output/'target-kmods.tar.gz', 'w:gz') as archive:
            archive.add(target/'packages', arcname='packages')
    for package in (work/'bin/packages').glob('**/njit8021xclient-*.apk'):
        shutil.copy2(package, output/package.name)
    njit_info = subprocess.check_output(['readelf', '-h', '-d', str(binary)], text=True)
    (output/'njit-elf.txt').write_text(njit_info)
    (output/'verification.json').write_text(json.dumps({
        'images': report, 'njit_arm32': True, 'njit_nonroot_smoke': 'passed',
        'njit_sha256': hashlib.sha256(elf).hexdigest(),
        'real_hardware_boot': 'not tested', 'real_8021x': 'not tested',
        'cfe_miniweb_compatible': False}, indent=2)+'\n')
    (output/'READ-BEFORE-FLASH.txt').write_text(
        '自编译实验固件，未实机启动、未验证认证。\n'
        '普通 TRX 仍不符合当前 K3 CFE MiniWeb 的斐讯 OEM 格式。\n'
        'initramfs 是 RAM 启动研究产物，不能当普通升级固件上传。\n'
        '内置 njit 程序；没有账号密码、自动认证服务或 OpenClash。\n'
        '必须另行确认安装与恢复方案后才能刷写。\n')
    checks = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
                     for p in sorted(output.iterdir()) if p.is_file())
    (output/'SHA256SUMS').write_text(checks)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
