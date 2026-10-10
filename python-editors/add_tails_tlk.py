#!/usr/bin/env python3
import argparse, struct, sys
from u5_formats import tlk_header, build_tlk

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
    headers = tlk_header(blob)
    return len(headers), headers

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
        if not force and tail and seg.rstrip(b"\x00").endswith(tail):
            new_segs.append(seg)
            continue

        # Ensure at least one 0x00 before the tail so the last entry is terminated.
        if len(seg) == 0 or seg[-1] != 0:
            seg = seg + b"\x00"
        seg = seg + tail
        new_segs.append(seg)
    return new_segs

def rebuild_tlk(count: int, headers, new_segs):
    if count != len(headers):
        raise ValueError("TLK count mismatch")
    return build_tlk(headers, new_segs)

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

