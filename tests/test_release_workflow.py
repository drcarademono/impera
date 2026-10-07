"""Exercise the actual release shell step against a stateful fake GitHub CLI."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = yaml.safe_load((ROOT / '.github/workflows/release.yml').read_text())
SCRIPT = next(step['run'] for step in WORKFLOW['jobs']['release']['steps']
              if step.get('name') == 'Create or reuse release and replace assets')

FAKE_GH = '''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
path = Path(os.environ['GH_TEST_STATE'])
state = json.loads(path.read_text())
args = sys.argv[1:]
assert args[0] == 'release'
command = args[1]
if command == 'view':
    if state['exists']:
        if '--json' in args:
            print('\\n'.join(state['assets']))
    else:
        sys.exit(1)
elif command == 'create':
    assert not state['exists'] and '--draft' in args and '--verify-tag' in args
    state.update(exists=True, draft=True, creates=state['creates']+1)
elif command == 'upload':
    assert state['exists'] and '--clobber' in args
    for asset in args[3:]:
        if asset == '--clobber': continue
        state['assets'][Path(asset).name] = Path(asset).read_text()
    state['uploads'] += 1
elif command == 'delete-asset':
    del state['assets'][args[3]]
else:
    raise AssertionError(args)
path.write_text(json.dumps(state))
'''


class ReleaseWorkflowTest(unittest.TestCase):
    def test_new_draft_and_published_release_reruns(self):
        for exists, draft in [(False, False), (True, True), (True, False)]:
            with self.subTest(exists=exists, draft=draft), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                cli = directory / 'gh';cli.write_text(FAKE_GH);cli.chmod(0o755)
                state = directory / 'state.json'
                name = 'Impera-vtest-linux-x86_64.AppImage'
                state.write_text(json.dumps(dict(exists=exists, draft=draft, creates=0,
                    uploads=0, assets={name: 'old'} if exists else {})))
                dist = directory / 'dist';dist.mkdir()
                package = dist / name;package.write_text('first build')
                env = dict(os.environ, PATH=str(directory)+os.pathsep+os.environ['PATH'],
                           GH_TEST_STATE=str(state), RELEASE_TAG='vtest')
                subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', SCRIPT], cwd=directory,
                               env=env, check=True)
                package.write_text('replacement build')
                subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', SCRIPT], cwd=directory,
                               env=env, check=True)
                result = json.loads(state.read_text())
                self.assertEqual(result['uploads'], 2)
                self.assertEqual(result['creates'], int(not exists))
                self.assertEqual(result['draft'], draft if exists else True)
                self.assertEqual(result['assets'], {name: 'replacement build'})


if __name__ == '__main__':
    unittest.main()
