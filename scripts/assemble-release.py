#!/usr/bin/env python3
"""Assemble only public downloads, rejecting unexpected artifact contents."""
import argparse
import hashlib
import re
import shutil
import zipfile
from pathlib import Path

PLATFORMS = {'linux-x86_64': '.AppImage', 'macos-x86_64': '.tar.gz',
             'macos-arm64': '.tar.gz'}
WINDOWS_FILES = {'Impera.exe', 'Run Impera.cmd', 'README.md',
                 'Licenses/SDL.txt', 'Licenses/SDL_mixer.txt',
                 'Licenses/dr_libs.txt', 'Licenses/stb_vorbis.txt'}


def assemble(artifacts, version, output, with_workshop=False):
    version = re.sub(r'[^A-Za-z0-9._-]', '-', version)
    expected = {f'Impera-{p}' for p in (*PLATFORMS, 'windows-x86_64')}
    if with_workshop:
        expected |= {f'Impera-Workshop-{p}' for p in (*PLATFORMS, 'windows-x86_64')}
    if {p.name for p in artifacts.iterdir()} != expected:
        raise ValueError('Missing or unexpected platform artifacts')
    output.mkdir(parents=True, exist_ok=True)
    for platform, suffix in PLATFORMS.items():
        source = artifacts / f'Impera-{platform}'
        name = f'Impera-{version}-{platform}{suffix}'
        if {p.name for p in source.iterdir()} != {name, name + '.sha256'}:
            raise ValueError(f'Unexpected release files for {platform}')
        archive = source / name
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if (source / (name + '.sha256')).read_text().strip() != f'{digest}  {name}':
            raise ValueError(f'Checksum mismatch for {name}')
        shutil.copy2(archive, output / name)
        shutil.copy2(source / (name + '.sha256'), output / (name + '.sha256'))
    source = artifacts / 'Impera-windows-x86_64'
    files = {p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()}
    if files != WINDOWS_FILES or any(p.is_symlink() for p in source.rglob('*')):
        raise ValueError('Unexpected Windows package contents')
    name = f'Impera-{version}-windows-x86_64'
    archive = output / (name + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for file in sorted(files):
            bundle.write(source / file, f'{name}/{file}')
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / (archive.name + '.sha256')).write_text(f'{digest}  {archive.name}\n')

    if with_workshop:
        assemble_workshop(artifacts, version, output)


def assemble_workshop(artifacts, version, output):
    for platform, suffix in PLATFORMS.items():
        source = artifacts / f'Impera-Workshop-{platform}'
        name = f'ImperaWorkshop-{version}-{platform}{suffix}'
        if {p.name for p in source.iterdir()} != {name, name + '.sha256'}:
            raise ValueError(f'Unexpected Workshop release files for {platform}')
        digest = hashlib.sha256((source / name).read_bytes()).hexdigest()
        if (source / (name + '.sha256')).read_text().strip() != f'{digest}  {name}':
            raise ValueError(f'Checksum mismatch for {name}')
        shutil.copy2(source / name, output / name)
        shutil.copy2(source / (name + '.sha256'), output / (name + '.sha256'))
    source = artifacts / 'Impera-Workshop-windows-x86_64'
    files = {p.relative_to(source).as_posix() for p in source.rglob('*') if p.is_file()}
    required = {'bin/ImperaWorkshop.exe', 'bin/Qt6Core.dll', 'bin/Qt6Gui.dll',
                'bin/Qt6Widgets.dll', 'plugins/platforms/qwindows.dll',
                'bin/qt.conf', 'README.md', 'Run Workshop.cmd', 'Licenses/Qt-runtime.txt'}
    def allowed(file):
        return (file in required or file.startswith('Licenses/Qt/') or
                file.startswith(('bin/', 'plugins/')) and file.endswith('.dll'))
    if not required <= files or any(not allowed(f) for f in files) or any(p.is_symlink() for p in source.rglob('*')):
        raise ValueError('Unexpected or incomplete Windows Workshop package contents')
    name = f'ImperaWorkshop-{version}-windows-x86_64'
    archive = output / (name + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for file in sorted(files):
            bundle.write(source / file, f'{name}/{file}')
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / (archive.name + '.sha256')).write_text(f'{digest}  {archive.name}\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--with-workshop', action='store_true')
    parser.add_argument('--output', type=Path, default=Path('dist'))
    args = parser.parse_args()
    assemble(args.artifacts, args.version, args.output, args.with_workshop)
