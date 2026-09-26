"""A/B compare frame dumps produced by dump_before.exe and dump_after.exe.

usage: python cmp_dumps.py <baseline_dir> <after_dir> <name...>
Prints per-file metadata equality and the number of differing frames/pixels.
"""
import hashlib
import os
import sys


def digests(d, w, h):
    out = {}
    stride = w * h * 3
    for fn in sorted(os.listdir(d)):
        if not fn.endswith(".rgb"):
            continue
        b = open(os.path.join(d, fn), "rb").read()
        if len(b) != stride:
            out[fn] = f"BAD_SIZE({len(b)})"
            continue
        out[fn] = hashlib.sha256(b).hexdigest()[:16]
    return out


def read_meta(path):
    meta = {}
    for line in open(path):
        if line.startswith("w="):
            parts = dict(p.split("=") for p in line.strip().split())
            meta.update(parts)
    return meta


def main():
    base_root, after_root = sys.argv[1], sys.argv[2]
    names = sys.argv[3:]
    for name in names:
        bdir = os.path.join(base_root, name)
        adir = os.path.join(after_root, name)
        if not os.path.isdir(bdir) or not os.path.isdir(adir):
            print(f"{name:26} SKIP (missing dump)")
            continue
        bmeta = read_meta(os.path.join(bdir, "meta.txt"))
        ameta = read_meta(os.path.join(adir, "meta.txt"))
        if bmeta != ameta:
            print(f"{name:26} META DIFF: baseline={bmeta} after={ameta}")
            continue
        w, h = int(bmeta["w"]), int(bmeta["h"])
        b = digests(bdir, w, h)
        a = digests(adir, w, h)
        diffs = [k for k in sorted(set(b) | set(a)) if b.get(k) != a.get(k)]
        if not diffs:
            print(f"{name:26} IDENTICAL  ({len(b)} frames, {w}x{h})")
        else:
            print(f"{name:26} {len(diffs)}/{len(b)} frames differ: {diffs[:6]}")


if __name__ == "__main__":
    main()
