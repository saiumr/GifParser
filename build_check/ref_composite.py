"""Independent index-domain GIF compositor, used as the arbitration reference.

Composites a GIF strictly by the specification:
  canvas starts filled with the background index
  per frame: skip transparent pixels, then apply the disposal method
This is deliberately written from the spec rather than from the C code so that
it can arbitrate between two C implementations that disagree.

usage: python ref_composite.py <gif> <out_dir>
writes f%04d.rgb (RGB triplets) + meta.txt
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzwref import lzw_decode  # noqa: E402


def parse(path):
    d = open(path, "rb").read()
    if d[:3] != b"GIF":
        raise ValueError("not a gif")
    w, h = struct.unpack_from("<HH", d, 6)
    packed = d[10]
    bg = d[11]
    i = 13
    gct = None
    if packed & 0x80:
        n = 2 ** ((packed & 7) + 1)
        gct = [list(d[i + k * 3 : i + k * 3 + 3]) for k in range(n)]
        i += n * 3
    frames = []
    gce = None
    while i < len(d):
        b = d[i]
        if b == 0x3B:
            break
        if b == 0x21:
            label = d[i + 1]
            if label == 0xF9:
                pf = d[i + 3]
                gce = dict(disposal=(pf >> 2) & 7, transparent=pf & 1,
                           delay=struct.unpack_from("<H", d, i + 4)[0], tindex=d[i + 6])
                i += 8
            else:
                i += 2
                while d[i]:
                    i += 1 + d[i]
                i += 1
            continue
        if b == 0x2C:
            left, top, fw, fh = struct.unpack_from("<HHHH", d, i + 1)
            ip = d[i + 9]
            i += 10
            lct = None
            if ip & 0x80:
                n = 2 ** ((ip & 7) + 1)
                lct = [list(d[i + k * 3 : i + k * 3 + 3]) for k in range(n)]
                i += n * 3
            min_code = d[i]
            i += 1
            payload = bytearray()
            while d[i]:
                payload += d[i + 1 : i + 1 + d[i]]
                i += 1 + d[i]
            i += 1
            frames.append(dict(rect=(left, top, fw, fh), lct=lct, min_code=min_code,
                               payload=bytes(payload), gce=gce, interlace=bool(ip & 0x40)))
            gce = None
            continue
        i += 1
    return w, h, gct, bg, frames


def deinterlace(rows, h):
    order = []
    for start, step in ((0, 8), (4, 8), (2, 4), (1, 2)):
        order += list(range(start, h, step))
    out = [None] * h
    for src, dst in enumerate(order):
        if src < len(rows):
            out[dst] = rows[src]
    return out


def composite(path):
    """Composite and return the RGB canvas sequence.

    Two details matter for comparing against the C implementation:
      * the canvas stores indices, but each pixel must be rendered through the
        palette of the frame that last wrote it (a pixel carried over from an
        earlier frame keeps that frame's palette);
      * rows shorter than the rectangle (truncated data) decode as index 0.

    Rectangles are clipped to the canvas. That is not a convenience: a malformed
    file can describe a rectangle that reaches outside the logical screen, and a
    decoder that writes it out of bounds is broken. Clipping here is what lets
    `build_check/cases/rect_overflow.gif` be arbitrated at all.
    """
    w, h, gct, bg, frames = parse(path)
    canvas = [bg] * (w * h)           # current palette index per pixel
    canvas_pal = [gct] * (w * h)      # palette that owns the index
    produced = []
    for f in frames:
        left, top, fw, fh = f["rect"]
        g = f["gce"] or dict(disposal=0, transparent=0, tindex=0, delay=0)
        pal = f["lct"] or gct
        idx, _stop = lzw_decode(f["min_code"], f["payload"], fw * fh)
        rows = [list(idx[r * fw: (r + 1) * fw]) for r in range(fh)]
        if f["interlace"]:
            rows = deinterlace(rows, fh)

        x0, y0 = min(left, w), min(top, h)
        x1, y1 = min(left + fw, w), min(top + fh, h)

        def src_pixel(dst_x, dst_y):
            sx, sy = dst_x - left, dst_y - top
            if sy >= len(rows) or sx >= len(rows[sy]):
                return 0
            return rows[sy][sx]

        saved = None
        saved_pal = None
        if g["disposal"] == 3 and x0 < x1 and y0 < y1:
            saved = [canvas[y * w + x0: y * w + x1] for y in range(y0, y1)]
            saved_pal = [canvas_pal[y * w + x0: y * w + x1] for y in range(y0, y1)]
        for dst_y in range(y0, y1):
            for dst_x in range(x0, x1):
                v = src_pixel(dst_x, dst_y)
                if g["transparent"] and v == g["tindex"]:
                    continue
                canvas[dst_y * w + dst_x] = v
                canvas_pal[dst_y * w + dst_x] = pal
        rgb = bytearray()
        for i in range(w * h):
            p = canvas_pal[i]
            v = canvas[i]
            rgb += bytes(p[v]) if p is not None and v < len(p) else b"\xff\x00\xff"
        produced.append(rgb)
        if g["disposal"] == 2:
            # spec: "restore to background colour" - the Logical Screen Descriptor
            # background index, NOT the frame's transparent index
            for dst_y in range(y0, y1):
                for dst_x in range(x0, x1):
                    canvas[dst_y * w + dst_x] = bg
                    canvas_pal[dst_y * w + dst_x] = gct
        elif g["disposal"] == 3 and saved is not None:
            for n, dst_y in enumerate(range(y0, y1)):
                canvas[dst_y * w + x0: dst_y * w + x1] = saved[n]
                canvas_pal[dst_y * w + x0: dst_y * w + x1] = saved_pal[n]
    return w, h, gct, produced, frames


def main():
    path, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    w, h, gct, produced, frames = composite(path)
    with open(os.path.join(out, "meta.txt"), "w") as f:
        f.write(f"w={w} h={h} count={len(produced)}\n")
    for i, rgb in enumerate(produced):
        with open(os.path.join(out, f"f{i:04d}.rgb"), "wb") as fp:
            fp.write(rgb)
    print(f"{os.path.basename(path)}: {w}x{h} {len(produced)} frames -> {out}")


if __name__ == "__main__":
    main()
