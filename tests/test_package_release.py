"""Packaging regression: private game data can never enter release archives."""
import importlib.util
import tempfile
import unittest
import tarfile
import zipfile
from pathlib import Path

spec = importlib.util.spec_from_file_location('packager', Path(__file__).resolve().parents[1] / 'scripts/package-release.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


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
                archive = packager.package(build, platform, 'v0.1.0', output)
                if archive.suffix == '.zip':
                    with zipfile.ZipFile(archive) as f:
                        names = f.namelist()
                else:
                    with tarfile.open(archive) as f:
                        names = f.getnames()
                        launch = next(m for m in f.getmembers() if m.name.endswith(('/Impera.sh', '/MacOS/Impera')))
                        self.assertTrue(launch.mode & 0o111)
                self.assertTrue(any('/textures/cursors/' in name for name in names))
                self.assertTrue(any('/Licenses/SDL.txt' in name for name in names))
                self.assertFalse(any(name.upper().endswith(('.GAM', '.OOL', '.NPC', '.16', '.CH', '.MP3')) for name in names))
                self.assertTrue(Path(str(archive) + '.sha256').is_file())


if __name__ == '__main__':
    unittest.main()
