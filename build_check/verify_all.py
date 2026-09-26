"""Run every verification the documentation quotes, in one command.

Each check is also usable on its own; this driver just runs them in the right
order, keeps their output, and prints one summary. It builds nothing: run the
compilers first (see the README) so the binaries match the current sources.

usage: python build_check/verify_all.py
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

CASES = sorted(os.path.join(HERE, "cases", f) for f in os.listdir(os.path.join(HERE, "cases"))
               if f.endswith(".gif"))


def run(script, args=()):
    proc = subprocess.run([sys.executable, os.path.join(HERE, script), *args],
                          cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return proc.returncode, proc.stdout


def last_summary(text):
    lines = [ln for ln in text.splitlines() if ln.strip()]
    return lines[-1] if lines else "(no output)"


def check_verify_assets():
    lines = []
    bad = 0
    for label, args in (("assets", ()), ("crafted cases", CASES)):
        code, out = run("verify_assets.py", args)
        lines.append(f"  {label:14} {last_summary(out)}")
        bad += code != 0
    return bad, lines


def check_verify_dirtyrect():
    lines = []
    bad = 0
    for label, args in (("assets", ()), ("crafted cases", CASES)):
        code, out = run("verify_dirtyrect.py", args)
        lines.append(f"  {label:14} {last_summary(out)}")
        bad += code != 0
    return bad, lines


def check_verify_sdl():
    lines = []
    bad = 0
    for label, args in (("assets", ()), ("crafted cases", CASES)):
        code, out = run("verify_sdl.py", args)
        lines.append(f"  {label:14} {last_summary(out)}")
        bad += code != 0
    return bad, lines


def check_leakcheck():
    exe = os.path.join(HERE, "leakcheck.exe")
    if not os.path.exists(exe):
        return 1, ["  leakcheck.exe missing"]
    targets = CASES + sorted(os.path.join(ROOT, "assets", f)
                             for f in os.listdir(os.path.join(ROOT, "assets"))
                             if f.endswith(".gif"))
    for extra in ("test.gif", "lm.gif"):
        p = os.path.join(ROOT, extra)
        if os.path.exists(p):
            targets.append(p)
    ok = 0
    leaks = []
    for target in targets:
        proc = subprocess.run([exe, target], cwd=ROOT, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
        if proc.returncode == 0:
            ok += 1
        else:
            leaks.append(os.path.basename(target))
    line = f"  {len(targets)} inputs    allocation-balanced (decode, clear, 2 playback passes): {ok}"
    if leaks:
        line += f"  LEAKING: {leaks}"
    return (len(leaks) != 0), [line]


def check_determinism():
    exe = os.path.join(HERE, "dumpanim.exe")
    if not os.path.exists(exe):
        return 1, ["  dumpanim.exe missing"]
    targets = CASES + sorted(os.path.join(ROOT, "assets", f)
                             for f in os.listdir(os.path.join(ROOT, "assets"))
                             if f.endswith(".gif"))
    code, out = run("det_check.py", targets)
    match = re.search(r"unstable/failed files: (\d+)/(\d+)", out)
    if match:
        bad, total = int(match.group(1)), int(match.group(2))
        return (bad != 0), [f"  {total} inputs x 3  unstable or crashed: {bad}"]
    return 1, ["  det_check produced no summary"]


def main():
    checks = [
        ("BMP output vs spec reference", check_verify_assets),
        ("incremental screen vs spec reference", check_verify_dirtyrect),
        ("SDL texture accumulation vs canvas", check_verify_sdl),
        ("allocation balance", check_leakcheck),
        ("determinism", check_determinism),
    ]
    failures = 0
    for title, fn in checks:
        bad, lines = fn()
        print(f"{title}")
        for line in lines:
            print(line)
        if bad:
            failures += 1
        print()
    print("ALL CHECKS PASSED" if failures == 0 else f"FAILING CHECKS: {failures}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
