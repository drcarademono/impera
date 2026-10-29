"""Derive labels and walking bits from audited repository sources."""

import hashlib
import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def build_catalogue(repo=REPO):
    audit_path = repo / 'docs/workshop-tile-catalog-audit.md'
    defaults_path = repo / 'src/mod/type_defaults.h'
    audit, defaults = audit_path.read_text(), defaults_path.read_text()
    body = re.search(r'vanilla_D_54d4\[\]\s*=\s*\{(.*?)\}', defaults, re.S)[1]
    bits = bytes(int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', body))
    if len(bits) != 32:
        raise ValueError('Native collision table changed; audit catalogue generator')
    records, category = [], None
    for line in audit.splitlines():
        match = re.match(r'### (Ground|Buildings|Objects|Other) ', line)
        if match:
            category = match[1].lower()
        match = re.match(r'\| `([0-9a-f]{2})` \| (.*?) \|', line)
        if not match:
            continue
        tile, label = int(match[1], 16), match[2]
        roles = [word for word in ('floor', 'wall', 'door', 'window', 'roof', 'fence',
                                   'stair', 'ladder', 'bridge', 'chair', 'table', 'bed',
                                   'anvil', 'bellows', 'stove', 'logs', 'sign')
                 if word in label.lower()]
        roles += ['furniture'] if tile in range(0x90, 0xb0) and tile not in (0x97, 0x98, 0x99, 0xa0, 0xa1) else []
        records.append({'id': tile, 'hex': f'{tile:02x}', 'description': label,
                        'category': category, 'roles': roles,
                        # Party on foot (shape 0x1c), native movement profile zero.
                        'partyWalkable': not bool(bits[tile >> 3] & (0x80 >> (tile & 7))),
                        'doorInteraction': tile in (0x97, 0x98, 0xb8, 0xb9, 0xba, 0xbb),
                        'hazard': tile in (4, 0x8f, 0xbc)})
    if len(records) != 256 or len({r['id'] for r in records}) != 256:
        raise ValueError('Audited catalogue must cover exactly 256 terrain slots')
    return {'sources': {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in (audit_path, defaults_path)},
            'movementScope': 'Party on foot, shape 0x1c; src/mod/types.c TYPES_Walkable. '
                             'Static terrain only; overrides take precedence.',
            'tiles': sorted(records, key=lambda r: r['id'])}


def load_catalogue():
    return json.loads(Path(__file__).with_name('tile_catalogue.json').read_text())
