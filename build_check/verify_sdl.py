"""Verify the SDL texture path: dirty-rectangle uploads must accumulate correctly.

The player does not own the screen, the texture does. It updates only
GIFCanvasDirtyRect(i) each frame, so the texture has to keep every pixel that was
not uploaded this frame. build_check/sdlcheck.c drives that for real - dummy video
driver, software renderer, SDL_LockTexture on the dirty region - then renders and
reads the whole target back and compares it with the canvas, pixel for pixel.

Nothing here touches a visible window.

usage: python build_check/verify_sdl.py [gif ...]
"""
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(HERE, "sdlcheck.exe")


def verify(gif, stride=1):
    name = os.path.splitext(os.path.basename(gif))[0]
    args = [EXE, os.path.abspath(gif)]
    if stride > 1:
        args.append(str(stride))
    proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    line = ""
    for candidate in proc.stdout.splitlines():
        if candidate.startswith("SDLCHECK "):
            line = candidate
    if not line:
        detail = proc.stdout.strip().splitlines()[-1] if proc.stdout.strip() else "no output"
        return "rejected", f"{name:22} REJECTED  ({detail})"
    ok = "mismatches=0 OK" in line
    upload = line.split("uploaded_pixels=")[1].split()[0]
    compared = line.split("compared=")[1].split()[0]
    checked = line.split("checked=")[1].split()[0]
    if not ok:
        return "mismatch", f"{name:22} MISMATCH  {line}"
    return "ok", (f"{name:22} OK        {checked} frames checked, uploaded {upload} px, "
                  f"screen==canvas over {compared} px")


def main():
    gifs = sys.argv[1:]
    if not gifs:
        gifs = sorted(glob.glob(os.path.join(ROOT, "assets", "*.gif")))
        for extra in ("test.gif", "lm.gif"):
            p = os.path.join(ROOT, extra)
            if os.path.exists(p):
                gifs.append(p)
    if not os.path.exists(EXE):
        print("sdlcheck.exe missing - build it with:")
        print('  gcc build_check/sdlcheck.c gif_parser.c gif_canvas.c ./lzw/*.c -I"./" '
              '-I"SDL3/include" -L"SDL3/lib" -lSDL3 -o build_check/sdlcheck.exe')
        return 2

    tally = {"ok": 0, "rejected": 0, "mismatch": 0}
    for gif in gifs:
        status, line = verify(gif)
        print(line)
        tally[status] += 1
    print(f"\n{tally['ok']}/{len(gifs)} files: the SDL texture accumulated the dirty "
          f"rectangles correctly, {tally['rejected']} rejected, "
          f"{tally['mismatch']} mismatched")
    return 0 if tally["mismatch"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
