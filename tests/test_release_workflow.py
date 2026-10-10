"""Exercise the actual release shell step against a stateful fake GitHub CLI."""
import datetime
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
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
    assert not state['exists'] and '--draft' in args
    assert '--verify-tag' in args or '--target' in args
    state.update(exists=True, draft=True, creates=state['creates']+1)
elif command == 'upload':
    assert state['exists'] and '--clobber' in args
    for asset in args[3:]:
        if asset == '--clobber': continue
        state['assets'][Path(asset).name] = Path(asset).read_text()
    state['uploads'] += 1
elif command == 'edit':
    assert state['exists'] and '--draft=false' in args and state['uploads'] > 0
    state['draft'] = False
elif command == 'delete-asset':
    del state['assets'][args[3]]
else:
    raise AssertionError(args)
path.write_text(json.dumps(state))
'''


class ReleaseWorkflowTest(unittest.TestCase):
    def test_detection_and_dependencies(self):
        jobs = WORKFLOW['jobs']
        self.assertEqual(jobs['build']['needs'], 'prepare')
        self.assertEqual(jobs['release']['needs'], ['prepare', 'build'])
        self.assertNotIn('always()', jobs['release']['if'])
        self.assertEqual(len(jobs['build']['strategy']['matrix']['include']), 4)
        self.assertFalse(WORKFLOW['concurrency']['cancel-in-progress'])
        script = jobs['prepare']['steps'][0]['run']
        sha = 'a' * 40
        latest = dict(tag_name='vold', draft=False, published_at='2026-01-01T00:00:00Z')
        cases = [
            ('workflow_dispatch', [], 0, '2026-07-01T09:23:00+00:00', True),
            ('workflow_dispatch', [[latest]], 0, '2026-07-01T09:23:00+00:00', False),
            ('workflow_dispatch', [[latest]], 3, '2026-07-01T09:23:00+00:00', True),
            ('schedule', [[latest]], 3, '2026-07-01T09:23:00+00:00', True),
            ('schedule', [[latest]], 3, '2026-07-01T10:23:00+00:00', False),
            ('schedule', [[latest]], 3, '2026-01-01T09:23:00+00:00', False),
            ('schedule', [[latest]], 3, '2026-01-01T10:23:00+00:00', True),
            ('schedule', [[latest]], 3, '2026-03-08T09:23:00+00:00', False),
            ('schedule', [[latest]], 3, '2026-03-08T10:23:00+00:00', True),
            ('workflow_dispatch', [[latest], [dict(latest, draft=True, published_at='2026-12-01T00:00:00Z')]], 0, '2026-07-01T09:23:00+00:00', False),
            ('push', [], 0, '2026-07-01T09:23:00+00:00', True),
        ]
        for event, pages, ahead, instant, expected in cases:
            with self.subTest(event=event, ahead=ahead, instant=instant), tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / 'outputs'
                def api(args, **kwargs):
                    path = args[2]
                    if '/compare/' in path: result = dict(ahead_by=ahead)
                    elif '/commits/' in path: result = dict(sha=sha)
                    elif '/releases?' in path: result = pages
                    else: result = dict(default_branch='main')
                    return json.dumps(result)
                class Clock(datetime.datetime):
                    @classmethod
                    def now(cls, zone):
                        return datetime.datetime.fromisoformat(instant).astimezone(zone)
                env = dict(GITHUB_EVENT_NAME=event, GITHUB_OUTPUT=str(output),
                           GH_REPO='owner/impera', GITHUB_SHA=sha, GITHUB_REF_NAME='vtest',
                           SCHEDULE=f'23 {datetime.datetime.fromisoformat(instant).hour} * * *')
                with patch.dict(os.environ, env), patch('subprocess.check_output', side_effect=api), patch('datetime.datetime', Clock):
                    exec(compile(script, '<workflow detection>', 'exec'), {})
                values = dict(line.split('=', 1) for line in output.read_text().splitlines())
                self.assertEqual(values['build'], str(expected).lower())
                if expected:
                    self.assertEqual(values['sha'], sha)
                    if event != 'push': self.assertTrue(values['version'].endswith('-'+sha[:12]))

    def test_new_draft_and_published_release_reruns(self):
        for exists, draft, automatic in [(False, False, False), (True, True, False), (True, False, False), (False, False, True), (True, True, True), (True, False, True)]:
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
                           GH_TEST_STATE=str(state), RELEASE_TAG='vtest', RELEASE_SHA='a'*40,
                           AUTOMATIC_RELEASE=str(automatic).lower())
                subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', SCRIPT], cwd=directory,
                               env=env, check=True)
                package.write_text('replacement build')
                subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', SCRIPT], cwd=directory,
                               env=env, check=True)
                result = json.loads(state.read_text())
                self.assertEqual(result['uploads'], 2)
                self.assertEqual(result['creates'], int(not exists))
                self.assertEqual(result['draft'], False if automatic else draft if exists else True)
                self.assertEqual(result['assets'], {name: 'replacement build'})


if __name__ == '__main__':
    unittest.main()
