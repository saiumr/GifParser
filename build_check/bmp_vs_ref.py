"""Verify the BMP files written by parser.exe against the Python reference.

Reads a 24-bit BMP, undoes the bottom-up/BGR/padding rules, and compares each
frame with the reference compositor output.

usage: python bmp_vs_ref.py <gif> <bmp_prefix> <ref_dir> <w> <h> <frames>
"""
import os
import struct
import sys


def read_bmp(path):
    d = open(path, "rb").read()
    if d[0] != 0x42 or d[1] != 0x4D:
        raise ValueError("not a BMP")
    offset = struct.unpack_from("<I", d, 10)[0]
    w = struct.unpack_from("<i", d, 18)[0]
    h = struct.unpack_from("<i", d, 22)[0]
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 24:
        raise ValueError(f"expected 24bpp, got {bpp}")
    stride = ((w * 3 + 3) // 4) * 4
    rows = []
    for y in range(h):
        base = offset + (h - 1 - y) * stride
        row = []
        for x in range(w):
            b, g, r = d[base + x * 3 : base + x * 3 + 3]
            row.append((r, g, b))
        rows.append(row)
    return w, h, rows


def main():
    gif, prefix, refdir, w, h, frames = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
    bad = 0
    for i in range(frames):
        bmp = f"{prefix}{i}.bmp"
        if not os.path.exists(bmp):
            print(f"  frame {i}: missing {bmp}")
            bad += 1
            continue
        bw, bh, rows = read_bmp(bmp)
        if (bw, bh) != (w, h):
            print(f"  frame {i}: size {bw}x{bh} != {w}x{h}")
            bad += 1
            continue
        ref = open(os.path.join(refdir, f"f{i:04d}.rgb"), "rb").read()
        diff = 0
        for y in range(h):
            for x in range(w):
                if rows[y][x] != tuple(ref[(y * w + x) * 3 : (y * w + x) * 3 + 3]):
                    diff += 1
        if diff:
            print(f"  frame {i}: {diff}/{w*h} pixels differ")
            bad += 1
    print(f"{os.path.basename(gif)}: {frames - bad}/{frames} BMP frames match the reference")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
