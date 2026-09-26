"""Reproduce, from the file, every concrete number used in docs/Concepts.zh-CN.md.

This is the "show your work" companion to the illustrated primer: it re-derives
the palettes, the decoded frame indices and the composited output straight from
the bytes, so the diagrams in the document can be checked instead of trusted.

usage: python build_check/concepts_probe.py [gif ...]
default: build_check/cases/local_palette_probe.gif
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzwref import lzw_decode  # noqa: E402
from ref_composite import composite, parse  # noqa: E402


def show_palette(name, pal, indent="  "):
    if pal is None:
        print(f"{indent}{name}: <none>")
        return
    entries = ", ".join("%d=(%d,%d,%d)" % (k, c[0], c[1], c[2])
                        for k, c in enumerate(pal))
    print(f"{indent}{name} ({len(pal)}): {entries}")


def probe(path):
    d = open(path, "rb").read()
    w, h, gct, bg, frames = parse(path)
    print("=" * 72)
    print(f"{os.path.basename(path)}: {len(d)} bytes, canvas {w}x{h}, "
          f"background_index={bg}, frames={len(frames)}")
    show_palette("GCT", gct)

    for n, f in enumerate(frames):
        left, top, fw, fh = f["rect"]
        g = f["gce"] or dict(disposal=0, transparent=0, tindex=0, delay=0)
        print(f"  frame #{n}: rect=({left},{top},{fw},{fh}) "
              f"disposal={g['disposal']} transparency_flag={g['transparent']} "
              f"transparent_index={g['tindex']} delay_cs={g['delay']} "
              f"interlaced={int(f['interlace'])}")
        show_palette("LCT", f["lct"])
        pal = f["lct"] or gct
        idx, stop = lzw_decode(f["min_code"], f["payload"], fw * fh)
        print(f"    decoded indices ({len(idx)} bytes, stop={stop}), file order:")
        for y in range(fh):
            row = idx[y * fw:(y + 1) * fw]
            print("      " + " ".join(f"{v:3d}" for v in row) +
                  "   -> " + " ".join(f"({pal[v][0]},{pal[v][1]},{pal[v][2]})"
                                      if v < len(pal) else "(??)" for v in row))

    _, _, _, produced, _ = composite(path)
    for n, rgb in enumerate(produced):
        print(f"  composited output #{n} ({w}x{h}):")
        for y in range(h):
            cells = []
            for x in range(w):
                o = (y * w + x) * 3
                cells.append(f"({rgb[o]},{rgb[o + 1]},{rgb[o + 2]})")
            print("    " + " ".join(cells))


def conflicts(path):
    """Show that one index can mean different colours in different frames."""
    import collections

    d = open(path, "rb").read()
    w, h, gct, bg, frames = parse(path)
    print("=" * 72)
    print(f"{os.path.basename(path)}: {len(d)} bytes, canvas {w}x{h}, "
          f"frames={len(frames)}, background_index={bg}")
    show_palette("GCT", gct)
    lct_frames = [n for n, f in enumerate(frames) if f["lct"]]
    print(f"  frames with their own LCT: {len(lct_frames)}/{len(frames)}")
    if not lct_frames or gct is None:
        return
    differs0 = collections.Counter()
    differs_any = 0
    for n in lct_frames:
        lct = frames[n]["lct"]
        if tuple(lct[0]) != tuple(gct[0]):
            differs0[tuple(lct[0])] += 1
        if any(tuple(a) != tuple(b) for a, b in zip(lct, gct)):
            differs_any += 1
    print(f"  frames whose LCT[0] differs from GCT[0]: {sum(differs0.values())} "
          f"-> {dict(differs0)}")
    print(f"  frames whose LCT differs from the GCT anywhere: {differs_any}")
    first = lct_frames[0]
    print(f"  example, frame #{first}: GCT[0]={tuple(gct[0])} "
          f"but this frame's LCT[0]={tuple(frames[first]['lct'][0])}")


def main():
    args = sys.argv[1:]
    if args and args[0] == "--conflicts":
        import glob
        paths = args[1:] or sorted(glob.glob("assets/*.gif"))
        for p in paths:
            conflicts(p)
        return
    for p in (args or ["build_check/cases/local_palette_probe.gif"]):
        probe(p)


if __name__ == "__main__":
    main()
