#!/usr/bin/env python3
"""Allowlist-only release packaging: never copy game data or local saves."""
import argparse
import os
import hashlib
import plistlib
import re
import shutil
import tarfile
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def package(build, platform, version, destination):
    version = re.sub(r'[^A-Za-z0-9._-]', '-', version)
    name = f'Impera-{version}-{platform}'
    stage = destination / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    windows = platform.startswith('windows-')
    mac = platform.startswith('macos-')
    binary = 'ultima5.exe' if windows else 'ultima5'
    candidates = [build / 'Release' / binary, build / binary]
    source = next((p for p in candidates if p.is_file()), None)
    if not source:
        raise FileNotFoundError(f'No built executable in {build}')
    if mac:
        contents = stage / 'Impera.app' / 'Contents'
        executables = contents / 'MacOS'
        executables.mkdir(parents=True)
        shutil.copy2(source, executables / 'impera-engine')
        launcher = executables / 'Impera'
        launcher.write_text('''#!/bin/sh
set -eu
app_dir="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
runtime="$HOME/Library/Application Support/Impera"
mkdir -p "$runtime"
cd "$runtime"
exec "$app_dir/MacOS/impera-engine" "$@"
''')
        launcher.chmod(0o755)
        with (contents / 'Info.plist').open('wb') as f:
            plistlib.dump(dict(CFBundleExecutable='Impera', CFBundleIdentifier='org.impera.engine',
                              CFBundleName='Impera', CFBundlePackageType='APPL',
                              CFBundleShortVersionString=version.removeprefix('v') if re.fullmatch(r'v?\d+\.\d+\.\d+', version) else '0.0.0',
                              LSMinimumSystemVersion='12.0', NSHighResolutionCapable=True), f)
    else:
        shutil.copy2(source, stage / ('Impera.exe' if windows else 'impera'))
        if windows:
            (stage / 'Run Impera.cmd').write_text('@echo off\r\ncd /d "%~dp0"\r\nImpera.exe %*\r\n')
        else:
            launcher = stage / 'Impera.sh'
            launcher.write_text('''#!/bin/sh
set -eu
app_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
runtime="${XDG_DATA_HOME:-$HOME/.local/share}/impera"
mkdir -p "$runtime"
cd "$runtime"
exec "$app_dir/impera" "$@"
''')
            launcher.chmod(0o755)
    shutil.copy2(REPO / 'README.md', stage / 'README.md')
    licenses = stage / 'Licenses'
    licenses.mkdir()
    dependencies = build / '_deps'
    for src, label in [('sdl3-src/LICENSE.txt', 'SDL.txt'),
                       ('sdl3_mixer-src/LICENSE.txt', 'SDL_mixer.txt'),
                       ('sdl3_mixer-src/src/dr_libs/LICENSE', 'dr_libs.txt')]:
        shutil.copy2(dependencies / src, licenses / label)
    # stb_vorbis carries its MIT/public-domain notice at the end of its header.
    stb = (dependencies / 'sdl3_mixer-src/src/stb_vorbis/stb_vorbis.h').read_text()
    license_start = stb.index('This software is available under 2 licenses')
    (licenses / 'stb_vorbis.txt').write_text(stb[license_start:])
    if mac:
        archive = destination / f'{name}.tar.gz'
    elif windows:
        archive = destination / f'{name}.zip'
    else:
        archive = destination / f'{name}.tar.gz'
    if windows:
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as f:
            for path in sorted(stage.rglob('*')):
                if path.is_file():
                    f.write(path, path.relative_to(destination))
    else:
        # Windows filesystems do not retain chmod's Unix execute bits.
        # Define portable permissions in the archive itself on every host.
        executable_paths = {'impera', 'Impera.sh',
                            'Impera.app/Contents/MacOS/Impera',
                            'Impera.app/Contents/MacOS/impera-engine'}
        def portable_permissions(member):
            relative = Path(member.name).relative_to(name).as_posix()
            member.mode = 0o755 if member.isdir() or relative in executable_paths else 0o644
            return member
        with tarfile.open(archive, 'w:gz') as f:
            f.add(stage, arcname=name, filter=portable_permissions)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (destination / f'{archive.name}.sha256').write_text(f'{digest}  {archive.name}\n')
    shutil.rmtree(stage)
    print(archive)
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--platform', choices=['linux-x86_64', 'windows-x86_64', 'macos-x86_64', 'macos-arm64'], required=True)
    parser.add_argument('--version', default=os.environ.get('RELEASE_VERSION'))
    parser.add_argument('--output', type=Path, default=Path('dist'))
    args = parser.parse_args()
    if not args.version:
        parser.error("--version or RELEASE_VERSION is required")
    args.output.mkdir(parents=True, exist_ok=True)
    package(args.build.resolve(), args.platform, args.version, args.output.resolve())
