"""Sanity check the markdown docs: balanced code fences, mermaid blocks, links."""
import os
import re
import sys

DOCS = ["Readme.md", "docs/Architecture.md", "docs/Architecture.zh-CN.md",
        "docs/Concepts.zh-CN.md"]
FENCE = re.compile(r"^```", re.M)
MERMAID = re.compile(r"^```mermaid", re.M)
LINK = re.compile(r"\[([^\]]+)\]\((\./[^)#]+)\)")

bad = 0
for path in DOCS:
    if not os.path.exists(path):
        print(f"{path}: MISSING")
        bad += 1
        continue
    text = open(path, encoding="utf-8").read()
    fences = FENCE.findall(text)
    balanced = len(fences) % 2 == 0
    if not balanced:
        bad += 1
    print(f"{path}: {len(text.splitlines())} lines, {len(fences)} fences "
          f"({'balanced' if balanced else 'UNBALANCED'}), {len(MERMAID.findall(text))} mermaid")

    for label, target in LINK.findall(text):
        resolved = os.path.normpath(os.path.join(os.path.dirname(path) or ".", target))
        if not os.path.exists(resolved):
            print(f"   BROKEN LINK: [{label}]({target}) -> {resolved}")
            bad += 1

# the English and Chinese architecture docs should have matching section counts
def sections(path):
    return len(re.findall(r"^#{2,3} ", open(path, encoding="utf-8").read(), re.M))

en, zh = sections("docs/Architecture.md"), sections("docs/Architecture.zh-CN.md")
print(f"\nsections: Architecture.md={en} Architecture.zh-CN.md={zh} "
      f"({'match' if en == zh else 'DIFFER'})")
if en != zh:
    bad += 1

print("OK" if bad == 0 else f"PROBLEMS: {bad}")
sys.exit(0 if bad == 0 else 1)
