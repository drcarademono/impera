"""Workshop release assembly and the actual four-platform workflow contract."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile
import yaml

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('assembly', ROOT / 'scripts/assemble-release.py')
assembly = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assembly)


class WorkshopReleaseTest(unittest.TestCase):
    def test_complete_release_and_rejected_payloads(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = root / 'artifacts'
            for prefix, product in [('Impera', 'Impera'), ('Impera-Workshop', 'ImperaWorkshop')]:
                for platform, suffix in assembly.PLATFORMS.items():
                    folder = artifacts / f'{prefix}-{platform}'
                    folder.mkdir(parents=True)
                    name = f'{product}-vtest-{platform}{suffix}'
                    (folder / name).write_bytes(b'package')
                    digest = hashlib.sha256(b'package').hexdigest()
                    (folder / (name + '.sha256')).write_text(f'{digest}  {name}\n')
            engine = artifacts / 'Impera-windows-x86_64'
            for name in assembly.WINDOWS_FILES:
                path = engine / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'engine')
            editor = artifacts / 'Impera-Workshop-windows-x86_64'
            files = ['bin/ImperaWorkshop.exe', 'bin/Qt6Core.dll', 'bin/Qt6Gui.dll',
                     'bin/Qt6Widgets.dll', 'bin/qt.conf', 'plugins/platforms/qwindows.dll',
                     'README.md', 'Run Workshop.cmd', 'Licenses/Qt-runtime.txt',
                     'Licenses/Qt/LGPL-3.0-only.txt', 'bin/vcruntime140.dll']
            for name in files:
                path = editor / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'editor')
            output = root / 'dist'
            for _ in range(2):
                assembly.assemble(artifacts, 'vtest', output, with_workshop=True)
                self.assertEqual(len(list(output.iterdir())), 16)
                self.assertFalse(list(output.glob('*linux*.tar.gz')))
                with zipfile.ZipFile(output / 'ImperaWorkshop-vtest-windows-x86_64.zip') as archive:
                    self.assertTrue(any(n.endswith('/bin/ImperaWorkshop.exe') for n in archive.namelist()))
                    self.assertFalse(any(n.endswith('.zip') for n in archive.namelist()))
            (editor / 'SAVED.GAM').write_bytes(b'private')
            with self.assertRaisesRegex(ValueError, 'Windows Workshop'):
                assembly.assemble(artifacts, 'vtest', output, with_workshop=True)
            (editor / 'SAVED.GAM').unlink()
            (editor / 'plugins/platforms/qwindows.dll').unlink()
            with self.assertRaisesRegex(ValueError, 'Windows Workshop'):
                assembly.assemble(artifacts, 'vtest', output, with_workshop=True)
            folder = artifacts / 'Impera-Workshop-linux-x86_64'
            next(folder.glob('*.AppImage')).write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                assembly.assemble(artifacts, 'vtest', output, with_workshop=True)

    def test_workflow_preserves_platforms_and_requires_workshop_success(self):
        workflow = yaml.safe_load((ROOT / '.github/workflows/release.yml').read_text())
        builds = workflow['jobs']['build']
        self.assertEqual({p['platform'] for p in builds['strategy']['matrix']['include']},
                         {'linux-x86_64', 'windows-x86_64', 'macos-x86_64', 'macos-arm64'})
        steps = {s.get('name'): s for s in builds['steps']}
        for name in ('Configure Workshop', 'Build Workshop', 'Workshop tests',
                     'Package Workshop with Qt runtime', 'Workshop package startup smoke check'):
            self.assertNotIn('continue-on-error', steps[name])
            self.assertNotIn('if', steps[name])
        release = workflow['jobs']['release']
        self.assertEqual(release['needs'], 'build')
        self.assertIn('--with-workshop', next(s['run'] for s in release['steps']
                                            if s.get('name') == 'Assemble release downloads'))
        self.assertIn('IMPERA_WORKSHOP_DEPLOY_QT=ON', steps['Configure Workshop']['run'])


if __name__ == '__main__':
    unittest.main()
