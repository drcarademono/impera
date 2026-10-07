#!/usr/bin/env python3
"""Generate native icon derivatives of packaging/impera.svg (CairoSVG + Pillow).

--check uses only the Python standard library and checks source/derivative hashes.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'packaging/impera.svg'
MANIFEST = ROOT / 'packaging/icon-manifest.json'
OUTPUTS = ['packaging/impera.png', 'packaging/impera.ico', 'packaging/impera.icns',
           'src/common/icon_pixels.h']


def hashes():
    # Git may check out text as CRLF on Windows; artwork/pixel content is identical.
    return {name: hashlib.sha256(
                (ROOT / name).read_text(encoding='utf-8').encode('utf-8')
                if name.endswith(('.svg', '.h')) else (ROOT / name).read_bytes()).hexdigest()
            for name in ['packaging/impera.svg', *OUTPUTS]}


def generate():
    import cairosvg
    from PIL import Image

    def render(size):
        return Image.open(io.BytesIO(cairosvg.svg2png(url=str(SOURCE),
                          output_width=size, output_height=size))).convert('RGBA')

    render(256).save(ROOT / OUTPUTS[0])
    render(256).save(ROOT / OUTPUTS[1], sizes=[(n, n) for n in [16, 32, 48, 64, 128, 256]])
    render(1024).save(ROOT / OUTPUTS[2])
    pixels = render(32).tobytes()
    lines = [', '.join(str(v) for v in pixels[i:i+24]) + ','
             for i in range(0, len(pixels), 24)]
    (ROOT / OUTPUTS[3]).write_text(
        '/* Generated from packaging/impera.svg by scripts/generate-icons.py. */\n'
        'static const unsigned char impera_icon_rgba[32 * 32 * 4] = {\n' +
        '\n'.join(lines) + '\n};\n')
    MANIFEST.write_text(json.dumps(hashes(), indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.check:
        if json.loads(MANIFEST.read_text()) != hashes():
            raise SystemExit('Icon artwork/derivatives changed; run scripts/generate-icons.py')
        print('Canonical icon source and all generated derivatives match.')
    else:
        generate()
