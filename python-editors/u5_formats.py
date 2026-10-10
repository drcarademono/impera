"""Shared DOS resource codecs, matched to src/common/lzw.c and src/talk.c."""
import struct

MAX_RESOURCE = 8 * 1024 * 1024
TAIL = b"\x90\x9f\xc0"

def lzw_decode(comp, expected_len):
    if not 0 <= expected_len <= MAX_RESOURCE:
        raise ValueError("Invalid uncompressed resource size")
    table = [bytes([i]) for i in range(256)] + [b"", b""]
    width, next_code, prev, pos = 9, 258, None, 0
    output = bytearray()
    while len(output) < expected_len:
        if pos + width > len(comp) * 8:
            raise ValueError("Truncated LZW stream")
        offset, shift = divmod(pos, 8)
        code = (int.from_bytes(comp[offset:offset+3], "little") >> shift) & ((1 << width)-1)
        pos += width
        if code == 256:
            table = [bytes([i]) for i in range(256)] + [b"", b""]
            width, next_code, prev = 9, 258, None
            continue
        if code == 257:
            raise ValueError("LZW ended before the declared resource size")
        if code < len(table) and table[code]:
            entry = table[code]
        elif prev is not None and code == next_code:
            entry = prev + prev[:1]
        else:
            raise ValueError("Invalid LZW dictionary code")
        # The engine stops at the declared length, even inside the last code.
        output.extend(entry[:expected_len-len(output)])
        if prev is not None and next_code < 4096:
            table.append(prev + entry[:1])
            next_code += 1
            if next_code == (1 << width) and width < 12:
                width += 1
        prev = entry
    return bytes(output)

def lzw_encode(raw):
    # Track the decoder's dictionary separately: the encoder is one entry ahead.
    output, bits, nbits = bytearray(), 0, 0
    width, decoder_next, decoder_prev = 9, 258, False
    def emit(code):
        nonlocal bits, nbits, width, decoder_next, decoder_prev
        bits |= code << nbits
        nbits += width
        while nbits >= 8:
            output.append(bits & 255)
            bits >>= 8
            nbits -= 8
        if code == 256:
            width, decoder_next, decoder_prev = 9, 258, False
        elif code != 257:
            if decoder_prev and decoder_next < 4096:
                decoder_next += 1
                if decoder_next == 1 << width and width < 12:
                    width += 1
            decoder_prev = True
    table, next_code, word = {bytes([i]): i for i in range(256)}, 258, b""
    emit(256)
    for value in raw:
        char = bytes([value])
        combined = word + char
        if combined in table:
            word = combined
            continue
        emit(table[word])
        if next_code < 4096:
            table[combined] = next_code
            next_code += 1
        else:
            emit(256)
            table, next_code = {bytes([i]): i for i in range(256)}, 258
        word = char
    if word:
        emit(table[word])
    emit(257)
    if nbits:
        output.append(bits & 255)
    return bytes(output)

def read_tiles_blob(path):
    raw = path.read_bytes()
    if len(raw) == 65536:
        return raw
    if len(raw) < 4 or struct.unpack_from("<I", raw)[0] != 65536:
        raise ValueError("TILES.16 must declare 65536 uncompressed bytes")
    return lzw_decode(raw[4:], 65536)

def tlk_header(blob):
    if len(blob) < 2:
        raise ValueError("TLK header truncated")
    count = struct.unpack_from("<H", blob)[0]
    if count > 127:
        raise ValueError("TLK header exceeds the engine 512-byte buffer")
    size = 2 + count*4
    if len(blob) < size:
        raise ValueError("TLK header truncated")
    headers = [struct.unpack_from("<HH", blob, 2+i*4) for i in range(count)]
    previous = size
    ids = set()
    for npc, offset in headers:
        if not previous <= offset < len(blob) or npc in ids:
            raise ValueError("Invalid TLK offsets or duplicate NPC ID")
        previous = offset + 1
        ids.add(npc)
    return headers

def tlk_segments(headers, blob):
    return [blob[offset:headers[i+1][1] if i+1<len(headers) else len(blob)]
            for i, (_, offset) in enumerate(headers)]

def build_tlk(headers, segments):
    if len(headers) != len(segments) or len(headers) > 127:
        raise ValueError("TLK header/segment count mismatch")
    offset = 2 + 4*len(headers)
    output = bytearray(struct.pack("<H", len(headers)))
    ids = set()
    for (npc, _), segment in zip(headers, segments):
        if not segment or len(segment) > 1024 or npc in ids:
            raise ValueError("Invalid conversation size or duplicate NPC ID")
        ids.add(npc)
        if offset > 32767 or not 0 <= npc <= 65535:
            raise ValueError("TLK offset exceeds signed 16 bits or NPC ID exceeds 16 bits")
        output.extend(struct.pack("<HH", npc, offset))
        offset += len(segment)
    return bytes(output) + b"".join(segments)


def write_map_pair(map_path, map_bytes, index_path, index_bytes):
    """Stage paired BRIT/DATA updates; restore originals on an I/O failure."""
    import os
    import tempfile
    from pathlib import Path
    files = [(Path(map_path), map_bytes), (Path(index_path), index_bytes)]
    staged, originals, replaced = [], {}, []
    try:
        for path, data in files:
            originals[path] = path.read_bytes() if path.exists() else None
            with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as out:
                staged.append(Path(out.name))
                out.write(data)
                out.flush()
                os.fsync(out.fileno())
        for (path, _), temp in zip(files, staged):
            temp.replace(path)
            replaced.append(path)
    except OSError:
        for path in reversed(replaced):
            if originals[path] is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(originals[path])
        raise
    finally:
        for temp in staged:
            temp.unlink(missing_ok=True)
