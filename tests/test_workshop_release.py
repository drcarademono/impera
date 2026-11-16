"""Workshop release assembly and the actual four-platform workflow contract."""
import contextlib
import io
import subprocess
from unittest import mock
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile
import tarfile
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
                    payload = folder / name
                    if platform.startswith('macos'):
                        stage = root / name.removesuffix('.tar.gz')
                        bundle = stage / (product + '.app') / 'Contents'
                        (bundle / 'MacOS').mkdir(parents=True)
                        (bundle / 'MacOS' / product).write_bytes(b'executable')
                        (stage / 'Licenses').mkdir()
                        (stage / 'Licenses/license.txt').write_text('license')
                        (stage / 'README.md').write_text(product)
                        with tarfile.open(payload, 'w:gz') as archive:
                            archive.add(stage, arcname=stage.name)
                    else:
                        payload.write_bytes(b'package')
                    digest = hashlib.sha256(payload.read_bytes()).hexdigest()
                    (folder / (name + '.sha256')).write_text(f'{digest}  {name}\n')
            engine = artifacts / 'Impera-windows-x86_64'
            for name in assembly.WINDOWS_FILES:
                path = engine / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'engine')
            editor = artifacts / 'Impera-Workshop-windows-x86_64'
            files = ['bin/ImperaWorkshop.exe', 'bin/Qt6Core.dll', 'bin/Qt6Gui.dll',
                     'bin/Qt6Widgets.dll', 'bin/qt.conf', 'plugins/platforms/qwindows.dll',
                     'plugins/platforms/qoffscreen.dll',
                     'README.md', 'Run Workshop.cmd', 'Licenses/Qt-runtime.txt',
                     'Licenses/Qt/LGPL-3.0-only.txt', 'bin/vcruntime140.dll']
            for name in files:
                path = editor / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'editor')
            output = root / 'dist'
            for _ in range(2):
                assembly.assemble(artifacts, 'vtest', output, with_workshop=True)
                self.assertEqual(len(list(output.iterdir())), 8)
                with tarfile.open(output / 'Impera-vtest-linux-x86_64.tar.gz') as archive:
                    names = archive.getnames()
                    self.assertTrue(any(n.endswith('/Mods') for n in names))
                    self.assertTrue(any(n.endswith('/Impera.AppImage') for n in names))
                    self.assertTrue(any(n.endswith('/ImperaWorkshop.AppImage') for n in names))
                    self.assertEqual(archive.getmember('Impera-vtest-linux-x86_64/Impera.AppImage').mode, 0o755)
                with zipfile.ZipFile(output / 'Impera-vtest-windows-x86_64.zip') as archive:
                    self.assertTrue(any(n.endswith('/ImperaWorkshop.exe') for n in archive.namelist()))
                    self.assertTrue(any(n.endswith('/Impera.exe') for n in archive.namelist()))
                    self.assertTrue(any(n.endswith('/Mods/') for n in archive.namelist()))
                    self.assertIn(b'Prefix=.', archive.read('Impera-vtest-windows-x86_64/qt.conf'))
                    self.assertFalse(any(n.endswith('.zip') for n in archive.namelist()))
                for platform in ('macos-x86_64', 'macos-arm64'):
                    with tarfile.open(output / f'Impera-vtest-{platform}.tar.gz') as archive:
                        names = archive.getnames()
                        self.assertTrue(any(n.endswith('/Impera.app') for n in names))
                        self.assertTrue(any(n.endswith('/ImperaWorkshop.app') for n in names))
                        self.assertTrue(any(n.endswith('/Mods') for n in names))
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

    def test_release_reuses_identical_verified_packages(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = root / 'artifacts'
            originals = {}
            for platform in (*assembly.PLATFORMS, 'windows-x86_64'):
                folder = artifacts / f'Build-{platform}'
                folder.mkdir(parents=True)
                suffix = '.zip' if platform.startswith('windows') else '.tar.gz'
                name = f'Impera-vtest-{platform}{suffix}'
                payload = f'verified combined {platform}'.encode()
                (folder / name).write_bytes(payload)
                digest = hashlib.sha256(payload).hexdigest()
                checksum = f'{digest}  {name}\n'.encode()
                (folder / (name + '.sha256')).write_bytes(checksum)
                originals.update({name: payload, name + '.sha256': checksum})
            output = root / 'release'
            for _ in range(2):
                assembly.assemble_verified(artifacts, 'vtest', output)
                self.assertEqual({p.name: p.read_bytes() for p in output.iterdir()}, originals)
            folder = artifacts / 'Build-macos-arm64'
            with tempfile.TemporaryDirectory() as missing:
                folder.rename(Path(missing) / folder.name)
                with self.assertRaisesRegex(ValueError, 'Missing or unexpected'):
                    assembly.assemble_verified(artifacts, 'vtest', root / 'missing')
                (Path(missing) / folder.name).rename(folder)
            extra = folder / 'ImperaWorkshop-vtest-macos-arm64.tar.gz'
            extra.write_bytes(b'separate product')
            with self.assertRaisesRegex(ValueError, 'Unexpected combined'):
                assembly.assemble_verified(artifacts, 'vtest', root / 'extra')
            extra.unlink()
            next(folder.glob('*.tar.gz')).write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                assembly.assemble_verified(artifacts, 'vtest', root / 'corrupt')
            self.assertFalse((root / 'corrupt').exists())

    def test_windows_help_check_redirects_output_and_suppresses_dialogs(self):
        workflow = yaml.safe_load((ROOT / '.github/workflows/release.yml').read_text())
        step = next(s for s in workflow['jobs']['build']['steps']
                    if s.get('name') == 'Native executable startup')
        self.assertEqual(step['env']['QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES'], '1')
        code = step['run'].replace('${{ runner.os }}', 'Windows').replace("os.name == 'nt'", 'True')
        def launched(command, **kwargs):
            self.assertTrue(kwargs['capture_output'])
            self.assertTrue(kwargs['check'])
            self.assertTrue(kwargs['text'])
            self.assertEqual(command[-1], '--help')
            return subprocess.CompletedProcess(command, 0, 'Usage: fixture\n', '')
        with mock.patch('subprocess.run', side_effect=launched) as run, contextlib.redirect_stdout(io.StringIO()):
            exec(compile(code, step['name'], 'exec'))
        self.assertEqual([Path(call.args[0][0]).name for call in run.call_args_list],
                         ['Impera.exe', 'ImperaWorkshop.exe'])

    def test_verified_testing_archives_survive_smoke_check(self):
        spec = importlib.util.spec_from_file_location('combined_check', ROOT / 'scripts/check-combined-release.py')
        check = importlib.util.module_from_spec(spec); spec.loader.exec_module(check)
        with tempfile.TemporaryDirectory() as temporary, contextlib.chdir(temporary):
            root = Path(temporary)
            for folder, product, output in [('dist', 'Impera', 'Usage: Impera'),
                                             ('workshop-dist', 'ImperaWorkshop',
                                              'PASS native dialogue packaged smoke test')]:
                path = root / folder / f'{product}-vtest-linux-x86_64.AppImage'
                path.parent.mkdir()
                path.write_text('#!/bin/sh\n[ -d "$U5D_RUNTIME_DIR" ] || exit 1\n'
                                'printf "%s\\n" "' + output + '"\n')
                path.chmod(0o755)
            destination = root / 'downloads'
            check.check('linux-x86_64', 'vtest', destination)
            archive = destination / 'Impera-vtest-linux-x86_64.tar.gz'
            checksum = destination / (archive.name + '.sha256')
            self.assertEqual(checksum.read_text().strip(),
                             f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}')
            with tarfile.open(archive) as stream:
                self.assertTrue(stream.getmember('Impera-vtest-linux-x86_64/Mods').isdir())
                self.assertTrue(stream.getmember('Impera-vtest-linux-x86_64/Impera.AppImage').mode & 0o111)
                self.assertTrue(stream.getmember('Impera-vtest-linux-x86_64/ImperaWorkshop.AppImage').mode & 0o111)
            editor = root / 'workshop-dist/ImperaWorkshop-vtest-linux-x86_64.AppImage'
            editor.write_text('#!/bin/sh\nexit 1\n')
            rejected = root / 'failed-downloads'
            with self.assertRaises(subprocess.CalledProcessError):
                check.check('linux-x86_64', 'vtest', rejected)
            self.assertFalse(rejected.exists(), 'Failed packages must not become testing downloads')

    def test_windows_package_requires_desktop_and_offscreen_runtimes(self):
        spec = importlib.util.spec_from_file_location('workshop_package', ROOT / 'scripts/package-workshop.py')
        package = importlib.util.module_from_spec(spec); spec.loader.exec_module(package)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / 'dist'; output.mkdir()
            required = ('bin/ImperaWorkshop.exe', 'bin/Qt6Core.dll', 'bin/Qt6Gui.dll',
                        'bin/Qt6Widgets.dll', 'plugins/platforms/qwindows.dll',
                        'plugins/platforms/qoffscreen.dll')
            omitted = None
            def install(command, **kwargs):
                stage = Path(command[command.index('--prefix') + 1])
                for name in required:
                    if name == omitted:
                        continue
                    path = stage / name; path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(b'release runtime')
                return subprocess.CompletedProcess(command, 0)
            with mock.patch.dict('os.environ', QT_ROOT_DIR=str(root / 'qt')), mock.patch('subprocess.run', side_effect=install):
                stage = package.package(root / 'build/workshop', 'windows-x86_64', 'vtest', output)
                self.assertTrue((stage / 'plugins/platforms/qoffscreen.dll').is_file())
                for omitted in required:
                    with self.subTest(missing=omitted), self.assertRaisesRegex(ValueError, Path(omitted).name):
                        package.package(root / 'build/workshop', 'windows-x86_64', 'vtest', output)

    def test_macos_offscreen_plugin_is_relocated_before_signing(self):
        spec = importlib.util.spec_from_file_location('workshop_package', ROOT / 'scripts/package-workshop.py')
        package = importlib.util.module_from_spec(spec); spec.loader.exec_module(package)
        for platform in ('macos-x86_64', 'macos-arm64'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                qt = root / 'qt'
                plugin = qt / 'plugins/platforms/libqoffscreen.dylib'
                plugin.parent.mkdir(parents=True)
                plugin.write_bytes(b'original SDK plugin')
                output = root / 'dist'; output.mkdir()
                calls = []
                def tool(command, **kwargs):
                    calls.append(command)
                    if command[0] == 'cmake':
                        stage = Path(command[command.index('--prefix') + 1])
                        app = stage / 'ImperaWorkshop.app'
                        (app / 'Contents/Frameworks/QtWidgets.framework').mkdir(parents=True)
                        (app / 'Contents/MacOS').mkdir()
                        (app / 'Contents/MacOS/ImperaWorkshop').write_bytes(b'program')
                    elif command[0].endswith('macdeployqt'):
                        deployed = Path(command[2].split('=', 1)[1])
                        self.assertEqual(deployed.read_bytes(), plugin.read_bytes())
                        self.assertIn('-no-plugins', command)
                        # Model relocation of the copied plugin, not modification of the SDK.
                        deployed.write_bytes(b'relocated bundle plugin')
                    elif command[0] == 'codesign':
                        app = Path(command[-1])
                        self.assertEqual((app / 'Contents/PlugIns/platforms/libqoffscreen.dylib').read_bytes(),
                                         b'relocated bundle plugin')
                    return subprocess.CompletedProcess(command, 0)
                with mock.patch.dict('os.environ', QT_ROOT_DIR=str(qt)), mock.patch('subprocess.run', side_effect=tool):
                    archive = package.package(root / 'build/workshop', platform, 'vtest', output)
                self.assertEqual(plugin.read_bytes(), b'original SDK plugin')
                self.assertEqual([Path(command[0]).name for command in calls],
                                 ['cmake', 'macdeployqt', 'codesign', 'codesign'])
                with tarfile.open(archive) as stream:
                    member = f'ImperaWorkshop-vtest-{platform}/ImperaWorkshop.app/Contents/PlugIns/platforms/libqoffscreen.dylib'
                    self.assertEqual(stream.extractfile(member).read(), b'relocated bundle plugin')
                plugin.unlink()
                with self.assertRaisesRegex(ValueError, 'offscreen macOS'):
                    package.deploy_mac_offscreen(root / 'app', qt)

    def test_workflow_preserves_platforms_and_requires_workshop_success(self):
        workflow = yaml.safe_load((ROOT / '.github/workflows/release.yml').read_text())
        builds = workflow['jobs']['build']
        self.assertEqual({p['platform'] for p in builds['strategy']['matrix']['include']},
                         {'linux-x86_64', 'windows-x86_64', 'macos-x86_64', 'macos-arm64'})
        self.assertEqual({p['platform']: p['qt_arch'] for p in builds['strategy']['matrix']['include']},
                         {'linux-x86_64': 'linux_gcc_64',
                          'windows-x86_64': 'win64_msvc2022_64',
                          'macos-x86_64': 'clang_64', 'macos-arm64': 'clang_64'})
        qt_install = next(s for s in builds['steps']
                          if s.get('uses', '').startswith('jurplel/install-qt-action@'))
        self.assertRegex(qt_install['uses'], r'@[0-9a-f]{40}$')
        self.assertEqual(qt_install['with']['version'], '6.8.3')
        self.assertNotIn('modules', qt_install['with'])
        steps = {s.get('name'): s for s in builds['steps']}
        # One root build includes both products; native validation runs on every event.
        self.assertIn('IMPERA_BUILD_WORKSHOP=ON', steps['Configure']['run'])
        self.assertIn('IMPERA_WORKSHOP_DEPLOY_QT=ON', steps['Configure']['run'])
        commands = [s.get('run', '') for s in builds['steps']]
        self.assertEqual(sum(command.startswith('cmake -S ') for command in commands), 1)
        self.assertEqual(sum(command.startswith('cmake --build ') for command in commands), 1)
        self.assertEqual(sum(command.startswith('ctest ') for command in commands), 1)
        for name in ('Configure', 'Build', 'Tests', 'Native executable startup'):
            self.assertNotIn('continue-on-error', steps[name])
            self.assertNotIn('if', steps[name])
        self.assertEqual(steps['Tests']['env']['QT_QPA_PLATFORM'], 'offscreen')
        package_steps = builds['steps'][builds['steps'].index(steps['Package']):]
        for step in package_steps:
            self.assertNotIn('continue-on-error', step)
            if step.get('uses', '').startswith('actions/upload-artifact'):
                continue
            self.assertNotIn('refs/tags/', step.get('if', ''))
        upload = steps['Upload verified combined package']
        self.assertEqual(upload['with']['path'], 'build-artifacts/*')
        self.assertEqual(upload['with']['name'], 'Build-${{ matrix.platform }}')
        self.assertNotIn('if', upload)
        self.assertEqual(sum(s.get('uses', '').startswith('actions/upload-artifact@')
                             for s in builds['steps']), 1)
        self.assertIn('--output build-artifacts', steps['Combined distribution startup smoke check']['run'])
        self.assertIn('github.sha', builds['env']['RELEASE_VERSION'])
        self.assertIn('github.ref_name', builds['env']['RELEASE_VERSION'])
        self.assertIn('--build release-build/workshop', steps['Package Workshop with Qt runtime']['run'])
        release = workflow['jobs']['release']
        self.assertEqual(release['needs'], 'build')
        self.assertEqual(release['if'], "startsWith(github.ref, 'refs/tags/v')")
        self.assertIn('--verified-combined', next(s['run'] for s in release['steps']
                                            if s.get('name') == 'Assemble release downloads'))
        download = next(s for s in release['steps'] if s.get('uses', '').startswith('actions/download-artifact@'))
        self.assertEqual(download['with']['pattern'], 'Build-*')
        events = workflow.get('on', workflow.get(True))
        self.assertEqual(events['push']['branches'], ['main'])
        self.assertEqual(events['push']['tags'], ['v*'])
        self.assertIn('workflow_dispatch', events)
        for event in ('push', 'pull_request'):
            self.assertIn('src/**', events[event]['paths'])
            self.assertIn('workshop/**', events[event]['paths'])
        self.assertEqual(workflow['permissions'], {'contents': 'read'})
        self.assertEqual(release['permissions'], {'contents': 'write'})
        self.assertIn('github.ref', workflow['concurrency']['group'])
        self.assertIn("!startsWith(github.ref, 'refs/tags/')", workflow['concurrency']['cancel-in-progress'])
        smoke = steps['Workshop package startup smoke check']['run']
        self.assertEqual(smoke.count("'--playtest-smoke-test'"), 3)
        self.assertIn('timeout=60', smoke)
        self.assertIn('PASS native dialogue packaged smoke test', smoke)



if __name__ == '__main__':
    unittest.main()
