"""Pokémon World: Pokémon Black's 3D cutscenes (Call3DDemo) -> files the web build plays.

BW's 3D demos are in a/1/6/0: per demo, its models (BMD0) with their animations - joints (BCA0),
visibility (BVA0), texture patterns (BTP0), texture scrolling (BTA0), material colours (BMA0, not
used yet) - and, last, its camera: u32 frame count, u32 (flags), then per frame f32 x 6.
Formats follow apicula (scurest/apicula: nitro/animation.rs, pattern.rs, material_animation.rs,
rotation.rs). Demo ids are matched to resource groups by where BW's scripts call them (DEMOS).

Output, graphics/unova/demos/:
  d<id>.json     {frames, camera: [[6 floats]...], models: [{mesh, textures, draws: [[bone ids]],
                  frames_file, bones, patterns: {group: [[frame, texture]...]}, scroll: {...}}]}
  d<id>_<k>.bin  the model's mesh (PWM2, bone slot in each vertex's spare field)
  d<id>_<k>.anim Float32: per baked frame, per bone, 12 values (a 3x4 matrix, rows), every STEP frames
  tex/*.png
The game side: src/bw_script.c (Call3DDemo) -> src/platform/web3d.c (scene.demo) ->
platform/web/demo3d.js.
"""

import json
import math
import os
import struct

import export3d
import nitro

OUT = 'graphics/unova/demos'
STEP = 2               # baked joint frames: every 2nd frame (the page interpolates)
MAX_BONES = 60         # per draw call (WebGL 1 uniform space)

# demo id -> the name its resource group's files start with (a/1/6/0), from the zones whose
# scripts call it: Royal Unova, Nimbasa, Castelia's harbour, the League, N's Castle, Dragonspiral
# Tower, Route 5 (the Driftveil Drawbridge)
INTRO_DEMO = 100       # our id for it (src/bw_script.c: BW_INTRO_DEMO)
ONLY_PREFIX = {INTRO_DEMO}
DEMOS = {2: 'c_cruiser_port', 3: 'f_wheel', 4: 'g_ship_port', 6: 'n_demo',
         # N's Castle: the player's legendary awakens (Black: Reshiram), then meets N's
         9: 'n_legend_born_b_', 10: 'n_legend_born_w_', 11: 'n_legend_meat_b_', 12: 'n_legend_meat_w_',
         # Dragonspiral Tower: N's legendary arrives (Black: Zekrom)
         13: 'n_legend_come_b_', 14: 'n_legend_come_w_', 15: 'riza_bridge', 16: 'v_boat_port',
         # not a script's: the new game's flight over Nuvema Town to the player's house (BW's intro
         # plays it after Juniper's speech); its group also holds another demo's models, so only
         # the intro_town ones are taken (ONLY_PREFIX)
         INTRO_DEMO: 'intro_town'}


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def s16(b, o): return struct.unpack_from('<h', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def fx32(v): return (v - (1 << 32) if v & 0x80000000 else v) / 4096.0
def fx16(v): return (v - (1 << 16) if v & 0x8000 else v) / 4096.0


def first_name(f):
    """The first dictionary name in a NITRO file (BMD0: its model, BCA0: its animation...)."""
    try:
        for magic, off in nitro.blocks(f).items():
            if magic == b'TEX0':
                continue
            return nitro.read_dict(f, off + 8)[0][0]
    except Exception:
        return None


# ---------------------------------------------------------------- animations

def _curve(base, info, off, wide_scale=False, pair=False):
    start, end = info & 0xFFFF, (info >> 16) & 0xFFF
    width, rate = (info >> 28) & 3, (info >> 30) & 3
    n = max(0, (end - start) >> rate)
    vals = []
    for k in range(n):
        if width == 0:
            v = fx32(u32(base, off + k * (8 if pair else 4)))
        else:
            v = fx16(u16(base, off + k * (4 if pair else 2)))
        vals.append(v)
    return ('samples', start, end, vals)


def _sample(curve, default, frame):
    if curve is None:
        return default
    if curve[0] == 'const':
        return curve[1]
    _, start, end, vals = curve
    if not vals:
        return default
    if frame <= start:
        return vals[0]
    if frame >= end - 1:
        return vals[-1]
    lam = (frame - start) / max(1, end - 1 - start)
    idx = lam * (len(vals) - 1)
    lo = int(math.floor(idx)); hi = min(len(vals) - 1, lo + 1); g = idx - lo
    a, b = vals[lo], vals[hi]
    if isinstance(a, list):
        return [[a[r][c] * (1 - g) + b[r][c] * g for c in range(3)] for r in range(3)]
    return a * (1 - g) + b * g


def _pivot(sel, neg, a, b):
    if sel >= 9:
        return [[-a, 0, 0], [0, 0, 0], [0, 0, 0]]
    o = -1.0 if neg & 1 else 1.0
    c = -b if neg & 2 else b
    d = -a if neg & 4 else a
    # apicula's Matrix3::new takes columns; written here as rows (transposed)
    cols = {0: (o, 0, 0, 0, a, b, 0, c, d), 1: (0, o, 0, a, 0, b, c, 0, d), 2: (0, 0, o, a, b, 0, c, d, 0),
            3: (0, a, b, o, 0, 0, 0, c, d), 4: (a, 0, b, 0, o, 0, c, 0, d), 5: (a, b, 0, 0, 0, o, c, d, 0),
            6: (0, a, b, 0, c, d, o, 0, 0), 7: (a, 0, b, c, 0, d, 0, o, 0), 8: (a, b, 0, c, d, 0, 0, 0, o)}[sel]
    return [[cols[3 * col + row] for col in range(3)] for row in range(3)]


def _basis(ins):
    in0, in1, in2, in3, in4 = ins
    inp = [in4, in0, in1, in2, in3]
    out = [0] * 6
    for i in range(5):
        out[i] = inp[i] >> 3
        out[5] = ((out[5] << 3) | (inp[i] & 7)) & 0xFFFF

    def f(x):
        x &= 0x1FFF
        return (x - 0x2000 if x & 0x1000 else x) / 4096.0
    a = (f(out[1]), f(out[2]), f(out[3]))
    b = (f(out[4]), f(out[0]), f(out[5]))
    c = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    return [[a[r], b[r], c[r]] for r in range(3)]          # columns a, b, c


def load_bca(buf):
    """{node index: (trans[3], rot, scale[3]) curves}, frame count."""
    jnt = nitro.blocks(buf).get(b'JNT0')
    if jnt is None:
        return {}, 0
    _, it = nitro.read_dict(buf, jnt + 8)[0]
    base = jnt + u32(buf, it)
    frames, nobj = u16(buf, base + 4), u16(buf, base + 6)
    pivot, basis = base + u32(buf, base + 0x0C), base + u32(buf, base + 0x10)
    curves = {}

    def fetch(x):
        idx = x & 0x7FFF
        if x & 0x8000:
            q = pivot + 6 * idx
            sn = u16(buf, q)
            return _pivot(sn & 15, (sn >> 4) & 15, fx16(u16(buf, q + 2)), fx16(u16(buf, q + 4)))
        q = basis + 10 * idx
        return _basis([u16(buf, q + 2 * k) for k in range(5)])

    for i in range(nobj):
        c = base + u16(buf, base + 0x14 + 2 * i)
        flags, node = u16(buf, c), buf[c + 3]
        p = c + 4
        if flags & 1:
            continue
        trans, rot, scale = [None] * 3, None, [None] * 3
        if not flags & 6:
            for k in range(3):
                if flags & (8 << k):
                    trans[k] = ('const', fx32(u32(buf, p))); p += 4
                else:
                    trans[k] = _curve(buf, u32(buf, p), base + u32(buf, p + 4)); p += 8
        if not flags & 0xC0:
            if flags & 0x100:
                rot = ('const', fetch(u16(buf, p))); p += 4
            else:
                info, off = u32(buf, p), base + u32(buf, p + 4); p += 8
                cv = _curve(buf, info & 0x0FFFFFFF | 0x10000000, off)          # u16 entries
                start, end = cv[1], cv[2]
                n = len(cv[3])
                rot = ('samples', start, end, [fetch(u16(buf, off + 2 * k)) for k in range(n)])
        if not flags & 0x600:
            for k in range(3):
                if flags & (0x800 << k):
                    scale[k] = ('const', fx32(u32(buf, p))); p += 8
                else:
                    scale[k] = _curve(buf, u32(buf, p), base + u32(buf, p + 4), pair=True); p += 8
        curves[node] = (trans, rot, scale)
    return curves, frames


def anim_node(curves, node, frame, bind):
    if node not in curves:
        return bind
    trans, rot, scale = curves[node]
    t = [_sample(trans[k], bind[k][3], frame) for k in range(3)]
    r = _sample(rot, [[1, 0, 0], [0, 1, 0], [0, 0, 1]], frame) if rot else [[bind[r][c] for c in range(3)] for r in range(3)]
    s = [_sample(scale[k], 1.0, frame) for k in range(3)]
    m = nitro.ident()
    for i in range(3):
        for j in range(3):
            m[i][j] = r[i][j] * s[j]
        m[i][3] = t[i]
    return m


def load_bva(buf):
    """(frames, nodes, visible(frame, node))"""
    for magic, off in nitro.blocks(buf).items():
        if magic == b'VIS0':
            _, it = nitro.read_dict(buf, off + 8)[0]
            base = off + u32(buf, it)
            frames, nodes = u16(buf, base + 4), u16(buf, base + 6)
            bits = base + 0x0C
            return frames, nodes, lambda f, n: bool((u32(buf, bits + 4 * ((f * nodes + n) >> 5)) >> ((f * nodes + n) & 31)) & 1)
    return 0, 0, None


def load_btp(buf):
    """{material: [(frame, texture name, palette name)]}"""
    out = {}
    b = nitro.blocks(buf).get(b'PAT0')
    if b is None:
        return out
    _, it = nitro.read_dict(buf, b + 8)[0]
    base = b + u32(buf, it)
    ntex, npal = buf[base + 6], buf[base + 7]
    toff, poff = base + u16(buf, base + 8), base + u16(buf, base + 10)
    tex = [bytes(buf[toff + 16 * k:toff + 16 * k + 16]).rstrip(b'\0').decode('latin1') for k in range(ntex)]
    pal = [bytes(buf[poff + 16 * k:poff + 16 * k + 16]).rstrip(b'\0').decode('latin1') for k in range(npal)]
    for name, it2 in nitro.read_dict(buf, base + 12):
        nk, off = u32(buf, it2), u16(buf, it2 + 6)
        keys = []
        for k in range(nk):
            fr, ti, pi = u16(buf, base + off + 4 * k), buf[base + off + 4 * k + 2], buf[base + off + 4 * k + 3]
            if ti < ntex and pi < npal:
                keys.append((fr, tex[ti], pal[pi]))
        out[name] = keys
    return out


def load_bta(buf):
    """{material: (frames, u values, v values)} - texture scrolling."""
    out = {}
    b = nitro.blocks(buf).get(b'SRT0')
    if b is None:
        return out
    _, it = nitro.read_dict(buf, b + 8)[0]
    base = b + u32(buf, it)
    for name, it2 in nitro.read_dict(buf, base + 8):
        chans = []
        for k in range(5):
            q = it2 + 8 * k
            n, flags, off = u16(buf, q), buf[q + 3], u32(buf, q + 4)
            if flags == 16 and k >= 3:
                chans.append([s16(buf, base + off + 2 * j) / 32.0 for j in range(n)])
            else:
                chans.append(None)
        if chans[3] or chans[4]:
            out[name] = (chans[3], chans[4])
    return out


# ---------------------------------------------------------------- geometry with bones

class SymMesh:
    """Triangles whose vertices keep their bone (the matrix chain they were drawn with)."""
    def __init__(self):
        self.groups = {}
        self.symbols = [('root',)]
        self.sym_index = {('root',): 0}

    def sym(self, key):
        i = self.sym_index.get(key)
        if i is None:
            i = self.sym_index[key] = len(self.symbols)
            self.symbols.append(key)
        return i


def run_sbc_bones(md):
    """Like nitro.run_sbc, but every vertex is (bone, local position) and bones are kept symbolic."""
    buf = md.buf
    mesh = SymMesh()
    cur = 0
    stack = [0] * 32
    mat = 0
    p, end = md.sbc
    while p < end:
        b = buf[p]; cmd, opt = b & 0x1F, b & 0xE0; p += 1
        if cmd == 0x00:
            continue
        if cmd == 0x01:
            break
        if cmd == 0x02:
            p += 2
        elif cmd == 0x03:
            cur = stack[buf[p]]; p += 1
        elif cmd == 0x04:
            mat = buf[p]; p += 1
        elif cmd == 0x05:
            s, n = md.shapes[buf[p]]; p += 1
            dl_bones(md, s, n, cur, stack, mat, mesh)
        elif cmd == 0x06:
            node = buf[p]; p += 3
            st = rs = None
            if opt & 0x20: st = buf[p]; p += 1
            if opt & 0x40: rs = buf[p]; p += 1
            if rs is not None: cur = stack[rs]
            cur = mesh.sym(('node', cur, node))
            if st is not None: stack[st] = cur
        elif cmd in (0x07, 0x08):
            p += 1
            st = rs = None
            if opt & 0x20: st = buf[p]; p += 1
            if opt & 0x40: rs = buf[p]; p += 1
            if rs is not None: cur = stack[rs]
            if st is not None: stack[st] = cur
        elif cmd == 0x09:
            dst, n = buf[p], buf[p + 1]; p += 2 + 3 * n
            stack[dst] = cur
        elif cmd == 0x0A:
            rel, size = u32(buf, p), u32(buf, p + 4)
            dl_bones(md, p - 1 + rel, size, cur, stack, mat, mesh); p += 8
        elif cmd == 0x0B:
            s = md.inv_pos_scale if opt else md.pos_scale
            cur = mesh.sym(('mul', cur, tuple(map(tuple, nitro.scale(s, s, s)))))
        elif cmd in (0x0C, 0x0D):
            p += 2
        else:
            break
    return mesh


def dl_bones(md, start, size, cur, stack, mat, mesh):
    """nitro.run_display_list with symbolic matrices (only the commands models use)."""
    buf = md.buf
    material = md.materials[mat]
    w, h = material.width or 1, material.height or 1
    color = (1.0, 1.0, 1.0)
    if material.dif_amb & 0x8000:
        d = material.dif_amb & 0x7FFF
        color = ((d & 31) / 31, ((d >> 5) & 31) / 31, ((d >> 10) & 31) / 31)
    alpha = material.alpha / 31 if material.alpha else 0.0
    uv, prev, verts, prim, sp = (0.0, 0.0), (0.0, 0.0, 0.0), [], None, 0
    sext = nitro.sext

    def tri(a, b, c):
        mesh.groups.setdefault(mat, []).append((a, b, c))

    def end_prim():
        nonlocal verts, prim
        v, t = verts, prim
        if t == 0:
            for i in range(0, len(v) - 2, 3): tri(v[i], v[i + 1], v[i + 2])
        elif t == 1:
            for i in range(0, len(v) - 3, 4): tri(v[i], v[i + 1], v[i + 2]); tri(v[i], v[i + 2], v[i + 3])
        elif t == 2:
            for k in range(len(v) - 2):
                tri(v[k], v[k + 1], v[k + 2]) if k % 2 == 0 else tri(v[k], v[k + 2], v[k + 1])
        elif t == 3:
            for i in range(0, len(v) - 3, 2):
                a, b, c, d = v[i], v[i + 1], v[i + 3], v[i + 2]
                tri(a, b, c); tri(a, c, d)
        verts, prim = [], None

    def vertex(local):
        nonlocal prev
        prev = local
        verts.append((cur, local, (uv[0] / w, uv[1] / h), color + (alpha,)))

    p, end = start, start + size
    while p + 4 <= end:
        cmds = [buf[p + i] for i in range(4)]
        p += 4
        for c in cmds:
            n = nitro.PARAMS.get(c, 0)
            par = [u32(buf, p + 4 * i) for i in range(n)]
            p += 4 * n
            if c == 0x14:
                cur = stack[par[0] & 31]
            elif c == 0x13:
                stack[par[0] & 31] = cur
            elif c == 0x11:
                stack[sp & 31] = cur; sp += 1
            elif c == 0x12:
                sp -= sext(par[0], 6); cur = stack[sp & 31]
            elif c == 0x15:
                cur = 0
            elif c in (0x18, 0x19, 0x1A, 0x1B, 0x1C):
                vals = [struct.unpack('<i', struct.pack('<I', x))[0] / 4096.0 for x in par]
                if c == 0x18:
                    m = [[vals[4 * col + row] for col in range(4)] for row in range(4)]
                elif c == 0x1A:
                    m = nitro.from_gbatek(vals + [0, 0, 0], 4)
                elif c == 0x19:
                    m = nitro.from_gbatek(vals, 4)
                elif c == 0x1B:
                    m = nitro.scale(*vals)
                else:
                    m = nitro.trans(*vals)
                cur = mesh.sym(('mul', cur, tuple(map(tuple, m))))
            elif c == 0x20:
                v = par[0]
                color = ((v & 31) / 31, ((v >> 5) & 31) / 31, ((v >> 10) & 31) / 31)
            elif c == 0x21:
                color = (1.0, 1.0, 1.0)
            elif c == 0x22:
                uv = (sext(par[0], 16) / 16.0, sext(par[0] >> 16, 16) / 16.0)
            elif c == 0x23:
                vertex((sext(par[0], 16) / 4096.0, sext(par[0] >> 16, 16) / 4096.0, sext(par[1], 16) / 4096.0))
            elif c == 0x24:
                v = par[0]
                vertex((sext(v, 10) / 64.0, sext(v >> 10, 10) / 64.0, sext(v >> 20, 10) / 64.0))
            elif c == 0x25:
                vertex((sext(par[0], 16) / 4096.0, sext(par[0] >> 16, 16) / 4096.0, prev[2]))
            elif c == 0x26:
                vertex((sext(par[0], 16) / 4096.0, prev[1], sext(par[0] >> 16, 16) / 4096.0))
            elif c == 0x27:
                vertex((prev[0], sext(par[0], 16) / 4096.0, sext(par[0] >> 16, 16) / 4096.0))
            elif c == 0x28:
                v = par[0]
                vertex((prev[0] + sext(v, 10) / 4096.0, prev[1] + sext(v >> 10, 10) / 4096.0, prev[2] + sext(v >> 20, 10) / 4096.0))
            elif c == 0x30:
                if par[0] & 0x8000:
                    d = par[0] & 0x7FFF
                    color = ((d & 31) / 31, ((d >> 5) & 31) / 31, ((d >> 10) & 31) / 31)
            elif c == 0x40:
                end_prim(); prim = par[0] & 3
            elif c == 0x41:
                end_prim()
    end_prim()


def bone_matrices(md, mesh, curves, vis, frame):
    """The matrix of every bone symbol at a frame (hidden nodes: zero scale)."""
    vframes, vnodes, visible = vis
    out = [None] * len(mesh.symbols)
    for i, s in enumerate(mesh.symbols):
        if s[0] == 'root':
            out[i] = nitro.ident()
        elif s[0] == 'node':
            _, parent, node = s
            m = anim_node(curves, node, frame, md.nodes[node]) if node < len(md.nodes) else nitro.ident()
            if visible and node < vnodes and not visible(min(frame, vframes - 1), node):
                m = nitro.scale(0, 0, 0)
            out[i] = nitro.mul(out[parent], m)
        else:
            _, parent, m = s
            out[i] = nitro.mul(out[parent], [list(r) for r in m])
    return out


# ---------------------------------------------------------------- one demo

def groups_of(files):
    """Split a/1/6/0 into demo groups: [(name prefix, [(magic, data)], camera bytes)]."""
    out, cur = [], []
    for f in files:
        magic = bytes(f[:4])
        if magic in (b'BMD0', b'BCA0', b'BTA0', b'BTP0', b'BMA0', b'BVA0', b'BTX0'):
            cur.append((magic, bytes(f)))
        else:
            out.append((cur, bytes(f)))
            cur = []
    return out


def write(rom):
    """Converts Black's demos (the _w_ ones are White's: BW's scripts choose by version, and ours is Black)."""
    files = rom.narc('a/1/6/0')
    os.makedirs(OUT + '/tex', exist_ok=True)
    groups = groups_of(files)
    table = []
    for demo, prefix in sorted(DEMOS.items()):
        if prefix.endswith('_w_'):
            continue
        grp = next(((g, cam) for g, cam in groups if g and any((first_name(d) or '').startswith(prefix) for m, d in g)), None)
        if grp is None:
            print('demos: %d (%s) not found' % (demo, prefix))
            continue
        g, cam = grp
        frames = u32(cam, 0)
        n = (len(cam) - 8) // 24
        camera = [list(struct.unpack_from('<6f', cam, 8 + 24 * k)) for k in range(n)]
        # models and the animations next to them (anims named like the model; BCA/BMA come before it)
        units, pending = [], []
        if demo in ONLY_PREFIX:
            g = [(m, d) for m, d in g if (first_name(d) or '').startswith(prefix)]
        for magic, d in g:
            if magic == b'BMD0':
                units.append({'bmd': d, 'anims': pending})
                pending = []
            elif magic in (b'BCA0', b'BMA0'):
                pending.append((magic, d))
            elif units:
                units[-1]['anims'].append((magic, d))
        models = []
        for k, u in enumerate(units):
            try:
                md = nitro.load_bmd(u['bmd'])[0]
            except Exception as ex:
                print('demos: %d model %d skipped (%s)' % (demo, k, ex))
                continue
            anims = dict()
            for magic, d in u['anims']:
                anims.setdefault(magic, d)
            curves, nf = load_bca(anims[b'BCA0']) if b'BCA0' in anims else ({}, 0)
            vis = load_bva(anims[b'BVA0']) if b'BVA0' in anims else (0, 0, None)
            pats = load_btp(anims[b'BTP0']) if b'BTP0' in anims else {}
            srt = load_bta(anims[b'BTA0']) if b'BTA0' in anims else {}
            mesh = run_sbc_bones(md)
            mi = write_model(demo, k, md, mesh, curves, vis, pats, srt, frames, u['bmd'])
            # seasonal versions (the Ferris wheel's park: ..._01sp/su/au/wi): only this season's is drawn
            for si, suf in enumerate(('sp', 'su', 'au', 'wi')):
                if md.name.endswith(suf):
                    mi['season'] = si
            models.append(mi)
        with open('%s/d%d.json' % (OUT, demo), 'w') as f:
            json.dump({'frames': frames, 'camera': [[round(v, 3) for v in c] for c in camera], 'models': models}, f,
                      separators=(',', ':'))
        table.append((demo, frames))
        print('demos: %d (%s): %d frames, %d models' % (demo, prefix, frames, len(models)))
    with open('src/data/bw_demos.h', 'w') as f:
        f.write('// Generated by tools/pokemon_world/bw/demos.py: BW 3D cutscene id -> its length in frames (30 fps).\n'
                'static const u16 sBwDemoFrames[][2] =\n{\n' + ''.join('    {%d, %d},\n' % t for t in table) + '};\n')
    return table


def write_model(demo, k, md, mesh, curves, vis, pats, srt, frames, bmd):
    texset = export3d.TextureSet(bmd)
    # draws: per material group, split so each uses at most MAX_BONES bones
    draws, verts, vindex, groups = [], [], {}, []
    for mat_idx in sorted(mesh.groups):
        m = md.materials[mat_idx]
        ti = texset.index(m.texture, m.palette) if getattr(m, 'texture', None) else None
        tris = mesh.groups[mat_idx]
        cur_bones, cur_idx = {}, []

        def flush():
            if cur_idx:
                ws, wt = m.wrap
                flags = 0
                if ti is not None:
                    _, _, translucent, tested = texset.images[ti]
                    flags |= (2 if translucent else 0) | (1 if tested else 0)
                if 0 < m.alpha < 31:
                    flags |= 2
                groups.append((0xFFFF if ti is None else ti, export3d.WRAP[ws], export3d.WRAP[wt], m.alpha or 31,
                               export3d.CULL[m.cull], flags, list(cur_idx), m.name))
                draws.append(sorted(cur_bones, key=cur_bones.get))
        for t in tris:
            need = {v[0] for v in t} - set(cur_bones)
            if len(cur_bones) + len(need) > MAX_BONES:
                flush()
                cur_bones, cur_idx = {}, []
            for b in sorted({v[0] for v in t}):
                cur_bones.setdefault(b, len(cur_bones))
            for bone, local, uv, col in t:
                v = (cur_bones[bone], round(local[0] * 4096), round(local[1] * 4096), round(local[2] * 4096),
                     round(uv[0], 5), round(uv[1], 5), tuple(max(0, min(255, round(c * 255))) for c in col), len(groups))
                i = vindex.get(v)
                if i is None:
                    i = vindex[v] = len(verts)
                    verts.append(v)
                cur_idx.append(i)
        flush()
    # mesh file: PWD1 - like PWM2 but positions are f32 (bone-local) and each vertex has its bone slot
    big = len(verts) > 0xFFFF
    gbin, ibin = b'', []
    for tex, ws, wt, alpha, cull, flags, idx, _ in groups:
        gbin += struct.pack('<HBBBBBBII', tex, ws, wt, alpha, cull, flags, 0, len(ibin), len(idx))
        ibin += idx
    vbin = b''.join(struct.pack('<3fHH2f4B', x / 4096, y / 4096, z / 4096, slot, 0, u, v, *c)
                    for slot, x, y, z, u, v, c, _ in verts)
    data = (b'PWD1' + struct.pack('<III', len(groups), len(verts), len(ibin)) + gbin + vbin
            + struct.pack(('<%dI' if big else '<%dH') % len(ibin), *ibin))
    name = 'd%d_%d' % (demo, k)
    with open('%s/%s.bin' % (OUT, name), 'wb') as f:
        f.write(data)
    textures = []
    for tname, pngdata, _, _ in texset.images:
        fn = '%s_%s.png' % (name, ''.join(ch if ch.isalnum() or ch in '_-' else '_' for ch in tname))
        with open('%s/tex/%s' % (OUT, fn), 'wb') as f:
            f.write(pngdata)
        textures.append(fn)
    # texture patterns: material -> keys [frame, texture index]
    patterns = {}
    for gi, (tex, ws, wt, alpha, cull, flags, idx, mname) in enumerate(groups):
        if mname in pats:
            keys = []
            for fr, tname, pname in pats[mname]:
                ti = texset.index(tname, pname)
                if ti is not None:
                    keys.append([fr, ti])
            if keys:
                patterns[gi] = keys
    for tname, pngdata, _, _ in texset.images[len(textures):]:       # textures the patterns added
        fn = '%s_%s.png' % (name, ''.join(ch if ch.isalnum() or ch in '_-' else '_' for ch in tname))
        with open('%s/tex/%s' % (OUT, fn), 'wb') as f:
            f.write(pngdata)
        textures.append(fn)
    scroll = {gi: srt[g[7]] for gi, g in enumerate(groups) if g[7] in srt}
    # baked bone matrices
    nb = len(mesh.symbols)
    baked = []
    for fr in range(0, max(1, frames) + STEP, STEP):
        mats = bone_matrices(md, mesh, curves, vis, fr)
        for m in mats:
            baked.append(struct.pack('<12f', *(m[r][c] for r in range(3) for c in range(4))))
    with open('%s/%s.anim' % (OUT, name), 'wb') as f:
        f.write(b''.join(baked))
    return {'mesh': name + '.bin', 'anim': name + '.anim', 'bones': nb, 'step': STEP, 'textures': textures,
            'draws': draws, 'patterns': patterns,
            'scroll': {gi: [s[0] or [], s[1] or []] for gi, s in scroll.items()}}
