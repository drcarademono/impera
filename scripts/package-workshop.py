#!/usr/bin/env python3
"""Package only the CMake-installed Workshop, including its deployed Qt runtime."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


# The host must supply glibc and graphics drivers. Bundle the other plugin
# dependencies (notably xcb-cursor), which a desktop need not have installed.
HOST_LIBRARIES = re.compile(r'^(ld-linux|lib(c|m|dl|pthread|rt|resolv|nss_.*)\.so|'
                            r'lib(GL|EGL|GLX|GLdispatch|OpenGL|drm|gbm|vulkan)\.so)')


def linux_license(source, stage):
    # Ubuntu's copyright files include notices for the bundled system libraries.
    owner = subprocess.run(['dpkg-query', '-S', str(source)], capture_output=True, text=True)
    if owner.returncode:
        return  # SDK Qt libraries are covered by the Qt notices.
    for line in owner.stdout.splitlines():
        package_name = line.split(':', 1)[0]
        copyright_file = Path('/usr/share/doc') / package_name / 'copyright'
        if copyright_file.is_file():
            destination = stage / 'Licenses/Linux'
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copy2(copyright_file, destination / (package_name + '.txt'))


def deploy_linux_plugins(stage, qt_root):
    for category in ('platforms', 'imageformats', 'platforminputcontexts', 'xcbglintegrations'):
        source = qt_root / 'plugins' / category
        if source.is_dir():
            shutil.copytree(source, stage / 'plugins' / category, dirs_exist_ok=True)
    # Desktop application: no embedded-device/framebuffer platform backends.
    for path in (stage / 'plugins/platforms').glob('*.so'):
        if path.name not in {'libqxcb.so', 'libqoffscreen.so', 'libqminimal.so'}:
            path.unlink()
    for plugin in ('libqxcb.so', 'libqoffscreen.so'):
        if not (stage / 'plugins/platforms' / plugin).is_file():
            raise ValueError(f'Required deployed Qt platform plugin missing: {plugin}')
    pending = [stage / 'bin/ImperaWorkshop', *stage.glob('plugins/**/*.so')]
    checked = set()
    while pending:
        binary = pending.pop()
        if binary in checked:
            continue
        checked.add(binary)
        result = subprocess.run(['ldd', str(binary)], capture_output=True, text=True, check=True)
        if 'not found' in result.stdout:
            raise ValueError(f'Unresolved dependencies for {binary}: {result.stdout}')
        for line in result.stdout.splitlines():
            match = re.match(r'\s*(\S+) => (/\S+) ', line)
            if not match or HOST_LIBRARIES.match(match[1]):
                continue
            destination = stage / 'lib' / match[1]
            if not destination.exists():
                shutil.copy2(match[2], destination)
                linux_license(Path(match[2]), stage)
                pending.append(destination)
    # Qt's install script supplies relative plugin paths; the launcher supplies
    # LD_LIBRARY_PATH so even plugins whose build RPATH points to the SDK work.


def package(build, platform, version, output):
    version = re.sub(r'[^A-Za-z0-9._-]', '-', version)
    name = f'ImperaWorkshop-{version}-{platform}'
    stage = output / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    # Install only declared targets and Qt's dependency closure, never a build tree.
    subprocess.run(['cmake', '--install', str(build), '--config', 'Release',
                    '--prefix', str(stage.resolve())], check=True)
    shutil.copy2(ROOT / 'workshop/README.md', stage / 'README.md')
    licenses = stage / 'Licenses'
    licenses.mkdir(exist_ok=True)
    # Qt is dynamically linked; include LGPL/GPL notices and source instructions.
    qt_root = Path(os.environ['QT_ROOT_DIR'])
    qt_licenses = ROOT / 'packaging/qt-licenses'
    shutil.copytree(qt_licenses, licenses / 'Qt', dirs_exist_ok=True)
    (licenses / 'Qt-runtime.txt').write_text(
        'Qt 6.8.3 is dynamically linked, with unmodified shared libraries.\n'
        'License notices are in Qt/. Qt source: https://download.qt.io/archive/qt/6.8/6.8.3/single/\n'
        'You may replace these libraries with compatible Qt builds, including modified builds.\n')
    if platform.startswith('windows-'):
        if not (stage / 'bin/ImperaWorkshop.exe').is_file() or not (stage / 'bin/Qt6Widgets.dll').is_file():
            raise ValueError('Workshop executable or deployed Qt Widgets DLL missing')
        (stage / 'Run Workshop.cmd').write_text('@echo off\r\n"%~dp0bin\\ImperaWorkshop.exe" %*\r\n', newline='')
        return stage  # Actions supplies the outer ZIP; assembly makes the Release ZIP.
    if platform.startswith('macos-'):
        app = stage / 'ImperaWorkshop.app'
        if not (app / 'Contents/Frameworks/QtWidgets.framework').is_dir():
            raise ValueError('Deployed macOS Qt Widgets framework missing')
        subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(app)], check=True)
        subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)
        archive = output / (name + '.tar.gz')
        with tarfile.open(archive, 'w:gz') as stream:
            stream.add(stage, arcname=name)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        Path(str(archive) + '.sha256').write_text(f'{digest}  {archive.name}\n')
    else:
        deploy_linux_plugins(stage, qt_root)
        if not (stage / 'bin/ImperaWorkshop').is_file() or not list((stage / 'lib').glob('libQt6Widgets.so*')):
            raise ValueError('Workshop executable or deployed Qt Widgets library missing')
        launcher = stage / 'AppRun'
        launcher.write_text('''#!/bin/sh
set -eu
app_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$app_dir/bin/ImperaWorkshop" "$@"
''')
        launcher.chmod(0o755)
        shutil.copy2(ROOT / 'packaging/impera.svg', stage / 'impera.svg')
        (stage / '.DirIcon').symlink_to('impera.svg')
        (stage / 'impera-workshop.desktop').write_text('''[Desktop Entry]
Type=Application
Name=Impera Workshop
Comment=Create Ultima 5 mods for Impera
Exec=ImperaWorkshop
Icon=impera
Categories=Game;Development;
Terminal=false
''')
        shutil.copy2(ROOT / 'packaging/AppImage-runtime-LICENSE.txt', licenses / 'AppImage-runtime.txt')
        spec = importlib.util.spec_from_file_location('appimage', ROOT / 'scripts/build-appimage.py')
        appimage = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(appimage)
        archive = appimage.build_appdir(stage, output / (name + '.AppImage'), build / 'appimage-tools')
    shutil.rmtree(stage)
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--platform', choices=['linux-x86_64', 'windows-x86_64', 'macos-x86_64', 'macos-arm64'], required=True)
    parser.add_argument('--version', default=os.environ.get('RELEASE_VERSION'))
    parser.add_argument('--output', type=Path, default=Path('workshop-dist'))
    args = parser.parse_args()
    if not args.version:
        parser.error('--version or RELEASE_VERSION is required')
    args.output.mkdir(parents=True, exist_ok=True)
    package(args.build.resolve(), args.platform, args.version, args.output.resolve())
