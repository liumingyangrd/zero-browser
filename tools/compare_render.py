"""Structurally compare zero-browser's rendering result with a reference browser screenshot.

Usage:
  python tools/compare_render.py <mine.bmp|png> <ref.png> [--crop-top N] [--rows K]

Method: project the "non-background pixels" onto rows/columns to find the segment
      boundaries of the content bands, then compare the segment positions of both
      sides, which quantifies layout differences (rather than just eyeballing
      whether the two "look alike").
--crop-top N: the zero-browser screenshot has N pixels of browser chrome at the top;
      crop that away before comparing.
"""
import sys

from PIL import Image


def profile(path, crop_top, axis):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()
    y0 = min(crop_top, h - 1)
    bg = px[0, y0]
    size = h - y0 if axis == "row" else w
    spans = []
    in_run = False
    start = 0
    if axis == "row":
        rng = range(y0, h)
        for i in rng:
            marked = False
            for x in range(0, w, 3):
                r, g, b = px[x, i]
                if abs(r - bg[0]) > 12 or abs(g - bg[1]) > 12 or abs(b - bg[2]) > 12:
                    marked = True
                    break
            if marked and not in_run:
                in_run = True
                start = i
            elif not marked and in_run:
                in_run = False
                spans.append((start, i - 1))
        if in_run:
            spans.append((start, h - 1))
    else:
        for j in range(w):
            marked = False
            for y in range(y0, h, 3):
                r, g, b = px[j, y]
                if abs(r - bg[0]) > 12 or abs(g - bg[1]) > 12 or abs(b - bg[2]) > 12:
                    marked = True
                    break
            if marked and not in_run:
                in_run = True
                start = j
            elif not marked and in_run:
                in_run = False
                spans.append((start, j - 1))
        if in_run:
            spans.append((start, w - 1))
    return im.size, spans


def merge(spans, gap=6):
    """Merge small segments separated by tiny gaps into content bands, for easier comparison."""
    out = []
    for s, e in spans:
        if out and s - out[-1][1] <= gap:
            out[-1] = (out[-1][0], e)
        else:
            out.append((s, e))
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    mine, edge = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    crop_top = 78
    rows = 12
    if "--crop-top" in args:
        crop_top = int(args[args.index("--crop-top") + 1])
    if "--rows" in args:
        rows = int(args[args.index("--rows") + 1])

    for axis in ("row", "col"):
        ms, mspans = profile(mine, crop_top if axis == "row" else 0, axis)
        es, espa = profile(edge, 0, axis)
        mt = merge(mspans)
        et = merge(espa)
        label = "rows (vertical content bands)" if axis == "row" else "cols (horizontal content bands)"
        print("== %s ==" % label)
        print("  zero-browser %s segments=%d" % (ms, len(mt)))
        print("               %s" % (mt[:rows],))
        print("  reference    %s segments=%d" % (es, len(et)))
        print("               %s" % (et[:rows],))
    return 0


if __name__ == "__main__":
    sys.exit(main())
