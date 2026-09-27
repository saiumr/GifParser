"""Verify the incremental (dirty-rectangle) output path end to end.

For each GIF this:
  1. composites the file with the Python reference (independent, from-spec);
  2. runs build_check/dirtycheck.exe, which keeps a persistent "screen" buffer
     and updates only GIFCanvasDirtyRect(i) out of the canvas each frame;
  3. compares the two frame sets byte for byte.

Step 3 is the point: if the dirty rectangle is ever one pixel too small, the
screen keeps a stale pixel and the comparison fails. A canvas-only check cannot
see that, because the canvas itself is always correct.

usage: python build_check/verify_dirtyrect.py [gif ...]
"""
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from cmp_dumps import digests, read_meta         # noqa: E402
from ref_composite import composite              # noqa: E402

EXE = os.path.join(HERE, "dirtycheck.exe")
SCRATCH = os.path.join(HERE, "tmp_dirty")


def reference_dumps(gif, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    w, h, _gct, produced, _frames = composite(gif)
    with open(os.path.join(out_dir, "meta.txt"), "w") as f:
        f.write(f"w={w} h={h} count={len(produced)}\n")
    for i, rgb in enumerate(produced):
        with open(os.path.join(out_dir, f"f{i:04d}.rgb"), "wb") as fp:
            fp.write(rgb)
    return w, h, len(produced)


def verify(gif):
    name = os.path.splitext(os.path.basename(gif))[0]
    work = os.path.join(SCRATCH, name)
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)

    proc = subprocess.run([EXE, os.path.abspath(gif), work],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if proc.returncode != 0:
        shutil.rmtree(work, ignore_errors=True)
        return "rejected", f"{name:22} REJECTED  (dirtycheck exit {proc.returncode & 0xFF})"

    try:
        w, h, count = reference_dumps(gif, os.path.join(work, "ref"))
    except Exception as exc:
        shutil.rmtree(work, ignore_errors=True)
        return "rejected", f"{name:22} REJECTED  (reference: {type(exc).__name__})"

    ref = digests(os.path.join(work, "ref"), w, h)
    screen = digests(work, w, h)
    diffs = [k for k in sorted(set(ref) | set(screen)) if ref.get(k) != screen.get(k)]
    shutil.rmtree(work, ignore_errors=True)
    if diffs:
        return "mismatch", (f"{name:22} MISMATCH  {len(diffs)}/{len(ref)} frames differ "
                            f"from the reference: {diffs[:6]}")
    return "ok", f"{name:22} OK        ({count} frames, {w}x{h})"


def main():
    gifs = sys.argv[1:]
    if not gifs:
        gifs = sorted(glob.glob(os.path.join(ROOT, "assets", "*.gif")))
        for extra in ("test.gif", "lm.gif"):
            p = os.path.join(ROOT, extra)
            if os.path.exists(p):
                gifs.append(p)
    if not os.path.exists(EXE):
        print("dirtycheck.exe missing - build it with:")
        print("  gcc build_check/dirtycheck.c gif_parser.c gif_canvas.c ./lzw/*.c -I\"./\" -O2 "
              "-o build_check/dirtycheck.exe")
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
    print(f"\n{tally['ok']}/{len(gifs)} files: the incrementally updated screen is "
          f"pixel-identical to the reference ({frames} frames), "
          f"{tally['rejected']} rejected, {tally['mismatch']} mismatched")
    return 0 if tally["mismatch"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
