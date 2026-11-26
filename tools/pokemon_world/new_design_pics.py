#!/usr/bin/env python3
"""Pokémon World: battle pictures for the extra player designs (Hilbert, Hilda, Lyra, Nate, Rosa).

Front pictures
  Hilbert, Hilda: Pokémon Black's battle pictures 0 and 1 (a/0/7/2), from the owner's ROM.
  Nate, Rosa:     Pokémon Black 2's pictures 0 and 1 (a/0/7/1). BW2 builds them from parts at run
                  time; each parts sheet (its second graphic, a 256x128 bitmap) also holds the
                  whole standing figure, at its right edge, which is what is used.
  Lyra:           HeartGold's trainer picture from the owner's sprite sheet (sources/lyra_front.png).
Back pictures (4 frames of 64x64: standing, arm back, throw, after the throw)
  From the owner's sheets (graphics/trainers/back_pics/sources/<name>.png): BW's / BW2's / HGSS's three
  throwing poses, scaled so the head is about Ethan's size and cut at the waist.

    PYTHONPATH=~/pylib python3 tools/pokemon_world/new_design_pics.py BW.nds BW2.nds
(the ROMs are only needed for the BW / BW2 front pictures; without them those are left as they are)
"""
import os
import struct
import sys
from collections import Counter

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools/pokemon_world/bw"))
FRONT = os.path.join(ROOT, "graphics/trainers/front_pics")
BACK = os.path.join(ROOT, "graphics/trainers/back_pics")
SRC = os.path.join(BACK, "sources")

# back-pic source: (file, background colour, scale)
BACKS = {
    "hilbert": ("hilbert.png", (160, 160, 160), 0.7),
    "hilda":   ("hilda.png", (64, 160, 32), 0.7),
    "nate":    ("nate.png", (176, 248, 168), 0.7),
    "rosa":    ("rosa.png", (248, 128, 248), 0.7),
    "lyra":    ("lyra.png", (56, 144, 88), 0.8),
}


# The DS draws the arm of the standing pose as separate parts (the loose arm pieces at the right of each
# sheet), laid over the first pose: (piece's box on the sheet, x, y in the pose's own coordinates).
# Frame 1 (before the throw) holds the ball up in front:
ARMS_BALL = {
    "hilbert": [((195, 82, 223, 126), 10, 30)],
    "hilda":   [((193, 84, 216, 123), 16, 36)],
    "nate":    [((203, 81, 232, 126), 8, 30)],
    "rosa":    [((217, 83, 229, 121), 24, 30)],
    "lyra":    [((201, 69, 224, 108), 12, 30)],
}
# Frame 4 (after the throw): the empty arm hangs down. BW's is in two pieces (upper arm, then forearm
# and fist from the elbow), BW2's in one.
ARMS = {
    "hilbert": [((194, 42, 211, 71), 18, 38), ((218, 40, 234, 70), 10, 64)],
    "hilda":   [((199, 42, 212, 71), 22, 44), ((218, 40, 233, 70), 14, 70)],
    "nate":    [((236, 68, 254, 127), 22, 38)],
    "rosa":    [((233, 63, 252, 123), 25, 36)],
    "lyra":    [((207, 27, 220, 56), 18, 37), ((226, 25, 241, 55), 10, 63)],
}


def rgb555(c):
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)


def gba(c):
    return tuple(v & 0xF8 for v in c)


def is_dark(c):
    return sum(c) < 200


def components(mask, w, h):
    seen = set(); comps = []
    for y in range(h):
        for x in range(w):
            if mask[y * w + x] and (x, y) not in seen:
                st = [(x, y)]; seen.add((x, y)); pts = []
                while st:
                    p = st.pop(); pts.append(p)
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            q = (p[0] + dx, p[1] + dy)
                            if 0 <= q[0] < w and 0 <= q[1] < h and q not in seen and mask[q[1] * w + q[0]]:
                                seen.add(q); st.append(q)
                comps.append(pts)
    return comps


def downscale(im, s):
    """RGBA pixel art scaled by s (< 1): each target pixel takes the colour covering most of it
    (dark outline weighted up), transparent if mostly transparent."""
    if abs(s - 1) < 1e-6:
        return im.copy()
    W, H = max(1, round(im.width * s)), max(1, round(im.height * s))
    src = im.load(); out = Image.new("RGBA", (W, H)); dst = out.load()
    f = 1 / s
    for y in range(H):
        for x in range(W):
            votes = Counter()
            y0, y1, x0, x1 = y * f, (y + 1) * f, x * f, (x + 1) * f
            for sy in range(int(y0), min(im.height, int(y1 + 0.999))):
                wy = min(y1, sy + 1) - max(y0, sy)
                for sx in range(int(x0), min(im.width, int(x1 + 0.999))):
                    wx = min(x1, sx + 1) - max(x0, sx)
                    p = src[sx, sy]
                    k = p[:3] if p[3] >= 128 else None
                    votes[k] += wx * wy * (1.35 if k is not None and is_dark(k) else 1.0)
            k = max(votes, key=votes.get)
            dst[x, y] = (k + (255,)) if k is not None else (0, 0, 0, 0)
    return out


def palette_of(images, n=15):
    """Up to n colours for all `images` (PIL's median cut over their opaque pixels)."""
    pts = []
    for im in images:
        pts += [p[:3] for p in im.getdata() if p[3] >= 128]
    strip = Image.new("RGB", (len(pts), 1)); strip.putdata(pts)
    q = strip.quantize(colors=n, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()[:3 * n]
    cols = [gba(tuple(pal[i:i + 3])) for i in range(0, len(pal), 3)]
    return cols


def save_indexed(path, frames, w, h, pal, bg=(115, 197, 164)):
    """frames stacked vertically into one indexed PNG (index 0 transparent)."""
    pal = [bg] + pal
    while len(pal) < 16:
        pal.append((0, 0, 0))
    out = Image.new("P", (w, h * len(frames)), 0)
    flat = [v for c in pal for v in c]
    out.putpalette(flat + [0] * (768 - len(flat)))
    op = out.load(); cache = {}
    for i, im in enumerate(frames):
        px = im.load()
        for y in range(h):
            for x in range(w):
                p = px[x, y]
                if p[3] >= 128:
                    k = p[:3]
                    if k not in cache:
                        cache[k] = 1 + min(range(15), key=lambda j: sum((k[c] - pal[1 + j][c]) ** 2 for c in range(3)))
                    op[x, y + i * h] = cache[k]
    out.save(path, transparency=0)


def keyed(path, bg, box=None):
    im = Image.open(path).convert("RGBA")
    if box:
        im = im.crop(box)
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            p = px[x, y]
            if abs(p[0] - bg[0]) + abs(p[1] - bg[1]) + abs(p[2] - bg[2]) < 24:
                px[x, y] = (0, 0, 0, 0)
    return im


def poses(im):
    """The three throwing poses: the three biggest figures, left to right."""
    w, h = im.size
    px = im.load()
    mask = [1 if px[x, y][3] >= 128 else 0 for y in range(h) for x in range(w)]
    comps = components(mask, w, h)
    height = lambda c: max(p[1] for p in c) - min(p[1] for p in c)
    tall = max(height(c) for c in comps)
    comps = sorted([c for c in comps if height(c) >= 0.6 * tall], key=len, reverse=True)[:3]
    comps.sort(key=lambda c: min(p[0] for p in c))
    out = []
    for c in comps:
        xs = [p[0] for p in c]; ys = [p[1] for p in c]
        x0, y0 = min(xs), min(ys)
        fig = Image.new("RGBA", (max(xs) - x0 + 1, max(ys) - y0 + 1))
        fp = fig.load()
        for x, y in c:
            fp[x - x0, y - y0] = px[x, y]
        out.append((fig, x0, y0))
    return out


def back_pic(name):
    fn, bg, s = BACKS[name]
    im = keyed(os.path.join(SRC, fn), bg)
    ps = poses(im)
    # give the standing pose its arm: holding the ball (frame 1), and hanging after the throw (frame 4)
    fig, x0, y0 = ps[0]
    for arms in (ARMS_BALL, ARMS):
        f = fig.copy()
        for box, dx, dy in arms[name]:
            f.alpha_composite(im.crop((box[0], box[1], box[2] + 1, box[3] + 1)), (dx, dy))
        ps.append((f, x0, y0))
    # (figure, x, y, the figure its placement is measured on: the armless pose for frames 1 and 4)
    ps = [ps[3] + (fig,), ps[1] + (ps[1][0],), ps[2] + (ps[2][0],), ps[4] + (fig,)]
    frames = []
    # the poses share the sheet's baseline: keep their vertical offsets; the head of the first pose
    # sits 12 pixels from the top (a crouching pose only half as far down). Horizontally each pose's
    # torso (the middle of its row at the bottom of the frame) stays put.
    base_y = min(p[2] for p in ps)
    for fig_, x0, y0, measure in ps:
        sm = downscale(fig_, s)
        oy = 12 + round((y0 - base_y) * s * 0.5)
        row = 63 - oy
        px = downscale(measure, s).load()
        xs = [x for x in range(sm.width) if 0 <= row < sm.height and px[x, row][3] >= 128]
        cx = (min(xs) + max(xs)) / 2 if xs else sm.width / 2
        ox = round(30 - cx)
        tmp = Image.new("RGBA", (64 + 2 * sm.width, 64 + sm.height))
        tmp.alpha_composite(sm, (sm.width + ox, oy))
        fr = tmp.crop((sm.width, 0, sm.width + 64, 64))
        frames.append(fr)
    pal = palette_of(frames)
    save_indexed(os.path.join(BACK, name + ".png"), frames, 64, 64, pal)


def fit64(im):
    """A figure into 64x64, feet at the bottom, shrunk only if it doesn't fit."""
    w, h = im.size
    px = im.load()
    mask = [1 if px[x, y][3] >= 128 else 0 for y in range(h) for x in range(w)]
    keep = max(components(mask, w, h), key=len)          # the figure, not stray bits of the sheet
    im = Image.new("RGBA", (w, h))
    fp = im.load()
    for x, y in keep:
        fp[x, y] = px[x, y]
    b = im.getbbox()
    im = im.crop(b)
    if im.width > 64 or im.height > 64:
        im = downscale(im, min(64 / im.width, 64 / im.height))
    fr = Image.new("RGBA", (64, 64))
    fr.alpha_composite(im, ((64 - im.width) // 2, 64 - im.height))
    return fr


def front_bw(rom_path, narc, k, bw2):
    import ndspy.rom, ndspy.narc
    from trainers import lz11, picture
    rom = ndspy.rom.NintendoDSRom.fromFile(rom_path)
    files = [bytes(f) for f in ndspy.narc.NARC(rom.getFileByName(narc)).files]
    pl = files[8 * k + 7]
    pal = [rgb555(struct.unpack_from("<H", pl, 16 + 0x18 + 2 * i)[0]) for i in range(16)]
    if not bw2:
        idx, _ = picture(files, k)
        w, h = 80, 80
        get = lambda x, y: idx[y * 80 + x]
    else:
        g = lz11(files[8 * k + 1])
        pd = g[48:]
        w, h = 256, 128
        get = lambda x, y: (pd[(y * 256 + x) // 2] >> 4) if x & 1 else (pd[(y * 256 + x) // 2] & 15)
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            v = get(x, y)
            if v:
                px[x, y] = pal[v] + (255,)
    if bw2:
        # the whole figure: the tallest part of the sheet
        mask = [1 if px[x, y][3] else 0 for y in range(h) for x in range(w)]
        c = max(components(mask, w, h), key=lambda c: max(p[1] for p in c) - min(p[1] for p in c))
        fig = Image.new("RGBA", (w, h)); fp = fig.load()
        for x, y in c:
            fp[x, y] = px[x, y]
        im = fig
    return fit64(im), [c for c in pal[1:]]


def write_front(name, im, pal=None):
    if pal is None:
        pal = palette_of([im])
    save_indexed(os.path.join(FRONT, name + ".png"), [im], 64, 64, pal)


def main():
    for n in BACKS:
        back_pic(n)
    lyra = keyed(os.path.join(SRC, "lyra_front.png"), (56, 144, 88))
    write_front("lyra", fit64(lyra))
    roms = sys.argv[1:]
    if len(roms) >= 1:
        for k, n in ((0, "hilbert"), (1, "hilda")):
            im, pal = front_bw(roms[0], "a/0/7/2", k, False)
            write_front(n, im, palette_of([im]))
    if len(roms) >= 2:
        for k, n in ((0, "nate"), (1, "rosa")):
            im, pal = front_bw(roms[1], "a/0/7/1", k, True)
            write_front(n, im, palette_of([im]))


if __name__ == "__main__":
    main()
