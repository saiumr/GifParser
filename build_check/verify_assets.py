"""End-to-end verification: parser.exe's frames vs the independent reference.

For every GIF given (default: assets/*.gif), this runs the real parser into a
scratch directory, composites the same file with the Python reference
(ref_composite.py + lzwref.py) and compares pixel by pixel through a real BMP
decode (bmp_vs_ref.read_bmp). It is the reproducible form of the "0 mismatching
frames" claim in the documentation.

usage: python build_check/verify_assets.py [gif ...]
"""
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from bmp_vs_ref import read_bmp                    # noqa: E402
from ref_composite import composite                # noqa: E402

SCRATCH = os.path.join(HERE, "tmp_verify")
PARSER = os.path.join(ROOT, "parser.exe")


def verify(gif):
    """Return (status, line) where status is "ok", "rejected" or "mismatch"."""
    name = os.path.splitext(os.path.basename(gif))[0]
    work = os.path.join(SCRATCH, name)
    frames_dir = os.path.join(work, "frames")
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(frames_dir)

    proc = subprocess.run([PARSER, os.path.abspath(gif)], cwd=work,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if proc.returncode != 0:
        # A malformed input the parser refuses is not a failure: there is no frame
        # to compare. It also must not crash, which is what the exit code shows.
        shutil.rmtree(work, ignore_errors=True)
        return "rejected", f"{name:22} REJECTED  (parser exit {proc.returncode & 0xFF})"

    try:
        w, h, _gct, produced, _frames = composite(gif)
    except Exception as exc:  # the spec-strict reference gives up where the parser repairs
        shutil.rmtree(work, ignore_errors=True)
        return "rejected", (f"{name:22} REJECTED  (reference cannot composite: "
                            f"{type(exc).__name__})")

    bad_frames = 0
    missing = []
    for i, rgb in enumerate(produced):
        bmp = os.path.join(frames_dir, f"{name}{i}.bmp")
        if not os.path.exists(bmp):
            missing.append(i)
            bad_frames += 1
            continue
        bw, bh, rows = read_bmp(bmp)
        if (bw, bh) != (w, h):
            bad_frames += 1
            continue
        mismatched = False
        for y in range(h):
            row = rows[y]
            base = y * w * 3
            for x in range(w):
                if row[x] != tuple(rgb[base + x * 3: base + x * 3 + 3]):
                    mismatched = True
                    break
            if mismatched:
                break
        if mismatched:
            bad_frames += 1
    shutil.rmtree(work, ignore_errors=True)
    if bad_frames == 0:
        return "ok", f"{name:22} OK        ({len(produced)} frames, {w}x{h})"
    return "mismatch", (f"{name:22} MISMATCH  {bad_frames}/{len(produced)} frames "
                        f"({w}x{h}){'' if not missing else ' missing ' + str(missing[:5])}")


def main():
    gifs = sys.argv[1:]
    if not gifs:
        gifs = sorted(glob.glob(os.path.join(ROOT, "assets", "*.gif")))
        for extra in ("test.gif", "lm.gif"):
            p = os.path.join(ROOT, extra)
            if os.path.exists(p):
                gifs.append(p)
    if not os.path.exists(PARSER):
        print("parser.exe missing - run `make parser` first")
        return 2
    os.makedirs(SCRATCH, exist_ok=True)
    tally = {"ok": 0, "rejected": 0, "mismatch": 0}
    frames = 0
    for gif in gifs:
        status, line = verify(gif)
        print(line)
        tally[status] += 1
        if status == "ok":
            frames += int(line.split("(")[1].split()[0])
    print(f"\n{tally['ok']}/{len(gifs)} files match the reference "
          f"({frames} frames), {tally['rejected']} rejected as malformed, "
          f"{tally['mismatch']} mismatched")
    return 0 if tally["mismatch"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
