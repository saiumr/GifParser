"""Read-only GIF structure inspector used to audit the parser's assumptions.

Only reads: it walks the block structure of a GIF file and prints per-frame
metadata (disposal, transparency, interlace, local color table, sub-block count).
"""
import glob
import os
import struct
import sys


def be16(b, i):
    return struct.unpack_from("<H", b, i)[0]


def inspect(path):
    data = open(path, "rb").read()
    out = {"file": os.path.basename(path), "bytes": len(data)}
    if data[:3] != b"GIF":
        out["error"] = "not a GIF"
        return out
    out["version"] = data[3:6].decode("ascii", "replace")
    w, h = be16(data, 6), be16(data, 8)
    packed = data[10]
    out["canvas"] = (w, h)
    out["global_color_table"] = bool(packed & 0x80)
    gct_size = 2 ** ((packed & 0x07) + 1) if packed & 0x80 else 0
    out["gct_entries"] = gct_size
    out["bg_index"] = data[11]
    out["aspect"] = data[12]
    i = 13 + gct_size * 3

    frames = []
    app_ext = []
    comment_bytes = 0
    plain_text = 0
    trailer = False
    pending_gce = None
    truncated = None

    def read_sub_blocks(i):
        total = 0
        blocks = 0
        while True:
            if i >= len(data):
                raise EOFError("sub-block overrun")
            n = data[i]
            if n == 0:
                return i + 1, total, blocks
            i += 1 + n
            total += n
            blocks += 1

    while i < len(data):
        try:
            b = data[i]
            if b == 0x3B:
                trailer = True
                break
            if b == 0x21:
                label = data[i + 1]
                if label == 0xF9:
                    size = data[i + 2]
                    gce = data[i + 3 : i + 3 + size]
                    pf = gce[0]
                    pending_gce = {
                        "disposal": (pf >> 2) & 0x07,
                        "user_input": (pf >> 1) & 1,
                        "transparency": pf & 1,
                        "transparent_index": gce[3] if len(gce) > 3 else None,
                        "delay_cs": struct.unpack_from("<H", gce, 1)[0] if len(gce) > 2 else None,
                    }
                    i += 3 + size
                    if data[i] != 0:
                        raise EOFError("GCE not terminated")
                    i += 1
                elif label == 0xFF:
                    size = data[i + 2]
                    ident = data[i + 3 : i + 3 + size]
                    i += 3 + size
                    i, total, blocks = read_sub_blocks(i)
                    app_ext.append(
                        {"ident": ident.decode("ascii", "replace"), "data_bytes": total, "blocks": blocks}
                    )
                elif label == 0xFE:
                    i += 2
                    i, total, blocks = read_sub_blocks(i)
                    comment_bytes += total
                elif label == 0x01:
                    plain_text += 1
                    size = data[i + 2]
                    i += 3 + size
                    i, _, _ = read_sub_blocks(i)
                else:
                    out["error"] = f"unknown extension label 0x{label:02X} at {i}"
                    break
                continue
            if b == 0x2C:
                left, top, fw, fh = struct.unpack_from("<HHHH", data, i + 1)
                ipacked = data[i + 9]
                lct = bool(ipacked & 0x80)
                lct_size = 2 ** ((ipacked & 0x07) + 1) if lct else 0
                interlace = bool(ipacked & 0x40)
                j = i + 10 + lct_size * 3
                lzw_min = data[j]
                j += 1
                j, total, blocks = read_sub_blocks(j)
                frames.append(
                    {
                        "rect": (left, top, fw, fh),
                        "full_canvas": (left, top, fw, fh) == (0, 0, w, h),
                        "interlace": interlace,
                        "lct_entries": lct_size,
                        "lzw_min": lzw_min,
                        "data_bytes": total,
                        "blocks": blocks,
                        "gce": pending_gce,
                    }
                )
                pending_gce = None
                i = j
                continue
            out["error"] = f"unexpected byte 0x{b:02X} at offset {i}"
            break
        except (EOFError, IndexError, struct.error) as exc:
            truncated = f"{exc} at offset {i}"
            break

    out["truncated"] = truncated
    out["trailer"] = trailer
    out["frames"] = frames
    out["frame_count"] = len(frames)
    out["app_ext"] = app_ext
    out["comment_bytes"] = comment_bytes
    out["plain_text_ext"] = plain_text
    return out


def main():
    paths = sys.argv[1:] or sorted(glob.glob("assets/*.gif")) + ["test.gif", "lm.gif"]
    for p in paths:
        o = inspect(p)
        print("=" * 78)
        if "error" in o and "frames" not in o:
            print(f"{o['file']}: {o['error']}")
            continue
        print(
            f"{o['file']}: {o['bytes']}B version={o.get('version')} canvas={o.get('canvas')} "
            f"gct={o.get('gct_entries')} frames={o.get('frame_count')} trailer={o.get('trailer')} "
            f"truncated={o.get('truncated')} err={o.get('error')}"
        )
        print(f"  app_ext={o['app_ext']} comment_bytes={o['comment_bytes']} plain_text={o['plain_text_ext']}")
        disposals = {}
        partial = 0
        interlaced = 0
        lct_frames = 0
        no_gce = 0
        trans = 0
        delays = set()
        for n, f in enumerate(o["frames"]):
            g = f["gce"]
            if g is None:
                no_gce += 1
                d = "NO-GCE"
            else:
                d = g["disposal"]
                disposals[d] = disposals.get(d, 0) + 1
                if g["transparency"]:
                    trans += 1
                delays.add(g["delay_cs"])
            partial += 0 if f["full_canvas"] else 1
            interlaced += 1 if f["interlace"] else 0
            lct_frames += 1 if f["lct_entries"] else 0
            if n < 12 or not f["full_canvas"]:
                print(
                    f"   #{n:3d} rect={f['rect']} full={f['full_canvas']} il={int(f['interlace'])} "
                    f"lct={f['lct_entries']} lzw_min={f['lzw_min']} bytes={f['data_bytes']} "
                    f"blocks={f['blocks']} gce={g}"
                )
        print(
            f"  summary: disposal={disposals} partial_rects={partial} interlaced={interlaced} "
            f"local_color_tables={lct_frames} frames_without_GCE={no_gce} transparency_frames={trans} "
            f"distinct_delays(cs)={sorted(delays)}"
        )


if __name__ == "__main__":
    main()
