"""Optional real-importer integration and independent pixel/movement parity."""

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess

from PIL import Image, ImageChops

from .catalogue import load_catalogue
from .model import CityMap, MapError, _metadata


def verify(study, native):
    """Verify already captured probe outputs; deliberately no assertions to disable."""
    source_native = CityMap.load(native / 'original/roundtrip.json')
    walking = json.loads((native / 'original/native-walking.json').read_text())
    if walking != [t['partyWalkable'] for t in load_catalogue()['tiles']]:
        raise MapError('Catalogue differs from native TYPES_Walkable(0x1c)')
    results = []
    manifest = json.loads((study / 'validation.json').read_text())
    paths = sorted(study.glob('[0-9][0-9]-*.json'))
    for path in sorted(study.glob('[0-9][0-9]-*.json')):
        city = CityMap.load(path)
        imported = CityMap.load(native / path.stem / 'roundtrip.json')
        expected = _metadata(source_native.data)
        if path == paths[-1]:
            for change in manifest.get('authorizedMetadataChanges', []):
                parent = expected
                for key in change['path'][:-1]:
                    parent = parent[key]
                parent[change['path'][-1]] = copy.deepcopy(change['value'])
        if _metadata(imported.data) != expected:
            raise MapError(f'{path.name}: imported metadata differs from imported source')
        for before, after in zip(city.data['map']['floors'], imported.data['map']['floors']):
            if any(before[k] != after[k] for k in ('floor', 'width', 'height', 'tiles')):
                raise MapError(f'{path.name}: native import changed floor terrain/dimensions')
        for floor in city.data['map']['floors']:
            z = floor['floor']
            label = 'ground' if z == 0 else 'upper'
            a = Image.open(study / f'{path.stem}-{label}.png').convert('RGB')
            b = Image.open(native / path.stem / f'floor{z}.png').convert('RGB')
            if a.size != b.size or ImageChops.difference(a, b).getbbox() is not None:
                raise MapError(f'{path.name}: DOS preview differs from native C++ renderer')
        results.append({'stage': path.stem, 'jsonSha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                        'nativeImport': 'passed', 'nativeProjectValidation': 'passed',
                        'reexportTerrainParity': 'passed',
                        'reexportMetadataParityWithSourceImport': 'passed (explicit authorized gate changes accounted for)',
                        'pixelParity': 'passed (both floors)'})
    if not results:
        raise MapError('No experimental stages found')
    arrival_path = native / paths[-1].stem / 'arrival.json'
    arrival = json.loads(arrival_path.read_text()) if arrival_path.exists() else None
    if manifest.get('arrival') and (not arrival or len(arrival) != 1 or
            [arrival[0]['arrivalX'], arrival[0]['arrivalY']] != manifest['arrival']['cell']):
        raise MapError('Native companion package has incorrect portal arrival')
    package = native / paths[-1].stem / 'Minoc-experiment.imperamod'
    return {'method': 'Real Workshop importMapJson, Project::validate, exportMapJson, U5::readGraphics; '
                      'private in-memory project, not GUI interaction or game play-testing.',
            'nativeMovementParity': 'passed (all 256 terrain tiles, party shape 0x1c)',
            'arrival': arrival, 'globalNewGameStateChanged': False,
            'companionModSha256': hashlib.sha256(package.read_bytes()).hexdigest() if package.exists() else None,
            'packageResourceRoundtrip': 'passed' if package.exists() else 'not run',
            'gamePlayTested': False, 'stages': results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--study', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    inputs = [('original', args.source)] + [(p.stem, p) for p in sorted(args.study.glob('[0-9][0-9]-*.json'))]
    manifest = json.loads((args.study / 'validation.json').read_text())
    for label, source in inputs:
        command = [str(args.probe.resolve()), str(args.game.resolve()), str(source.resolve()),
                   str((args.output / label).resolve())]
        if label == inputs[-1][0] and manifest.get('arrival'):
            command += ['--arrival', *map(str, manifest['arrival']['cell'])]
        result = subprocess.run(command, env=env,
                                text=True, capture_output=True, timeout=120, check=True)
        (args.output / f'{label}.log').write_text(result.stdout + result.stderr)
    report = verify(args.study, args.output)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
