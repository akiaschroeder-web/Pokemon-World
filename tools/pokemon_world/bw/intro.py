"""Pokémon World: Pokémon Black's new-game introduction graphics (Professor Juniper's speech).

a/1/7/0 holds the intro's 2D and 3D pieces (found 2026-11-26):
  30 NANR  Juniper's per-part animations      31 NCGR  her parts (LZ11, 256x128, a linear bitmap)
  33 NCER  the part cells                     34 NCLR  her 16 colours
  35 NMAR  whole-figure animations: 0 idle / talking / blinking, 1 a short transition, 2 the Poké Ball toss
  36 NMCR  three poses (multi-cells)
  0/1 NCGR/NCLR with NSCR 3: the speech's background (white over dark grey); NSCR 2 is all dark
  2, 3 (+ 0, 1)  letterbox bars; 25/29 the 3D gender-select stage (not used: our setup picks the trainer)
A multi-cell part's tile number tn is pixel (tn % 32 * 8, tn // 32 * 8) of the bitmap; parts are drawn
last node first (node 0 on top); a node sits at its NMCR x/y plus its animation frame's x/y.

Writes graphics/unova/intro/ (raw GBA data, src/juniper_intro.c includes it):
  juniper.pal          her palette (16 x RGB555)
  juniper_<n>.tiles    (raw 4bpp, not .4bpp: that is gitignored) 64x128 frames in 32x32 blocks (8 blocks of 16 tiles: the sprite's subsprites)
                       0: pose 0 (holding the Poké Ball), 1..: the talking animation's frames
  juniper_frames.h     the frames (INCBIN) and the idle animation's order and timing (60 Hz ticks)
  juniper_text.h       her speech, from BW's text (a/0/0/2 file 44)
  bg.tiles / bg.pal / bg_map.bin   the background (BW's 256x256 screen, scrolled by the intro)
"""

import os
import struct

import ndspy.narc
from PIL import Image

OUT = 'graphics/unova/intro'
SHAPES = {(0, 0): (8, 8), (0, 1): (16, 16), (0, 2): (32, 32), (0, 3): (64, 64), (1, 0): (16, 8), (1, 1): (32, 8),
          (1, 2): (32, 16), (1, 3): (64, 32), (2, 0): (8, 16), (2, 1): (8, 32), (2, 2): (16, 32), (2, 3): (32, 64)}
W, H = 64, 128


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]


def lz11(d):
    out = bytearray()
    n = d[1] | d[2] << 8 | d[3] << 16
    i = 4
    if n == 0:
        n, i = u32(d, 4), 8
    while len(out) < n:
        flags = d[i]
        i += 1
        for b in range(8):
            if len(out) >= n:
                break
            if flags & (0x80 >> b):
                x = d[i] >> 4
                if x == 0:
                    ln, disp, i = ((d[i] & 15) << 4 | d[i + 1] >> 4) + 0x11, ((d[i + 1] & 15) << 8 | d[i + 2]) + 1, i + 3
                elif x == 1:
                    ln = ((d[i] & 15) << 12 | d[i + 1] << 4 | d[i + 2] >> 4) + 0x111
                    disp, i = ((d[i + 2] & 15) << 8 | d[i + 3]) + 1, i + 4
                else:
                    ln, disp, i = x + 1, ((d[i] & 15) << 8 | d[i + 1]) + 1, i + 2
                for _ in range(ln):
                    out.append(out[-disp])
            else:
                out.append(d[i])
                i += 1
    return bytes(out)


def dec(f):
    f = bytes(f)
    return lz11(f) if f[:1] == b'\x11' else f


def blk(f, magic):
    o = u16(f, 0xC)
    for _ in range(u16(f, 0xE)):
        if f[o:o + 4] == magic:
            return o
        o += u32(f, o + 4)
    return None


def ncgr(g):
    o = blk(g, b'RAHC')
    th, tw, depth = u16(g, o + 8), u16(g, o + 10), u32(g, o + 12)
    size, off = u32(g, o + 0x18), u32(g, o + 0x1C)
    return {'tw': tw, 'bpp': 4 if depth == 3 else 8, 'data': g[o + 8 + off:o + 8 + off + size]}


def nclr(p):
    o = blk(p, b'TTLP')
    size, off = u32(p, o + 0x10), u32(p, o + 0x14)
    d = p[o + 8 + off:o + 8 + off + size]
    return [u16(d, i) for i in range(0, len(d) - 1, 2)]


def cells(c):
    o = blk(c, b'KBEC')
    n, bt = u16(c, o + 8), u16(c, o + 10)
    co = o + 8 + u32(c, o + 12)
    out = []
    for i in range(n):
        no, _, oo = struct.unpack_from('<HHI', c, co + i * (16 if bt else 8))
        base = co + n * (16 if bt else 8) + oo
        out.append([struct.unpack_from('<3H', c, base + 6 * j) for j in range(no)])
    return out


def nanr(f):
    b = blk(f, b'KNBA') + 8
    nseq = u16(f, b)
    so, fo, po = u32(f, b + 4), u32(f, b + 8), u32(f, b + 12)
    seqs = []
    for i in range(nseq):
        n, _, typ, _, _, foff = struct.unpack_from('<HHHHII', f, b + so + 16 * i)
        frames = []
        for j in range(n):
            doff, dur = struct.unpack_from('<IH', f, b + fo + foff + 8 * j)
            de = b + po + doff
            if typ & 0xFF == 0:
                frames.append({'cell': u16(f, de), 'x': 0, 'y': 0, 'dur': dur})
            elif typ & 0xFF == 1:
                idx, rot, sx, sy, x, y = struct.unpack_from('<HHiihh', f, de)
                frames.append({'cell': idx, 'rot': rot, 'sx': sx, 'sy': sy, 'x': x, 'y': y, 'dur': dur})
            else:
                idx, _, x, y = struct.unpack_from('<HHhh', f, de)
                frames.append({'cell': idx, 'x': x, 'y': y, 'dur': dur})
        seqs.append(frames)
    return seqs


def nmcr(f):
    b = blk(f, b'KBCM') + 8
    n, mo, no = u16(f, b), u32(f, b + 4), u32(f, b + 8)
    out = []
    for i in range(n):
        nn, _, noff = struct.unpack_from('<HHI', f, b + mo + 8 * i)
        out.append([struct.unpack_from('<HhhH', f, b + no + noff + 8 * j) for j in range(nn)])
    return out


def frame_at(frames, t):
    total = sum(fr['dur'] for fr in frames) or 1
    t %= total
    for fr in frames:
        if t < fr['dur']:
            return fr
        t -= fr['dur']
    return frames[-1]


class Juniper:
    def __init__(self, fs):
        g = ncgr(fs[31])
        d = g['data']
        self.bm = bytearray(len(d) * 2)
        for i, v in enumerate(d):
            self.bm[2 * i], self.bm[2 * i + 1] = v & 15, v >> 4
        self.bw = 256 if g['tw'] == 0xFFFF else g['tw'] * 8
        self.pal = nclr(fs[34])[:16]
        self.cells = cells(fs[33])
        self.seqs = nanr(fs[30])
        self.poses = nmcr(fs[36])
        self.anims = nanr(fs[35])

    def compose(self, pose, t=None, size=256):
        """palette indices (size x size, figure centred at size/2) of a pose at time t (None: frame 0)"""
        img = [0] * (size * size)
        for seq, nx, ny, _ in reversed(self.poses[pose]):
            fr = self.seqs[seq][0] if t is None else frame_at(self.seqs[seq], t)
            layer = Image.new('P', (size, size), 0)
            px = layer.load()
            for a0, a1, a2 in reversed(self.cells[fr['cell']]):
                y = a0 & 0xFF
                y = y - 256 if y >= 128 else y
                x = a1 & 0x1FF
                x = x - 512 if x >= 256 else x
                w, h = SHAPES[(a0 >> 14, a1 >> 14)]
                rs = (a0 >> 8) & 1
                hf, vf = (a1 >> 12) & 1 and not rs, (a1 >> 13) & 1 and not rs
                tn = a2 & 0x3FF
                bx, by = tn % 32 * 8, tn // 32 * 8
                for yy in range(h):
                    for xx in range(w):
                        i = (by + yy) * self.bw + bx + xx
                        v = self.bm[i] if i < len(self.bm) else 0
                        if v:
                            dx, dy = (w - 1 - xx) if hf else xx, (h - 1 - yy) if vf else yy
                            X, Y = size // 2 + x + dx, size // 2 + y + dy
                            if 0 <= X < size and 0 <= Y < size:
                                px[X, Y] = v
            if fr.get('rot'):
                layer = layer.rotate(fr['rot'] * 360 / 65536, resample=Image.NEAREST, center=(size // 2, size // 2))
            sx, sy = fr.get('sx', 4096), fr.get('sy', 4096)
            if (sx, sy) != (4096, 4096):
                fx, fy, c = sx / 4096, sy / 4096, size // 2
                layer = layer.transform((size, size), Image.AFFINE, (1 / fx, 0, c - c / fx, 0, 1 / fy, c - c / fy),
                                        resample=Image.NEAREST)
            data = layer.load()
            ox, oy = nx + fr['x'], ny + fr['y']
            for Y in range(size):
                for X in range(size):
                    v = data[X, Y]
                    if v and 0 <= X + ox < size and 0 <= Y + oy < size:
                        img[(Y + oy) * size + X + ox] = v
        return img


def crop_box(images, size=256):
    xs, ys = [], []
    for img in images:
        for i, v in enumerate(img):
            if v:
                xs.append(i % size)
                ys.append(i // size)
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def to_blocks(img, size, box):
    """64x128 frame (figure centred horizontally, feet at the bottom) -> 4bpp in 32x32 blocks"""
    x0, y0, x1, y1 = box
    ox = x0 - (W - (x1 - x0)) // 2
    oy = y1 - H
    out = bytearray()
    for by in range(H // 32):
        for bx in range(W // 32):
            for ty in range(4):
                for tx in range(4):
                    for yy in range(8):
                        for xx in range(0, 8, 2):
                            def at(px, py):
                                X, Y = ox + px, oy + py
                                return img[Y * size + X] if 0 <= X < size and 0 <= Y < size else 0
                            px, py = bx * 32 + tx * 8 + xx, by * 32 + ty * 8 + yy
                            out.append(at(px, py) | at(px + 1, py) << 4)
    return bytes(out)


def background(fs):
    """BW's speech background, NSCR 3 (NCGR 0, NCLR 1): a 256x256 screen, white above and dark grey
    below with a gradient between; the intro scrolls it (dark -> white). The whole screen, as tiles."""
    f = fs[3]
    o = blk(f, b'NRCS')
    w, h, size = u16(f, o + 8), u16(f, o + 10), u32(f, o + 0x10)
    ent = [u16(f, o + 0x14 + 2 * i) for i in range(size // 2)]
    g = ncgr(fs[0])
    pal = nclr(fs[1])
    tiles, tw = g['data'], w // 8
    img = [[0] * w for _ in range(h)]
    for Y in range(h):
        for X in range(w):
            e = ent[(Y // 8) * tw + X // 8]
            t, hf, vf, p = e & 0x3FF, e & 0x400, e & 0x800, e >> 12
            xx, yy = (7 - X % 8) if hf else X % 8, (7 - Y % 8) if vf else Y % 8
            if g['bpp'] == 4:
                q = t * 32 + yy * 4 + xx // 2
                v = tiles[q] if q < len(tiles) else 0
                img[Y][X] = p * 16 + ((v >> 4) if xx & 1 else v & 15)
            else:
                img[Y][X] = tiles[t * 64 + yy * 8 + xx]
    colours = sorted({v for row in img for v in row})
    if len(colours) > 16:
        raise SystemExit('intro background: %d colours' % len(colours))
    remap = {c: i for i, c in enumerate(colours)}
    bgpal = [pal[c] if c < len(pal) else 0 for c in colours] + [0] * (16 - len(colours))
    tile_ids, tile_data, tmap = {}, bytearray(), [0] * (32 * 32)
    for ty in range(h // 8):
        for tx in range(w // 8):
            t = bytes(remap[img[ty * 8 + yy][tx * 8 + xx]] | remap[img[ty * 8 + yy][tx * 8 + xx + 1]] << 4
                      for yy in range(8) for xx in range(0, 8, 2))
            if t not in tile_ids:
                tile_ids[t] = len(tile_ids)
                tile_data += t
            tmap[ty * 32 + tx] = tile_ids[t]
    return bytes(tile_data), bgpal, tmap


# Juniper's speech: a/0/0/2 file 44 (BW's order; 11-13, the boy/girl question, are left out: our
# setup has chosen the trainer already)
TEXT_FILE = 44
TEXT_IDS = [6, 7, 8, 9, 10, 16, 21, 22, 23, 24, 25, 26]


def write_text(rom):
    import import_bw
    import scripts
    msgs = import_bw.decode_text(rom.narc('a/0/0/2')[TEXT_FILE])
    with open(OUT + '/juniper_text.h', 'w', encoding='utf-8') as f:
        f.write('// Generated by tools/pokemon_world/bw/intro.py: Professor Juniper\'s speech, Pokémon Black\'s text\n'
                '// (a/0/0/2 file %d, message n = sJuniperText_n).\n' % TEXT_FILE)
        for n in TEXT_IDS:
            body = scripts.to_script_string(msgs[n]).replace('"', '\\"')
            f.write('static const u8 sJuniperText_%d[] = _("%s");\n' % (n, body))


def write(rom):
    fs = [dec(f) for f in ndspy.narc.NARC(rom.rom.getFileByName('a/1/7/0')).files]
    os.makedirs(OUT, exist_ok=True)
    j = Juniper(fs)
    S = 256
    # the talking animation: its distinct frames, in order, with their lengths
    talk = j.anims[0]
    total = sum(fr['dur'] for fr in talk)
    frames, order = [], []
    for t in range(0, total, 2):
        img = j.compose(frame_at(talk, t)['cell'], t, S)
        if img not in frames:
            frames.append(img)
        k = frames.index(img)
        if order and order[-1][0] == k:
            order[-1][1] += 2
        else:
            order.append([k, 2])
    pose0 = j.compose(0, None, S)
    images = [pose0] + frames
    box = crop_box(images, S)
    if box[2] - box[0] > W or box[3] - box[1] > H:
        raise SystemExit('Juniper is %dx%d, more than %dx%d' % (box[2] - box[0], box[3] - box[1], W, H))
    with open(OUT + '/juniper.pal', 'wb') as f:
        f.write(struct.pack('<16H', *j.pal))
    for n, img in enumerate(images):
        with open('%s/juniper_%d.tiles' % (OUT, n), 'wb') as f:
            f.write(to_blocks(img, S, box))
    with open(OUT + '/juniper_frames.h', 'w') as f:
        f.write('// Generated by tools/pokemon_world/bw/intro.py: Juniper\'s frames (0: pose 0, then her idle\n'
                '// animation, BW NMAR 0: tossing the Poké Ball) and that animation: {frame, BW ticks (60/s)}\n')
        f.write('#define JUNIPER_FRAME_COUNT %d\n' % len(images))
        for n in range(len(images)):
            f.write('static const u32 sJuniperFrame%d[] = INCBIN_U32("%s/juniper_%d.tiles");\n' % (n, OUT, n))
        f.write('static const u32 *const sJuniperFrames[] = {%s};\n' % ', '.join('sJuniperFrame%d' % n for n in range(len(images))))
        f.write('static const u8 sJuniperIdle[][2] = {%s};\n' % ', '.join('{%d, %d}' % (k + 1, d) for k, d in order))
    write_text(rom)
    tiles, bgpal, tmap = background(fs)
    open(OUT + '/bg.tiles', 'wb').write(tiles)
    open(OUT + '/bg.pal', 'wb').write(struct.pack('<16H', *bgpal))
    open(OUT + '/bg_map.bin', 'wb').write(struct.pack('<1024H', *tmap))
    print('intro: Juniper %dx%d, %d frames (talking: %d steps), background %d tiles'
          % (box[2] - box[0], box[3] - box[1], len(images), len(order), len(tiles) // 32))
