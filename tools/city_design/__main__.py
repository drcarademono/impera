"""Run from the repository root: python -m tools.city_design --help."""

import argparse
import json
import sys
from pathlib import Path

from .art import compare, render
from .layout import analyze
from .model import CityMap, MapError
from . import patterns


def _png(image, path):
    # PNGs, like maps, never overwrite an existing file, including a symlink.
    with Path(path).open('xb') as output:
        image.save(output, format='PNG')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for command in ('inspect', 'validate', 'render', 'resize', 'paint', 'building', 'compare', 'analyze'):
        p = commands.add_parser(command)
        p.add_argument('input', type=Path)
        if command in ('render', 'resize', 'paint', 'building', 'compare'):
            p.add_argument('output', type=Path)
        if command in ('render', 'resize', 'paint', 'building', 'compare', 'analyze'):
            p.add_argument('--floor', type=int, default=0)
        if command in ('render', 'compare'):
            p.add_argument('--scale', type=int, default=1)
        if command == 'render':
            p.add_argument('--region', type=int, nargs=4, metavar=('X', 'Y', 'W', 'H'))
        if command == 'resize':
            p.add_argument('--size', type=int, nargs=2, required=True)
            p.add_argument('--fill', type=int, default=5)
            p.add_argument('--all-floors', action='store_true')
        if command in ('paint', 'building', 'validate'):
            p.add_argument('--baseline', type=Path, required=command in ('paint', 'building'))
            p.add_argument('--authorization', type=Path,
                           help='Explicit reviewed exception manifest, with --baseline')
        if command == 'paint':
            p.add_argument('--region', type=int, nargs=4, required=True)
            p.add_argument('--tile', type=int, required=True)
        if command == 'building':
            p.add_argument('--at', type=int, nargs=2, required=True)
            p.add_argument('--size', type=int, nargs=2, required=True)
            p.add_argument('--pattern', choices=('house', 'shop', 'workshop', 'inn'), required=True)
        if command == 'compare':
            p.add_argument('--after', type=Path, required=True)
        if command == 'analyze':
            p.add_argument('--start', type=int, nargs=2, required=True)
            p.add_argument('--target', type=int, nargs=2, action='append', default=[])
    args = parser.parse_args(argv)
    try:
        city = CityMap.load(args.input)
        if getattr(args, 'baseline', None):
            city.original = CityMap.load(args.baseline).data
        city.protect_original()
        if getattr(args, 'authorization', None):
            if not args.baseline:
                raise MapError('--authorization requires --baseline')
            import hashlib
            manifest = json.loads(args.authorization.read_text())
            if hashlib.sha256(args.baseline.read_bytes()).hexdigest() != manifest['sourceSha256']:
                raise MapError('Authorization manifest does not match the baseline')
            for exception in manifest.get('authorizedTerrainChanges', []):
                city.authorize_terrain(exception['floor'], exception['cells'], exception['reason'])
            for exception in manifest.get('authorizedMetadataChanges', []):
                city.authorize_metadata(exception['path'], exception['value'], exception['reason'])
        command = args.command
        if command == 'inspect':
            print(json.dumps(city.summary(), indent=2))
        elif command == 'validate':
            errors = city.preservation()
            if errors:
                raise MapError('; '.join(errors))
            print('Valid impera-map structure; preservation contract passed (including explicit exceptions). '
                  'Use the native importer probe; this is not a play-test.')
        elif command == 'render':
            _png(render(city, args.floor, args.region, args.scale), args.output)
        elif command == 'compare':
            _png(compare(city, CityMap.load(args.after), args.floor, args.scale), args.output)
        elif command == 'analyze':
            report = analyze(city, args.floor, args.start,
                             [(f'target-{i}', *xy) for i, xy in enumerate(args.target)])
            print(json.dumps(report, indent=2))
            return bool(report['errors'])
        else:
            if command == 'resize':
                floors = [f['floor'] for f in city.data['map']['floors']] if args.all_floors else [args.floor]
                for floor in floors:
                    city.resize(floor, *args.size, args.fill)
            elif command == 'paint':
                city.rectangle(args.floor, args.region, args.tile)
            elif command == 'building':
                city.stamp(args.floor, *args.at, getattr(patterns, args.pattern)(*args.size))
            city.save(args.output)
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(f'city-design: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
