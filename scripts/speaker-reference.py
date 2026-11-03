#!/usr/bin/env python3
"""Build a local DOSBox harness using an owner's original DOS speaker routines.

No original executable/data bytes are embedded in this script or committed output.
"""
import argparse
import math
from pathlib import Path
import struct


def build(game, output):
    exe = (game / 'ULTIMA.EXE').read_bytes()
    data_file = (game / 'DATA.OVL').read_bytes()
    header_size = struct.unpack_from('<H', exe, 8)[0] * 16
    code = exe[header_size:]
    data = data_file[16:]
    if (exe[:2] != b'MZ' or code[0x2192:0x2198] != bytes.fromhex('558bec56571e')
            or code[0x22c0:0x22c3] != bytes.fromhex('558bec')
            or data[0x545c:0x545e] != bytes.fromhex('6476')):
        raise ValueError('This harness supports the audited original DOS executable layout only')
    output.mkdir(parents=True, exist_ok=True)
    effects = [('TONE', 0x22c0, [1000, 200]),
               ('PULSE', 0x2192, [3116, 1, 2000, 20000, -10]),
               ('STEP', 0x223c, [1, 25, 1000]),
               ('SWEEP', 0x43ae, [1000, 200, 5, 300])]
    for name, address, arguments in effects:
        image = bytearray(0x30000)
        image[:len(code)] = code
        image[0xf640:0xf640 + len(data)] = data
        entry = bytearray(bytes.fromhex('b8640f8ed8'))  # relocated original DS

        def call(target):
            here = 0x9000 + len(entry)
            entry.extend(b'\xe8' + struct.pack('<H', (target - here - 3) & 65535))

        def wait():
            entry.extend(bytes.fromhex('b486b90700ba20a1cd15'))  # BIOS 500 ms

        call(0x11b4)  # original BIOS-tick CPU calibration
        entry.extend(bytes.fromhex('ba0060b90000b43ccd218bd8b90200ba5653b440cd21b43ecd21'))
        entry.extend(bytes.fromhex('c706cea90100'))  # enable original speaker flag
        wait()
        for argument in arguments:
            entry.extend(b'\xb8' + struct.pack('<H', argument & 65535) + b'\x50')
        call(address)
        wait()
        entry.extend(bytes.fromhex('b8004ccd21'))
        image[0x9000:0x9000 + len(entry)] = entry
        image[0xf640 + 0x6000:0xf640 + 0x600a] = b'CALIB.BIN\0'
        image = image[:0xf640 + len(data)]
        header = bytearray(exe[:header_size])
        relocations = struct.unpack_from('<H', header, 6)[0]
        table = struct.unpack_from('<H', header, 24)[0]
        if table + (relocations + 1) * 4 > header_size:
            raise ValueError('No room for harness relocation')
        struct.pack_into('<HH', header, table + relocations * 4, 0x9001, 0)
        struct.pack_into('<H', header, 6, relocations + 1)
        size = len(header) + len(image)
        struct.pack_into('<HH', header, 2, size % 512, math.ceil(size / 512))
        struct.pack_into('<HH', header, 10, math.ceil((0x30000 - len(image)) / 16), 0xffff)
        struct.pack_into('<HH', header, 14, 0x2000, 0xfffe)
        struct.pack_into('<HH', header, 20, 0x9000, 0)
        (output / (name + '.EXE')).write_bytes(header + image)
    captures = output / 'capture'
    captures.mkdir(exist_ok=True)
    config = f'''[cpu]
core=simple
cputype=386_slow
cycles=5000
[mixer]
rate=48000
[speaker]
pcspeaker=true
pcrate=48000
[dosbox]
captures={captures.resolve()}
[autoexec]
mount c "{output.resolve()}"
c:
echo Start WAV capture with Ctrl-F6, then press a key.
pause
TONE.EXE
PULSE.EXE
STEP.EXE
SWEEP.EXE
echo Stop WAV capture with Ctrl-F6. CALIB.BIN contains the measured delay calibration.
'''
    (output / 'speaker.conf').write_text(config, encoding='utf-8', newline='\n')
    print('Run DOSBox with -conf', output / 'speaker.conf')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    build(arguments.game, arguments.output)
