"""Independent GIF LZW decoder, written from the specification.

Deliberately NOT a translation of lzw/lzw.c: the point of the reference tools in
this directory is to be wrong in different ways than the C code, so that when the
two disagree the disagreement is informative.

usage: python build_check/lzwref.py <gif>       # decode + report every frame
"""


def lzw_decode(min_code_size, payload, expected=None):
    """Decode one image block's LZW payload.

    Returns (bytes, stop) where stop is "eoi" for a clean end, "out-of-data" if
    the bit stream ran out first, or a description of the offending code.
    """
    clear = 1 << min_code_size
    eoi = clear + 1
    code_size = min_code_size + 1
    table = {i: bytes([i]) for i in range(clear)}
    next_code = eoi + 1
    bitpos = 0
    total_bits = len(payload) * 8
    out = bytearray()
    prev = None
    stop = None

    def read(nbits):
        nonlocal bitpos
        if bitpos + nbits > total_bits:
            return None
        val = 0
        for k in range(nbits):
            byte = payload[(bitpos + k) >> 3]
            val |= ((byte >> ((bitpos + k) & 7)) & 1) << k
        bitpos += nbits
        return val

    while True:
        code = read(code_size)
        if code is None:
            stop = "out-of-data"
            break
        if code == eoi:
            stop = "eoi"
            break
        if code == clear:
            table = {i: bytes([i]) for i in range(clear)}
            next_code = eoi + 1
            code_size = min_code_size + 1
            prev = None
            continue
        if code in table:
            entry = table[code]
        elif prev is not None and code == next_code:
            entry = prev + prev[:1]          # the KwKwK case
        else:
            stop = "bad code %d" % code
            break
        out += entry
        if prev is not None and next_code < 4096:
            table[next_code] = prev + entry[:1]
            next_code += 1
            if next_code == (1 << code_size) and code_size < 12:
                code_size += 1
        prev = entry

    return bytes(out), stop


def main():
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from ref_composite import parse
    for path in sys.argv[1:]:
        _w, _h, _gct, _bg, frames = parse(path)
        print("=" * 72)
        print(f"{os.path.basename(path)}: {len(frames)} frames")
        for n, f in enumerate(frames):
            fw, fh = f["rect"][2], f["rect"][3]
            idx, stop = lzw_decode(f["min_code"], f["payload"], fw * fh)
            print(f"  #{n}: min_code={f['min_code']} rect={fw}x{fh} "
                  f"payload={len(f['payload'])}B decoded={len(idx)}B "
                  f"expected={fw * fh} stop={stop}")


if __name__ == "__main__":
    main()
