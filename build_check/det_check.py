"""Run dumpanim.exe three times per GIF and verify the dumps are identical.

A non-deterministic decode (uninitialised memory, heap corruption) shows up here
even when no single run crashes.
"""
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "dumpanim.exe")
RUNS = os.path.join(HERE, "det")


def digest_dir(d):
    out = {}
    for fn in sorted(os.listdir(d)):
        if fn.endswith(".rgb"):
            out[fn] = hashlib.sha256(open(os.path.join(d, fn), "rb").read()).hexdigest()
    return out


def main():
    targets = sys.argv[1:]
    bad = 0
    for t in targets:
        name = os.path.splitext(os.path.basename(t))[0]
        digests = []
        codes = []
        for r in range(3):
            d = os.path.join(RUNS, f"{name}_{r}")
            os.makedirs(d, exist_ok=True)
            for fn in os.listdir(d):
                os.remove(os.path.join(d, fn))
            p = subprocess.run([EXE, t, d], capture_output=True)
            codes.append(p.returncode)
            digests.append(digest_dir(d))
        stable = all(d == digests[0] for d in digests)
        # exit 0 = decoded, exit 1 = the parser rejected the file cleanly;
        # anything else (e.g. 0xC0000005) is a crash and must never happen
        ok_exit = all(c in (0, 1) for c in codes)
        crashed = [c for c in codes if c not in (0, 1)]
        frames = len(digests[0])
        status = "OK" if (stable and ok_exit) else ("CRASH" if crashed else "UNSTABLE")
        if not (stable and ok_exit):
            bad += 1
        print(f"{name:26} {status:9} frames={frames:4} exits={codes}")
    print(f"\nunstable/failed files: {bad}/{len(targets)}")


if __name__ == "__main__":
    main()
