#!/usr/bin/env python3
"""Verify native release icons against the canonical SVG's generated derivatives."""
import argparse
import plistlib
import struct
import subprocess
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def ico_images(data):
    reserved, kind, count = struct.unpack_from('<HHH', data)
    assert reserved == 0 and kind == 1 and count >= 1, 'Invalid ICO'
    return [data[offset:offset+length] for i in range(count)
            for length, offset in [struct.unpack_from('<II', data, 6+i*16+8)]]


def pe_resources(data):
    """Read IMAGE_RESOURCE_DIRECTORY leaves keyed by type/name/language."""
    assert data[:2] == b'MZ', 'Not a Windows executable'
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe+4] == b'PE\0\0', 'Invalid PE signature'
    sections = struct.unpack_from('<H', data, pe+6)[0]
    optional_size = struct.unpack_from('<H', data, pe+20)[0]
    optional = pe+24
    magic = struct.unpack_from('<H', data, optional)[0]
    assert magic in (0x10b, 0x20b), 'Unsupported PE format'
    directory = optional+(112 if magic == 0x20b else 96)
    resource_rva = struct.unpack_from('<I', data, directory+2*8)[0]
    assert resource_rva, 'Executable has no native resources'
    table = optional+optional_size

    def address(rva):
        for i in range(sections):
            size, start, raw_size, raw = struct.unpack_from('<IIII', data, table+i*40+8)
            if start <= rva < start+max(size, raw_size):
                return raw+rva-start
        raise AssertionError('Resource RVA outside PE sections')

    base = address(resource_rva)
    result = {}

    def walk(offset, path):
        assert len(path) < 4, 'Malformed resource tree'
        named, ids = struct.unpack_from('<HH', data, base+offset+12)
        for i in range(named+ids):
            name, child = struct.unpack_from('<II', data, base+offset+16+i*8)
            if name & 0x80000000:
                pos = base+(name & 0x7fffffff)
                length = struct.unpack_from('<H', data, pos)[0]
                name = data[pos+2:pos+2+length*2].decode('utf-16le')
            key = (*path, name)
            if child & 0x80000000:
                walk(child & 0x7fffffff, key)
            else:
                rva, size = struct.unpack_from('<II', data, base+child)
                start = address(rva)
                result[key] = data[start:start+size]
    walk(0, ())
    return result


def verify_windows(executable):
    resources = pe_resources(executable.read_bytes())
    expected = ico_images((ROOT / 'packaging/impera.ico').read_bytes())
    groups = [value for key, value in resources.items() if key[0] == 14]
    assert groups, 'Missing RT_GROUP_ICON in Impera.exe'
    for group in groups:
        reserved, kind, count = struct.unpack_from('<HHH', group)
        if (reserved, kind, count) != (0, 1, len(expected)):
            continue
        images = []
        for i in range(count):
            size, icon_id = struct.unpack_from('<IH', group, 6+i*14+8)
            matching = [value for key, value in resources.items()
                        if key[0] == 3 and key[1] == icon_id]
            assert matching and len(matching[0]) == size, 'Missing RT_ICON image'
            images.append(matching[0])
        if images == expected:
            return
    raise AssertionError('Embedded Windows icon does not match Impera ICO')


def verify_mac(archive):
    with tarfile.open(archive) as stream:
        names = stream.getnames()
        plist = next(name for name in names if name.endswith('/Impera.app/Contents/Info.plist'))
        metadata = plistlib.loads(stream.extractfile(plist).read())
        assert metadata['CFBundleIconFile'] == 'impera.icns', 'Missing bundle icon metadata'
        icon = plist.removesuffix('Info.plist')+'Resources/impera.icns'
        assert stream.extractfile(icon).read() == (ROOT / 'packaging/impera.icns').read_bytes(), 'Incorrect macOS icon'


def verify_appdir(appdir):
    assert (appdir / 'impera.svg').read_bytes() == (ROOT / 'packaging/impera.svg').read_bytes()
    assert (appdir / '.DirIcon').resolve() == (appdir / 'impera.svg').resolve()
    assert 'Icon=impera\n' in (appdir / 'impera.desktop').read_text()


def verify(platform, directory):
    if platform.startswith('windows-'):
        verify_windows(next(directory.glob('*/Impera.exe')))
    elif platform.startswith('macos-'):
        verify_mac(next(directory.glob(f'*-{platform}.tar.gz')))
    else:
        appimage = next(directory.glob('*.AppImage')).resolve()
        with tempfile.TemporaryDirectory() as temporary:
            subprocess.run([str(appimage), '--appimage-extract'], cwd=temporary,
                           check=True, stdout=subprocess.DEVNULL)
            verify_appdir(Path(temporary) / 'squashfs-root')
    print(f'{platform}: packaged Impera icon verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', required=True, choices=['windows-x86_64', 'macos-x86_64', 'macos-arm64', 'linux-x86_64'])
    parser.add_argument('--package-root', required=True, type=Path)
    args = parser.parse_args()
    verify(args.platform, args.package_root)
