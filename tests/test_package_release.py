"""Packaging regression: private game data can never enter release archives."""
import importlib.util
import tempfile
import sys
from unittest import mock
import unittest
import tarfile
import zipfile
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
                # AppDir staging uses Linux filesystem permissions and symlinks.
                # Archive contents above are verified on every platform.
                if platform == 'linux-x86_64' and sys.platform == 'linux':
                    appdir = appimage.stage_appdir(archive, root / 'AppDir-stage')
                    self.assertTrue((appdir / 'AppRun').stat().st_mode & 0o111)
                    self.assertTrue((appdir / '.DirIcon').is_symlink())
                    self.assertIn('Icon=impera', (appdir / 'impera.desktop').read_text())
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


if __name__ == '__main__':
    unittest.main()
