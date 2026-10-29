"""Summarize native Workshop exports into reviewable architectural evidence."""
import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path

from PIL import Image, ImageDraw

from .art import DosArt, render, unbase64
from .model import CityMap
from .catalogue import load_catalogue


def survey(exports, output):
    paths = sorted(exports.glob('location-*.json'))
    if len(paths) != 32:
        raise ValueError('Export all 32 locations with the native --survey-game probe first')
    output.mkdir(parents=True, exist_ok=False)
    counts, facing, locations = Counter(), {hex(t): Counter() for t in range(0x90, 0x94)}, []
    sheet = Image.new('RGB', (1280, 720), '#202020')
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(paths):
        city = CityMap.load(path)
        local = Counter(t for f in city.data['map']['floors'] for row in f['tiles'] for t in row)
        counts.update(local)
        locations.append({'id': i+1, 'name': city.data['map']['name'],
                          'floors': len(city.data['map']['floors']),
                          'tileCounts': {str(k): v for k, v in sorted(local.items())}})
        for f in city.data['map']['floors']:
            for y, row in enumerate(f['tiles']):
                for x, tile in enumerate(row):
                    if tile in range(0x90, 0x94):
                        for dx, dy, name in ((0,-1,'N'),(1,0,'E'),(0,1,'S'),(-1,0,'W')):
                            if (0 <= x+dx < f['width'] and 0 <= y+dy < f['height'] and
                                    f['tiles'][y+dy][x+dx] in (0x94,0x95,0x96,0x9a,0x9b,0x9c,0xa5)):
                                facing[hex(tile)][name] += 1
        x, y = i%8*160, i//8*180
        sheet.paste(render(city).resize((160,160), Image.Resampling.NEAREST), (x,y))
        draw.text((x,y+161), f'{i+1} '+city.data['map']['name'], fill='white')
        if i+1 in (2, 9, 19):
            render(city).save(output/f'original-location-{i+1:02}.png')
    sheet.save(output/'original-towns.png')
    art = DosArt(CityMap.load(paths[0]).data)
    atlas = Image.new('RGB', (16*72, 32*90), '#202020')
    pen = ImageDraw.Draw(atlas)
    for tile in range(512):
        x, y = tile%16*72, tile//16*90
        atlas.paste(art.tile(tile).resize((64,64),Image.Resampling.NEAREST),(x,y))
        pen.text((x,y+66), f'{tile:03x}',fill='white')
    atlas.save(output/'all-512-dos-slots.png')
    catalogue = load_catalogue()
    evidence = {'method': 'Read-only native export of all 32 original U5 locations; all their floors counted.',
                'locations': locations, 'tileCounts': {str(k):v for k,v in sorted(counts.items())},
                'chairAdjacentTableDirections': facing,
                'catalogueDescriptions': catalogue['tiles'],
                'sourceHashes': catalogue['sources'],
                'dosResourceSha256': hashlib.sha256(unbase64(CityMap.load(paths[0]).data['artwork']['dos'])).hexdigest()}
    (output/'survey.json').write_text(json.dumps(evidence,indent=2)+'\n')
    return evidence


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exports',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    survey(args.exports,args.output)
