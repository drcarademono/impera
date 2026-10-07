"""Packaging regression: private game data can never enter release archives."""
import importlib.util
import tempfile
import sys
from unittest import mock
import unittest
import tarfile
import zipfile
import struct
from pathlib import Path

spec = importlib.util.spec_from_file_location('packager', Path(__file__).resolve().parents[1] / 'scripts/package-release.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)
appspec = importlib.util.spec_from_file_location('appimage', Path(__file__).resolve().parents[1] / 'scripts/build-appimage.py')
appimage = importlib.util.module_from_spec(appspec)
appspec.loader.exec_module(appimage)

assembly_spec = importlib.util.spec_from_file_location('assembly', Path(__file__).resolve().parents[1] / 'scripts/assemble-release.py')
assembly = importlib.util.module_from_spec(assembly_spec)
assembly_spec.loader.exec_module(assembly)

icon_spec = importlib.util.spec_from_file_location('icons', Path(__file__).resolve().parents[1] / 'scripts/verify-package-icons.py')
icons = importlib.util.module_from_spec(icon_spec)
icon_spec.loader.exec_module(icons)


def icon_pe_fixture(images):
    """Minimal PE resource section exercising the same reader used on CI binaries."""
    group = struct.pack('<HHH', 0, 1, len(images))
    for i, image in enumerate(images):
        group += struct.pack('<BBBBHHIH', 0, 0, 0, 0, 1, 32, len(image), i+1)
    tree = {3: {i+1: {1033: image} for i, image in enumerate(images)},
            14: {1: {1033: group}}}
    resource = bytearray()

    def allocate(size):
        offset = len(resource)
        resource.extend(bytes(size))
        return offset

    def directory(entries):
        offset = allocate(16+len(entries)*8)
        struct.pack_into('<HH', resource, offset+12, 0, len(entries))
        for i, (name, value) in enumerate(entries.items()):
            if isinstance(value, dict):
                child = directory(value) | 0x80000000
            else:
                child = allocate(16)
                start = allocate(len(value))
                resource[start:start+len(value)] = value
                struct.pack_into('<II', resource, child, 0x1000+start, len(value))
            struct.pack_into('<II', resource, offset+16+i*8, name, child)
        return offset
    directory(tree)
    header = bytearray(512)
    header[:2] = b'MZ';struct.pack_into('<I', header, 0x3c, 0x80)
    header[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', header, 0x84, 0x8664, 1)
    struct.pack_into('<H', header, 0x94, 240)
    struct.pack_into('<H', header, 0x98, 0x20b)
    struct.pack_into('<II', header, 0x98+112+16, 0x1000, len(resource))
    struct.pack_into('<IIII', header, 0x98+240+8, len(resource), 0x1000, len(resource), 512)
    return header+resource


class PackagingTest(unittest.TestCase):
    def test_platform_archives(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build = root / 'build'
            build.mkdir()
            (build / 'ultima5').write_bytes(b'engine')
            (build / 'ultima5.exe').write_bytes(b'engine')
            (build / 'SAVED.GAM').write_bytes(b'private')
            for name in ['sdl3-src/LICENSE.txt', 'sdl3_mixer-src/LICENSE.txt', 'sdl3_mixer-src/src/dr_libs/LICENSE', 'sdl3_mixer-src/src/stb_vorbis/stb_vorbis.h']:
                path = build / '_deps' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('This software is available under 2 licenses - test notice')
            output = root / 'dist'
            output.mkdir()
            for platform in ['linux-x86_64', 'windows-x86_64', 'macos-x86_64', 'macos-arm64']:
                # Reproduce Windows chmod behavior even on a Unix runner.
                with mock.patch.object(Path, 'chmod', return_value=None):
                    archive = packager.package(build, platform, 'v0.1.0', output)
                if platform == 'windows-x86_64':
                    staged = packager.package(build, platform, 'v0.1.0', root / 'unpacked', unpacked=True)
                    self.assertEqual((staged / 'Run Impera.cmd').read_bytes(), b'@echo off\r\ncd /d "%~dp0"\r\nImpera.exe %*\r\n')
                    self.assertEqual({p.relative_to(staged).as_posix() for p in staged.rglob('*') if p.is_file()}, assembly.WINDOWS_FILES)
                if archive.suffix == '.zip':
                    with zipfile.ZipFile(archive) as f:
                        names = f.namelist()
                else:
                    with tarfile.open(archive) as f:
                        names = f.getnames()
                        launch = next(m for m in f.getmembers() if m.name.endswith(('/Impera.sh', '/MacOS/Impera')))
                        self.assertEqual(launch.mode, 0o755)
                        engine = next(m for m in f.getmembers() if m.name.endswith(('/impera', '/MacOS/impera-engine')))
                        self.assertEqual(engine.mode, 0o755)
                        readme = next(m for m in f.getmembers() if m.name.endswith('/README.md'))
                        self.assertEqual(readme.mode, 0o644)
                self.assertFalse(any('/textures/' in name for name in names))
                self.assertTrue(any('/Licenses/SDL.txt' in name for name in names))
                self.assertFalse(any(name.upper().endswith(('.GAM', '.OOL', '.NPC', '.16', '.CH', '.MP3')) for name in names))
                self.assertTrue(Path(str(archive) + '.sha256').is_file())
                if platform.startswith('macos-'):
                    icons.verify_mac(archive)
                # AppDir staging uses Linux filesystem permissions and symlinks.
                # Archive contents above are verified on every platform.
                if platform == 'linux-x86_64' and sys.platform == 'linux':
                    appdir = appimage.stage_appdir(archive, root / 'AppDir-stage')
                    self.assertTrue((appdir / 'AppRun').stat().st_mode & 0o111)
                    self.assertTrue((appdir / '.DirIcon').is_symlink())
                    self.assertIn('Icon=impera', (appdir / 'impera.desktop').read_text())
                    icons.verify_appdir(appdir)
                    self.assertTrue((appdir / 'Licenses/AppImage-runtime.txt').is_file())
                    self.assertFalse((appdir / 'SAVED.GAM').exists())
                    with archive.open('ab') as file:
                        file.write(b'corrupt')
                    with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                        appimage.stage_appdir(archive, root / 'bad-stage')

    def test_release_downloads(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            artifacts = root / 'artifacts'
            artifacts.mkdir()
            for platform, suffix in assembly.PLATFORMS.items():
                directory = artifacts / f'Impera-{platform}'
                directory.mkdir()
                name = f'Impera-v0.1.0-{platform}{suffix}'
                (directory / name).write_bytes(b'package')
                digest = assembly.hashlib.sha256(b'package').hexdigest()
                (directory / (name + '.sha256')).write_text(f'{digest}  {name}\n')
            windows = artifacts / 'Impera-windows-x86_64'
            for name in assembly.WINDOWS_FILES:
                file = windows / name
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(b'package')
            output = root / 'dist'
            assembly.assemble(artifacts, 'v0.1.0', output)
            self.assertEqual(len(list(output.iterdir())), 8)
            self.assertFalse(list(output.glob('*linux*.tar.gz')))
            with zipfile.ZipFile(next(output.glob('*.zip'))) as bundle:
                self.assertTrue(any(n.endswith('/Impera.exe') for n in bundle.namelist()))
                self.assertFalse(any(n.endswith('.zip') for n in bundle.namelist()))
            (windows / 'SAVED.GAM').write_bytes(b'private')
            with self.assertRaisesRegex(ValueError, 'Windows package contents'):
                assembly.assemble(artifacts, 'v0.1.0', root / 'bad')
            (windows / 'SAVED.GAM').unlink()
            linux = artifacts / 'Impera-linux-x86_64'
            (linux / 'internal.tar.gz').write_bytes(b'internal')
            with self.assertRaisesRegex(ValueError, 'Unexpected release files'):
                assembly.assemble(artifacts, 'v0.1.0', root / 'bad')

    def test_cached_tool_verification(self):
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp)
            (cache / 'appimagetool.AppImage').write_bytes(b'corrupt executable')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                appimage.verified_tool(cache, 'appimagetool.AppImage')

    def test_windows_icon_resource_verification(self):
        with tempfile.TemporaryDirectory() as temp:
            executable = Path(temp) / 'Impera.exe'
            images = icons.ico_images((icons.ROOT / 'packaging/impera.ico').read_bytes())
            executable.write_bytes(icon_pe_fixture(images))
            icons.verify_windows(executable)
            executable.write_bytes(icon_pe_fixture([b'wrong artwork', *images[1:]]))
            with self.assertRaisesRegex(AssertionError, 'does not match'):
                icons.verify_windows(executable)
            executable.write_bytes(b'not a PE executable')
            with self.assertRaisesRegex(AssertionError, 'Windows executable'):
                icons.verify_windows(executable)


if __name__ == '__main__':
    unittest.main()
