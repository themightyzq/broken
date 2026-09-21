#!/usr/bin/env python3
"""check_ids.py — guard against hand-formatted indexed-bank parameter ids.

WHY THIS EXISTS: plugin/src/plugin/Params.h owns the ids for the three
indexed parameter banks (osc.h%02d, osc.d%03d, ws.c%03d) via harmonicId(),
drawPointId(), curvePointId(). Nothing else is supposed to format or spell
these ids out. In v0.20/v0.21 a hand-formatted literal survived a bank
renumbering elsewhere in the tree: getParameter() returned nullptr for the
stale id, a null guard swallowed it silently, and a UI control just stopped
working with no build error, no crash, no log line — a totally silent dead
control. This script exists so that class of bug fails a build instead of
shipping.

It walks plugin/src/ and fails if any file OTHER THAN Params.h contains
either a printf-style format literal (e.g. "osc.h%02d") or a fully
spelled-out literal (e.g. "osc.h01") for one of the three banks.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "plugin" / "src"
OWNER = (ROOT / "plugin" / "Params.h").resolve()

# printf-style: "osc.h%02d", "osc.d%3d", "ws.c%d", etc.
FORMAT_RE = re.compile(r'"(osc\.h|osc\.d|ws\.c)%0?\d*d')
# fully spelled-out: "osc.h01", "osc.d003", "ws.c016", etc.
LITERAL_RE = re.compile(r'"(osc\.h0|osc\.d0|ws\.c0)\d+')

SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".mm"}


def main() -> int:
    failures = []
    scanned = 0
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or path.suffix not in SUFFIXES:
            continue
        if path.resolve() == OWNER:
            continue
        scanned += 1
        for lineno, line in enumerate(path.read_text(errors="replace").splitlines(), start=1):
            # Ignore text after a "//" line comment — this catches real code, not the
            # explanatory prose (like this very file's own history) that quotes an id.
            code = line.split("//", 1)[0]
            m = FORMAT_RE.search(code) or LITERAL_RE.search(code)
            if m:
                failures.append(f"{path}:{lineno}: {line.strip()}")

    if failures:
        print("check_ids: FAIL — hand-formatted bank id(s) found outside Params.h:")
        for f in failures:
            print(f"  {f}")
        return 1

    print(f"check_ids: OK ({scanned} files scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
