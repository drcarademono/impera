#!/usr/bin/env python3
"""Export Ultima V EGA/CGA graphics and monochrome fonts; Python stdlib only."""
import argparse
import json
from pathlib import Path
import struct
import zlib

EGA = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),
       (85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
CGA = [EGA[i] for i in (0,11,13,15)]
MONO = [EGA[0], EGA[15]]


def decompress(data):
    expected, = struct.unpack_from('<I', data)
    if not 0 < expected <= 16*1024*1024:
        raise ValueError('invalid compressed resource length')
    bits = int.from_bytes(data[4:], 'little')
    position = 0
    table = [bytes([i]) for i in range(256)] + [None, None]
    size, previous, output = 9, None, bytearray()
    while len(output) < expected:
        if position + size > (len(data)-4)*8:
            raise ValueError('truncated LZW stream')
        code = (bits >> position) & ((1 << size)-1)
        position += size
        if code == 256:
            table = table[:256] + [None, None]
            size, previous = 9, None
            continue
        if code == 257:
            break
        if code < len(table) and table[code] is not None:
            entry = table[code]
        elif code == len(table) and previous is not None:
            entry = previous + previous[:1]
        else:
            raise ValueError('invalid LZW code')
        output.extend(entry)
        if previous is not None and len(table) < 4096:
            table.append(previous + entry[:1])
            if len(table) == 1 << size and size < 12:
                size += 1
        previous = entry
    if len(output) < expected:
        raise ValueError('incomplete resource')
    return bytes(output[:expected])


def write_png(path, width, height, rgba, overwrite=False):
    if path.exists() and not overwrite:
        raise FileExistsError(f'{path} already exists; choose a new output folder or use --overwrite')
    def chunk(kind, data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    scan = b''.join(b'\0'+rgba[y*width*4:(y+1)*width*4] for y in range(height))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+
                     chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))


def pixels(data, width, height, stride, bpp, palette):
    if width <= 0 or height <= 0 or len(data) < stride*height:
        raise ValueError('invalid image dimensions or truncated pixels')
    out = bytearray()
    for y in range(height):
        for x in range(width):
            shift = 8-bpp-(x*bpp%8)
            color = (data[y*stride+x*bpp//8] >> shift)&((1<<bpp)-1)
            out.extend((*palette[color],255))
    return out


def extract(data_dir, output_dir, overwrite=False):
    output = Path(output_dir)
    entries = []
    for source in sorted(Path(data_dir).iterdir(), key=lambda p:p.name.lower()):
        ext = source.suffix.lower()
        if ext not in ('.16','.4','.bit','.ch','.hcs'):
            continue
        raw = source.read_bytes()
        resource = source.name.lower().replace('.', '-')
        decoded = raw if ext in ('.ch','.hcs') or source.name.lower()=='wd.bit' else decompress(raw)
        images = []
        if source.stem.lower() == 'tiles':
            bpp, palette = (4,EGA) if ext=='.16' else (2,CGA)
            stride, tile_size = 16*bpp//8, 16*16*bpp//8
            if len(decoded)%tile_size:
                raise ValueError(f'{source}: incomplete tileset')
            for i in range(len(decoded)//tile_size):
                name = f"{'terrain' if i<256 else 'sprites'}/tile-{i:03x}.png"
                images.append((name,16,16,pixels(decoded[i*tile_size:(i+1)*tile_size],16,16,stride,bpp,palette)))
        elif ext in ('.ch','.hcs'):
            w,h = (8,8) if ext=='.ch' else (16,12)
            size = w*h//8
            for i in range(len(decoded)//size):
                images.append((f'glyph-{i:03x}.png',w,h,pixels(decoded[i*size:(i+1)*size],w,h,w//8,1,MONO)))
        else:
            count, = struct.unpack_from('<H',decoded)
            offset_count = count if ext=='.bit' else count*2
            offsets = struct.unpack_from(f'<{offset_count}H',decoded,2)
            bpp, palette = (1,MONO) if ext=='.bit' else (4,EGA) if ext=='.16' else (2,CGA)
            step = 1 if ext=='.bit' else 2
            for i in range(0,offset_count,step):
                off=offsets[i]
                if not off:
                    continue
                w,h=struct.unpack_from('<HH',decoded,off)
                stride=(w+3)//4 if bpp==2 else (w+7)//8*bpp
                rgba=pixels(decoded[off+4:],w,h,stride,bpp,palette)
                if step==2 and i+1<offset_count and offsets[i+1]:
                    mo=offsets[i+1]; mw,mh=struct.unpack_from('<HH',decoded,mo)
                    if (mw,mh)!=(w,h):
                        raise ValueError(f'{source}: mask dimensions differ')
                    mask=pixels(decoded[mo+4:],w,h,(w+7)//8,1,MONO)
                    for j in range(w*h):
                        rgba[j*4+3]=mask[j*4] # set mask bits draw pixels, as in game
                images.append((f'image-{i//step:03d}.png',w,h,rgba))
        for name,w,h,rgba in images:
            target=output/resource/name
            write_png(target,w,h,rgba,overwrite)
            entries.append({'file':str(target.relative_to(output)), 'source':source.name,'width':w,'height':h})
        if images and (ext in ('.ch','.hcs') or source.stem.lower()=='tiles'):
            columns=16; w,h=images[0][1:3]; rows=(len(images)+columns-1)//columns
            sheet=bytearray(columns*w*rows*h*4)
            for i,(_,_,_,rgba) in enumerate(images):
                for y in range(h):
                    dst=((i//columns*h+y)*columns*w+(i%columns*w))*4
                    sheet[dst:dst+w*4]=rgba[y*w*4:(y+1)*w*4]
            write_png(output/resource/'contact-sheet.png',columns*w,rows*h,sheet,overwrite)
    if not entries:
        raise ValueError('no supported graphics found')
    manifest=output/'manifest.json'
    if manifest.exists() and not overwrite:
        raise FileExistsError(f'{manifest} already exists')
    manifest.write_text(json.dumps(entries,indent=2)+'\n')
    return len(entries)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data-dir',default='Ultima 5')
    parser.add_argument('--output-dir',default='extracted-graphics')
    parser.add_argument('--overwrite',action='store_true',help='replace existing exported PNGs')
    args=parser.parse_args()
    try:
        print(f'Exported {extract(args.data_dir,args.output_dir,args.overwrite)} graphics to {args.output_dir}')
    except (OSError,ValueError,struct.error) as error:
        parser.exit(1,f'{error}\n')
