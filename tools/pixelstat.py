"""Count non-background pixels in a screenshot, for automated verification of
rendering results / comparison against a reference browser.

Usage:
  python tools/pixelstat.py <image> [x0 y0 x1 y1] [--bg RRGGBB]

Output: size, non-background pixel count and ratio in the sampled region, the
bounding box of the non-background pixels, and the colors of a few sample points.
"""
import sys

try:
    from PIL import Image
except ImportError:  # fall back to plain PNG parsing when Pillow is unavailable
    Image = None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    args = sys.argv[2:]
    bg = None
    if "--bg" in args:
        i = args.index("--bg")
        bg = args[i + 1].lstrip("#")
        args = args[:i] + args[i + 2:]
    if Image is None:
        print("Pillow is required: pip install pillow")
        return 1
    im = Image.open(path).convert("RGB")
    w, h = im.size
    if len(args) >= 4:
        x0, y0, x1, y1 = (int(v) for v in args[:4])
    else:
        x0, y0, x1, y1 = 0, 0, w, h
    x0 = max(0, x0); y0 = max(0, y0)
    x1 = min(w, x1); y1 = min(h, y1)
    px = im.load()
    if bg is None:
        # use the top-left pixel as the background color reference
        bg = px[x0, y0]
        bg = "%02X%02X%02X" % bg
    br = int(bg[0:2], 16); bgc = int(bg[2:4], 16); bb = int(bg[4:6], 16)

    total = 0
    diff = 0
    minx, miny, maxx, maxy = x1, y1, -1, -1
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y]
            total += 1
            if abs(r - br) > 12 or abs(g - bgc) > 12 or abs(b - bb) > 12:
                diff += 1
                if x < minx: minx = x
                if y < miny: miny = y
                if x > maxx: maxx = x
                if y > maxy: maxy = y
    print("image=%s size=%dx%d bg=#%s region=(%d,%d)-(%d,%d)" %
          (path, w, h, bg, x0, y0, x1, y1))
    print("nonbg=%d/%d (%.2f%%)" % (diff, total, 100.0 * diff / max(1, total)))
    if maxx >= 0:
        print("bbox=(%d,%d)-(%d,%d) size=%dx%d" %
              (minx, miny, maxx, maxy, maxx - minx + 1, maxy - miny + 1))
    else:
        print("bbox=(none)  -- region has the same color as the background")
    return 0


if __name__ == "__main__":
    sys.exit(main())
