#!/usr/bin/env python3
import argparse, struct, sys

def parse_tail_hex(s: str) -> bytes:
    # Accept "90 9F C0" or "909FC0"
    s = s.replace(" ", "")
    if len(s) % 2 != 0:
        raise ValueError("Tail hex must have an even number of hex digits")
    try:
        return bytes.fromhex(s)
    except ValueError as e:
        raise ValueError(f"Invalid tail hex: {e}")

def read_header(blob: bytes):
    if len(blob) < 2:
        raise ValueError("File too small to be a TLK")
    count = struct.unpack_from("<H", blob, 0)[0]
    need = 2 + count * 4
    if len(blob) < need:
        raise ValueError("Truncated TLK header")
    headers = []
    p = 2
    for _ in range(count):
        npc_id, off = struct.unpack_from("<HH", blob, p)
        p += 4
        headers.append((npc_id, off))
    return count, headers

def split_segments(headers, blob: bytes):
    segs = []
    for i, (_npc_id, off) in enumerate(headers):
        start = off
        end = headers[i+1][1] if i+1 < len(headers) else len(blob)
        if start > end or end > len(blob):
            raise ValueError("Header offsets out of range / corrupt TLK")
        segs.append(blob[start:end])
    return segs

def add_tails_to_segments(segs, tail: bytes, force: bool):
    new_segs = []
    for seg in segs:
        # If the exact tail is already present at the end, keep as-is unless --force
        if not force and tail and seg.endswith(tail):
            new_segs.append(seg)
            continue

        # Ensure at least one 0x00 before the tail so the last entry is terminated.
        if len(seg) == 0 or seg[-1] != 0:
            seg = seg + b"\x00"
        seg = seg + tail
        new_segs.append(seg)
    return new_segs

def rebuild_tlk(count: int, headers, new_segs):
    # Recompute offsets and write a fresh TLK
    header_size = 2 + count * 4
    offsets = []
    cur_off = header_size
    for (npc_id, _), seg in zip(headers, new_segs):
        offsets.append((npc_id, cur_off))
        cur_off += len(seg)

    out = bytearray()
    out += struct.pack("<H", count)
    for npc_id, off in offsets:
        # TLK uses 16-bit offsets; warn if overflow
        if off > 0xFFFF:
            print(f"WARNING: offset {off} for NPC {npc_id} exceeds 16-bit; file may be invalid.", file=sys.stderr)
        out += struct.pack("<HH", npc_id, off)

    for seg in new_segs:
        out += seg
    return bytes(out)

def main():
    ap = argparse.ArgumentParser(description="Append per-NPC tail bytes to a TLK file (after the final 0x00 of each segment).")
    ap.add_argument("-i", "--input", required=True, help="Input TLK")
    ap.add_argument("-o", "--output", required=True, help="Output TLK")
    ap.add_argument("--tail", default="90 9F C0",
                    help='Tail bytes as hex (default: "90 9F C0") — accepts "90 9F C0" or "909FC0"')
    ap.add_argument("--force", action="store_true",
                    help="Append tail even if the exact tail is already present at the end of a segment")
    args = ap.parse_args()

    tail = parse_tail_hex(args.tail)
    with open(args.input, "rb") as f:
        blob = f.read()

    count, headers = read_header(blob)
    segs = split_segments(headers, blob)
    new_segs = add_tails_to_segments(segs, tail, args.force)
    out_blob = rebuild_tlk(count, headers, new_segs)

    with open(args.output, "wb") as f:
        f.write(out_blob)

    print(f"Done. Wrote {len(out_blob)} bytes to {args.output}")

if __name__ == "__main__":
    main()

