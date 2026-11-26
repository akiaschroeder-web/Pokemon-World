#!/usr/bin/env python3
"""Pokémon World: overworld sprite sets for the extra player designs (Hilbert, Hilda, Lyra, Nate, Rosa).

The owner supplied Gen 3 style walking sheets (graphics/object_events/pics/people/<name>/source.png:
a 4x4 grid of 16x24 frames, rows down/left/right/up, columns stand/step/stand/step). Every other
animation (running, bikes, surfing, field move, underwater, fishing, watering, decorating) is
transplanted from a base character's sheet: the base's head is found in each frame by matching it
against the base's own standing heads, the body is recoloured into the new character's colours
(learnt by overlaying the two characters' standing frames), and the new head for that direction is
pasted on. The result is quantized to one 15-colour palette (+ transparent) and a reflection palette.

    python3 tools/pokemon_world/new_designs.py [name ...]
"""
import math
import os
import sys
from collections import Counter, defaultdict

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PEOPLE = os.path.join(ROOT, "graphics/object_events/pics/people")
PALS = os.path.join(ROOT, "graphics/object_events/palettes")

T = (0, 0, 0, 0)

# name -> (base for most animations, base for watering/decorating, which also covers acro wheelies
# when the main base lacks them)
DESIGNS = {
    "hilbert": ("ethan_hns", "brendan"),
    "hilda":   ("kris_hns", "may"),
    "lyra":    ("kris_hns", "may"),
    "nate":    ("ethan_hns", "brendan"),
    "rosa":    ("kris_hns", "may"),
}

# animation -> frame width (all frames are 32 high)
ANIMS = {
    "running": 16, "mach_bike": 32, "acro_bike": 32, "surfing": 32, "field_move": 32,
    "underwater": 32, "fishing": 32, "watering": 32, "decorating": 16,
}
SECOND_BASE = {"watering", "decorating"}

# Gen 3 walking order: down, up, left standing; down x2, up x2, left x2 steps.
# Source sheet rows: 0 down, 1 left, 2 right, 3 up; columns 0/2 stand, 1/3 step.
WALK_ORDER = [(0, 0), (3, 0), (1, 0), (0, 1), (0, 3), (3, 1), (3, 3), (1, 1), (1, 3)]
DIRS = {"down": 0, "up": 1, "left": 2}   # standing frame index in a walking sheet
FEET = 30                                 # bottom opaque row of a 16x32 walking frame


def load_idx(path):
    """An indexed PNG as RGBA, index 0 transparent."""
    im = Image.open(path)
    if im.mode != "P":
        return im.convert("RGBA")
    ip = im.load()
    out = im.convert("RGBA")
    px = out.load()
    for y in range(im.height):
        for x in range(im.width):
            if ip[x, y] == 0:
                px[x, y] = T
    return out


def frames(im, w):
    return [im.crop((i * w, 0, i * w + w, im.height)) for i in range(im.width // w)]


def opaque(p):
    return p[3] >= 128


def bbox(im):
    px = im.load()
    pts = [(x, y) for y in range(im.height) for x in range(im.width) if opaque(px[x, y])]
    if not pts:
        return None
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def mirror(im):
    return im.transpose(Image.FLIP_LEFT_RIGHT)


class Head:
    """A character's head for one facing: the pixels from the top of the standing frame down to the neck."""

    def __init__(self, frame, neck):
        b = bbox(frame)
        self.top = b[1]
        self.neck = neck                     # first body row (exclusive end of the head)
        self.pixels = {}
        px = frame.load()
        for y in range(self.top, neck):
            for x in range(frame.width):
                if opaque(px[x, y]):
                    self.pixels[(x, y)] = px[x, y][:3]
        xs = [p[0] for p in self.pixels]
        self.cx = (min(xs) + max(xs) + 1) / 2

    def mirrored(self, width):
        h = Head.__new__(Head)
        h.top, h.neck = self.top, self.neck
        h.pixels = {(width - 1 - x, y): c for (x, y), c in self.pixels.items()}
        h.cx = width - self.cx
        return h


def base_heads(base, necks):
    walk = frames(load_idx(os.path.join(PEOPLE, base, "walking.png")), 16)
    heads = {}
    for d, i in DIRS.items():
        heads[d] = Head(walk[i], necks[d])
    heads["right"] = heads["left"].mirrored(16)
    return heads, walk


def locate(frame, heads):
    """Best (direction, dx, dy, score) placing one of `heads` (16-wide coordinates) in `frame`."""
    px = frame.load()
    best = None
    for d, h in heads.items():
        n = len(h.pixels)
        for dy in range(-12, frame.height - h.top):
            for dx in range(-8, frame.width):
                hit = 0
                miss = 0
                for (x, y), c in h.pixels.items():
                    X, Y = x + dx, y + dy
                    if 0 <= X < frame.width and 0 <= Y < frame.height:
                        p = px[X, Y]
                        if opaque(p) and p[:3] == c:
                            hit += 1
                        elif not opaque(p):
                            miss += 1
                score = (hit - 0.5 * miss) / n
                if best is None or score > best[3]:
                    best = (d, dx, dy, score)
    return best


def colour_map(base_walk, base_necks, new_walk, new_necks):
    """Base colour -> new colour, from the bodies (below the neck) of the standing frames,
    bottom-aligned and centred."""
    co = defaultdict(Counter)
    for d, i in DIRS.items():
        a = base_walk[i]; b = new_walk[i]
        ab, bb = bbox(a), bbox(b)
        ox = round(((bb[0] + bb[2]) - (ab[0] + ab[2])) / 2)
        oy = bb[3] - ab[3]
        pa, pb = a.load(), b.load()
        for y in range(base_necks[d], 32):
            for x in range(16):
                p = pa[x, y]
                X, Y = x + ox, y + oy
                if not opaque(p) or not (0 <= X < 16 and 0 <= Y < 32) or Y < new_necks[d]:
                    continue
                q = pb[X, Y]
                if opaque(q):
                    co[p[:3]][q[:3]] += 1
    m = {}
    for c, cnt in co.items():
        m[c] = cnt.most_common(1)[0][0]
    return m


def transplant(frame, base_heads_, new_heads, cmap, min_score=0.35):
    """Recolour `frame` and swap the base head for the new one. Returns (image, placement)."""
    w, h = frame.size
    loc = locate(frame, base_heads_)
    out = Image.new("RGBA", (w, h), T)
    src = frame.load(); dst = out.load()
    for y in range(h):
        for x in range(w):
            p = src[x, y]
            if opaque(p):
                c = cmap.get(p[:3], p[:3])
                dst[x, y] = c + (255,)
    if loc is None or loc[3] < min_score:
        return out, loc
    d, dx, dy, score = loc
    bh = base_heads_[d]; nh = new_heads[d]
    # erase the base head where it is actually drawn
    # (also where a pose draws it a little differently, e.g. field move: any head colour in its outline)
    head_cols = set(bh.pixels.values())
    for (x, y), c in bh.pixels.items():
        X, Y = x + dx, y + dy
        if 0 <= X < w and 0 <= Y < h and opaque(src[X, Y]) and src[X, Y][:3] in head_cols:
            dst[X, Y] = T
    # paste the new head, its neck on the base head's neck, centred on it
    ox = dx + round(bh.cx - nh.cx)
    oy = dy + (bh.neck - nh.neck)
    for (x, y), c in nh.pixels.items():
        X, Y = x + ox, y + oy
        if 0 <= X < w and 0 <= Y < h:
            dst[X, Y] = c + (255,)
    # close one-pixel gaps the old head leaves between the new head and the body
    for (x, y) in bh.pixels:
        X, Y = x + dx, y + dy
        if 0 <= X < w and 0 < Y < h - 1 and not opaque(dst[X, Y]) and opaque(dst[X, Y - 1]) and opaque(dst[X, Y + 1]):
            dst[X, Y] = dst[X, Y + 1]
    return out, loc


NECK = 23   # first body row of a 16x32 standing frame, for every base and new character


def new_walking(name):
    """The new character's 9 walking frames (16x32) from its source sheet."""
    src = Image.open(os.path.join(PEOPLE, name, "source.png")).convert("RGBA")
    out = []
    for r, c in WALK_ORDER:
        cell = src.crop((c * 16, r * 24, c * 16 + 16, r * 24 + 24))
        b = bbox(cell)
        im = Image.new("RGBA", (16, 32), T)
        im.alpha_composite(cell, (0, FEET - b[3]))
        out.append(im)
    return out


def extend_head(head, frame):
    """Long hair (ponytails, twin tails) hangs below the neck: add hair-coloured pixels below the
    neck that join the head through hair of the same colour, plus the outline around them."""
    px = frame.load()
    above = Counter(); below = Counter()
    for y in range(32):
        for x in range(16):
            p = px[x, y]
            if opaque(p):
                (above if y < head.neck else below)[p[:3]] += 1
    hair = {c for c, n in above.items() if n >= 6 and n >= 3 * below.get(c, 0) and sum(c) > 60}
    added = {}
    frontier = [(x, y) for (x, y), c in head.pixels.items() if c in hair]
    seen = set(frontier)
    while frontier:
        x, y = frontier.pop()
        for X, Y in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if (X, Y) in seen or not (0 <= X < 16 and head.neck <= Y < 32):
                continue
            seen.add((X, Y))
            p = px[X, Y]
            if opaque(p) and p[:3] in hair:
                added[(X, Y)] = p[:3]
                frontier.append((X, Y))
    dark = min(above, key=lambda c: sum(c)) if above else None
    for (x, y) in list(added):
        for X, Y in ((x + 1, y), (x - 1, y), (x, y + 1), (x + 1, y + 1), (x - 1, y + 1)):
            if (X, Y) not in added and 0 <= X < 16 and Y < 32 and opaque(px[X, Y]) and px[X, Y][:3] == dark:
                added[(X, Y)] = dark
    head.pixels.update(added)
    return head


def heads_from(walk, extend=False):
    hs = {}
    for d, i in DIRS.items():
        h = Head(walk[i], NECK)
        if extend:
            extend_head(h, walk[i])
        hs[d] = h
    hs["right"] = hs["left"].mirrored(16)
    return hs


def gba(c):
    return tuple(v & 0xF8 for v in c)


def quantize(colours, n=15):
    """Greedy weighted merge of an {rgb: count} histogram down to n colours."""
    cl = [[list(c), w] for c, w in colours.items()]
    while len(cl) > n:
        best = None
        for i in range(len(cl)):
            for j in range(i + 1, len(cl)):
                a, b = cl[i], cl[j]
                d = sum((a[0][k] - b[0][k]) ** 2 for k in range(3))
                cost = d * min(a[1], b[1])
                if best is None or cost < best[0]:
                    best = (cost, i, j)
        _, i, j = best
        a, b = cl[i], cl[j]
        w = a[1] + b[1]
        # keep the heavier colour exactly when one dominates (outline, skin), else average
        if max(a[1], b[1]) >= 3 * min(a[1], b[1]):
            c = a[0] if a[1] >= b[1] else b[0]
        else:
            c = [(a[0][k] * a[1] + b[0][k] * b[1]) / w for k in range(3)]
        cl[i] = [c, w]
        del cl[j]
    return [gba(tuple(int(round(v)) for v in c)) for c, _ in cl]


def nearest(c, pal):
    return min(range(len(pal)), key=lambda i: sum((c[k] - pal[i][k]) ** 2 for k in range(3)))


def reflection(pal):
    """Reflection colours: the per-channel fit between Ethan's two palettes (lighter, blue-green)."""
    def rd(p):
        return [tuple(map(int, l.split())) for l in open(p).read().splitlines()[3:19]]
    a = rd(os.path.join(PALS, "ethan_hns.pal")); b = rd(os.path.join(PALS, "ethan_hns_reflection.pal"))
    pairs = [(a[i], b[i]) for i in range(1, 16) if i not in (7, 8)]
    fit = []
    for k in range(3):
        xs = [p[0][k] for p in pairs]; ys = [p[1][k] for p in pairs]
        mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
        sl = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
        fit.append((sl, my - sl * mx))
    return [tuple(max(0, min(255, int(round(c[k] * fit[k][0] + fit[k][1])))) for k in range(3)) for c in pal]


def write_pal(path, pal):
    with open(path, "w", newline="\r\n") as f:
        f.write("JASC-PAL\n0100\n16\n")
        for c in pal:
            f.write("%d %d %d\n" % c)


def save_indexed(path, im, pal):
    out = Image.new("P", im.size, 0)
    flat = []
    for c in pal:
        flat += list(c)
    out.putpalette(flat + [0] * (768 - len(flat)))
    px = im.load(); op = out.load()
    cache = {}
    for y in range(im.height):
        for x in range(im.width):
            p = px[x, y]
            if opaque(p):
                if p[:3] not in cache:
                    cache[p[:3]] = 1 + nearest(p[:3], pal[1:])
                op[x, y] = cache[p[:3]]
    out.save(path, transparency=0)


# Diving: the sprite is a silhouette seen from behind, drawn in the shared underwater palette
# (graphics/object_events/palettes/player_underwater.pal) rather than the character's own:
# 15 the outline, then 9, 5, 6, 7, 8 from the lightest blue to the darkest.
UW_OUTLINE = 15
UW_SHADES = [9, 5, 6, 7, 8]
UW_FACING = ["down", "up", "left", "left"]


def uw_shade(c, outline):
    """A colour of the new character's head as an underwater index, by brightness."""
    if c == outline:
        return UW_OUTLINE
    lum = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
    if lum < 40:
        return UW_OUTLINE
    # 230+ lightest ... under 70 darkest
    k = int((230 - lum) / 40)
    return UW_SHADES[max(0, min(len(UW_SHADES) - 1, k))]


def underwater(name, base2, walk, new_heads):
    """The diving frames: the original Gen 3 frames of `base2` (Brendan or May) with their head
    silhouette replaced by the new character's back-of-head, shaded in the underwater blues.
    Returns an indexed image (underwater palette indices)."""
    src = Image.open(os.path.join(PEOPLE, base2, "underwater.png"))
    assert src.mode == "P"
    out = src.copy()
    sp = src.load(); op = out.load()
    for f in range(src.width // 32):
        d = UW_FACING[f]                     # the frames face down, up, left, left
        nh = new_heads[d]
        outline = min(Counter(nh.pixels.values()), key=lambda c: sum(c))
        x0 = f * 32
        rows = {}
        for y in range(32):
            xs = [x for x in range(32) if sp[x0 + x, y]]
            if xs:
                rows[y] = (min(xs), max(xs))
        top = min(rows)
        # the dive frames draw a smaller head than walking does: the head is everything above the
        # shoulders, the first row (a few below the top) that spans 16 pixels or more
        shoulders = next(y for y in sorted(rows) if y > top + 4 and rows[y][1] - rows[y][0] + 1 >= 16)
        hx = [x for y in range(top, shoulders) for x in rows[y]]
        cx = (min(hx) + max(hx) + 1) / 2
        for y in range(top, shoulders):
            for x in range(32):
                op[x0 + x, y] = 0
        # the new head: its neck a row into the shoulders, centred on the old head
        ox = round(cx - nh.cx)
        oy = shoulders + 1 - nh.neck
        for (x, y), c in nh.pixels.items():
            X, Y = x + ox, y + oy
            if 0 <= X < 32 and 0 <= Y < 32 and (Y < shoulders + 1 or not sp[x0 + X, Y]):
                op[x0 + X, Y] = uw_shade(c, outline)
        # close one-pixel gaps between the new head and the shoulders
        for y in range(top, shoulders + 1):
            for x in range(1, 31):
                if op[x0 + x, y] == 0 and 0 < y < 31 and op[x0 + x, y - 1] and op[x0 + x, y + 1]:
                    op[x0 + x, y] = UW_OUTLINE
    return out


def build(name, report=None, write=True):
    base, base2 = DESIGNS[name]
    walk = new_walking(name)
    new_heads = heads_from(walk, extend=True)
    sets = {}
    necks = {d: NECK for d in DIRS}
    for b in (base, base2):
        bw = frames(load_idx(os.path.join(PEOPLE, b, "walking.png")), 16)
        sets[b] = (heads_from(bw), colour_map(bw, necks, walk, necks))
    sheets = {}
    log = []
    for anim, w in ANIMS.items():
        if anim == "underwater":
            continue                      # its own palette: see underwater()
        main = base2 if anim in SECOND_BASE else base
        fr_main = frames(load_idx(os.path.join(PEOPLE, main, anim + ".png")), w)
        fr_alt = frames(load_idx(os.path.join(PEOPLE, base2, anim + ".png")), w)
        out = []
        for i, f in enumerate(fr_main):
            b = main
            if bbox(f) is None and i < len(fr_alt) and bbox(fr_alt[i]) is not None:
                f, b = fr_alt[i], base2   # e.g. Ethan's sheet has no acro bike wheelies
            if bbox(f) is None:
                out.append(f); log.append((anim, i, b, None)); continue
            bh, cmap = sets[b]
            im, loc = transplant(f, bh, new_heads, cmap)
            if (loc is None or loc[3] < 0.35) and b != base2 and i < len(fr_alt) and bbox(fr_alt[i]) is not None:
                # this pose draws the base's head differently (Kris's acro wheelies): use the
                # second base's frame if its head can be found
                bh2, cmap2 = sets[base2]
                im2, loc2 = transplant(fr_alt[i], bh2, new_heads, cmap2)
                if loc2 is not None and loc2[3] >= 0.35:
                    im, loc, b = im2, loc2, base2
            out.append(im); log.append((anim, i, b, loc))
        sheet = Image.new("RGBA", (w * len(out), 32), T)
        for i, im in enumerate(out):
            sheet.alpha_composite(im, (i * w, 0))
        sheets[anim] = sheet
    sheets["walking"] = Image.new("RGBA", (16 * 9, 32), T)
    for i, im in enumerate(walk):
        sheets["walking"].alpha_composite(im, (i * 16, 0))
    # one palette for everything; the walking frames count most
    hist = Counter()
    for anim, sh in sheets.items():
        wgt = 6 if anim == "walking" else 1
        for p in sh.getdata():
            if opaque(p):
                hist[gba(p[:3])] += wgt
    pal = [(115, 197, 164)] + quantize(hist)
    while len(pal) < 16:
        pal.append((0, 0, 0))
    if write:
        d = os.path.join(PEOPLE, name)
        for anim, sh in sheets.items():
            save_indexed(os.path.join(d, anim + ".png"), sh, pal)
        uw = underwater(name, base2, walk, new_heads)
        uw.save(os.path.join(d, "underwater.png"), transparency=0)
        sheets["underwater"] = uw
        # the region map's player icon: the head of the down-facing standing frame
        top = bbox(walk[0])[1]
        icon = walk[0].crop((0, top, 16, top + 16))
        save_indexed(os.path.join(ROOT, "graphics/pokenav/region_map", name + "_icon.png"), icon, pal)
        write_pal(os.path.join(PALS, name + ".pal"), pal)
        write_pal(os.path.join(PALS, name + "_reflection.pal"), [pal[0]] + reflection(pal[1:]))
    if report is not None:
        report[name] = log
    return sheets, pal


if __name__ == "__main__":
    names = sys.argv[1:] or list(DESIGNS)
    for n in names:
        rep = {}
        build(n, rep)
        bad = [(a, i) for a, i, b, l in rep[n] if l is not None and l[3] < 0.35]
        print(n, "done;", len(bad), "frames without a head match:", bad)
