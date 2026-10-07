#!/usr/bin/env python3
"""Build an x86_64 AppImage from the verified, engine-only Linux archive."""
import argparse
import hashlib
import os
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# Immutable release tags plus SHA-256 verification before executing tools.
TOOLS = {
    'appimagetool.AppImage': (
        'https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage',
        'ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0'),
    'runtime-x86_64': (
        'https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64',
        '2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d'),
}


def verified_tool(cache, name):
    url, digest = TOOLS[name]
    path = cache / name
    if not path.exists():
        with urllib.request.urlopen(url, timeout=120) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f'Checksum mismatch downloading {name}')
        path.write_bytes(data)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise ValueError(f'Checksum mismatch for cached {name}; remove it and retry')
    path.chmod(0o755)
    return path.resolve()


def stage_appdir(archive, directory):
    """Accept only the allowlisted portable release, not arbitrary build trees."""
    checksum = Path(str(archive) + '.sha256').read_text().split()[0]
    if hashlib.sha256(archive.read_bytes()).hexdigest() != checksum:
        raise ValueError('Portable archive checksum mismatch')
    with tarfile.open(archive) as stream:
        members = stream.getmembers()
        roots = {Path(member.name).parts[0] for member in members}
        if len(roots) != 1:
            raise ValueError('Expected one portable archive root')
        root = next(iter(roots))
        for member in members:
            parts = Path(member.name).parts
            relative = parts[1:]
            if member.issym() or member.islnk() or not (member.isfile() or member.isdir()) or '..' in parts or Path(member.name).is_absolute():
                raise ValueError(f'Unsafe archive member: {member.name}')
            if relative and relative[0] not in {'impera', 'Impera.sh', 'README.md', 'Licenses', 'textures'}:
                raise ValueError(f'Unexpected archive member: {member.name}')
        filters = {'filter': 'data'} if hasattr(tarfile, 'data_filter') else {}
        stream.extractall(directory, **filters)
    appdir = directory / root
    shutil.copy2(appdir / 'Impera.sh', appdir / 'AppRun')
    for name in ['impera.desktop', 'impera.svg']:
        shutil.copy2(REPO / 'packaging' / name, appdir / name)
    shutil.copy2(REPO / 'packaging/AppImage-runtime-LICENSE.txt', appdir / 'Licenses/AppImage-runtime.txt')
    (appdir / '.DirIcon').symlink_to('impera.svg')
    return appdir


def build(archive, cache):
    cache.mkdir(parents=True, exist_ok=True)
    tool = verified_tool(cache, 'appimagetool.AppImage')
    runtime = verified_tool(cache, 'runtime-x86_64')
    output = archive.with_name(archive.name.removesuffix('.tar.gz') + '.AppImage')
    with tempfile.TemporaryDirectory(prefix='impera-appimage-') as temp:
        temporary = Path(temp)
        appdir = stage_appdir(archive, temporary / 'package')
        # Extraction avoids requiring /dev/fuse on CI and in cloud environments.
        subprocess.run([str(tool), '--appimage-extract'], cwd=temporary, check=True, stdout=subprocess.DEVNULL)
        subprocess.run([str(temporary / 'squashfs-root/AppRun'), '--no-appstream',
                        '--runtime-file', str(runtime), '--mksquashfs-opt', '-processors',
                        '--mksquashfs-opt', '2', str(appdir), str(output)],
                       env=dict(os.environ, ARCH='x86_64'), check=True)
    output.chmod(0o755)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    Path(str(output) + '.sha256').write_text(f'{digest}  {output.name}\n')
    print(output)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--tool-cache', type=Path, default=Path('release-build/appimage-tools'))
    args = parser.parse_args()
    build(args.archive.resolve(), args.tool_cache.resolve())
