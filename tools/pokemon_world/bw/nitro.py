"""Pokémon World: a reader for Nintendo DS NITRO 3D files (BMD0 models, BTX0 textures).

Used by the Unova converter (tools/pokemon_world/bw/) to turn Pokémon Black's map models into
triangle meshes and RGBA textures for the web build's 3D renderer. Written from the formats as
decoded by scurest/apicula, noclip.website's NNS G3D code, Fexty12573/nitrog3d and GBATEK.

Everything is little-endian. Matrices are 4x4 lists of rows in column-vector form (v' = M v).
"""

import struct


def u8(b, o): return b[o]
def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def s16(b, o): return struct.unpack_from('<h', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def s32(b, o): return struct.unpack_from('<i', b, o)[0]
def fx32(b, o): return s32(b, o) / 4096.0
def fx16(b, o): return s16(b, o) / 4096.0


def sext(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


# --- small matrix helpers (4x4, rows) ---

def ident():
    return [[1.0 if r == c else 0.0 for c in range(4)] for r in range(4)]


def mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def scale(x, y, z):
    m = ident(); m[0][0], m[1][1], m[2][2] = x, y, z
    return m


def trans(x, y, z):
    m = ident(); m[0][3], m[1][3], m[2][3] = x, y, z
    return m


def apply(m, v):
    x, y, z = v
    return (m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3],
            m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3],
            m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3])


def copy(m):
    return [row[:] for row in m]


def from_gbatek(vals, rows):
    """GBATEK row-vector matrix (rows x 3 values, plus translation row for 4x3) -> column-vector 4x4."""
    m = ident()
    for r in range(3):
        for c in range(3):
            m[c][r] = vals[3 * r + c]
    if rows == 4:
        m[0][3], m[1][3], m[2][3] = vals[9], vals[10], vals[11]
    return m


# --- containers and dictionaries ---

def blocks(buf):
    """{magic: offset} of the blocks of a BMD0/BTX0 file."""
    n = u16(buf, 0x0E)
    out = {}
    for i in range(n):
        off = u32(buf, 0x10 + 4 * i)
        out[bytes(buf[off:off + 4])] = off
    return out


def read_dict(buf, off):
    """[(name, item offset)] of a NITRO dictionary."""
    if buf[off] != 0:
        raise ValueError('bad dictionary at %#x' % off)
    count = buf[off + 1]
    entry = off + u16(buf, off + 6)
    esize = u16(buf, entry)
    names = entry + u16(buf, entry + 2)
    out = []
    for i in range(count):
        name = bytes(buf[names + 16 * i:names + 16 * i + 16]).rstrip(b'\0').decode('latin1')
        out.append((name, entry + 4 + i * esize))
    return out


# --- models ---

class Material:
    def __init__(self, name):
        self.name = name
        self.texture = None
        self.palette = None
        self.dif_amb = 0x7FFF7FFF
        self.polygon_attr = 0
        self.teximage = 0
        self.width = self.height = 0

    @property
    def alpha(self): return (self.polygon_attr >> 16) & 31

    @property
    def cull(self):
        """'none' (draw both sides), 'back' (draw the front only), 'front' (draw the back only), 'all'."""
        return {0: 'all', 1: 'front', 2: 'back', 3: 'none'}[(self.polygon_attr >> 6) & 3]

    @property
    def wrap(self):
        def axis(rep, flip):
            if not (self.teximage >> rep) & 1:
                return 'clamp'
            return 'mirror' if (self.teximage >> flip) & 1 else 'repeat'
        return axis(16, 18), axis(17, 19)


class Model:
    pass


def pivot_matrix(p, minus, revc, revd, a, b):
    o = -1.0 if minus else 1.0
    c = -b if revc else b
    d = -a if revd else a
    r = [[0.0] * 3 for _ in range(3)]
    pr, pc = p % 3, p // 3
    r[pr][pc] = o
    rows = [i for i in range(3) if i != pr]
    cols = [i for i in range(3) if i != pc]
    r[rows[0]][cols[0]] = a; r[rows[0]][cols[1]] = c
    r[rows[1]][cols[0]] = b; r[rows[1]][cols[1]] = d
    return r


def parse_node(buf, o):
    flags = u16(buf, o)
    m00 = fx16(buf, o + 2)
    p = o + 4
    t = (0.0, 0.0, 0.0)
    if not flags & 1:
        t = (fx32(buf, p), fx32(buf, p + 4), fx32(buf, p + 8)); p += 12
    rot = [[1.0, 0, 0], [0, 1.0, 0], [0, 0, 1.0]]
    if not flags & 2:
        if flags & 8:
            a, b = fx16(buf, p), fx16(buf, p + 2); p += 4
            rot = pivot_matrix((flags >> 4) & 15, flags & 0x100, flags & 0x200, flags & 0x400, a, b)
        else:
            m = [m00] + [fx16(buf, p + 2 * i) for i in range(8)]; p += 16
            rot = [[m[3 * c + r] for c in range(3)] for r in range(3)]
    s = (1.0, 1.0, 1.0)
    if not flags & 4:
        s = (fx32(buf, p), fx32(buf, p + 4), fx32(buf, p + 8)); p += 24
    m = ident()
    for r in range(3):
        for c in range(3):
            m[r][c] = rot[r][c] * s[c]
        m[r][3] = t[r]
    return m


def parse_model(buf, m, name):
    md = Model()
    md.name = name
    sbc_off, mat_off, shp_off = u32(buf, m + 4), u32(buf, m + 8), u32(buf, m + 0x0C)
    md.pos_scale, md.inv_pos_scale = fx32(buf, m + 0x1C), fx32(buf, m + 0x20)
    md.nodes = [parse_node(buf, m + 0x40 + u32(buf, it)) for _, it in read_dict(buf, m + 0x40)]
    md.node_names = [n for n, _ in read_dict(buf, m + 0x40)]
    ms = m + mat_off
    md.materials = []
    for mname, it in read_dict(buf, ms + 4):
        r = ms + u32(buf, it)
        mat = Material(mname)
        mat.dif_amb = u32(buf, r + 4)
        mat.polygon_attr = u32(buf, r + 0x0C)
        mat.teximage = u32(buf, r + 0x14)
        mat.width, mat.height = u16(buf, r + 0x20), u16(buf, r + 0x22)
        md.materials.append(mat)
    for which, doff in (('texture', u16(buf, ms)), ('palette', u16(buf, ms + 2))):
        for tname, it in read_dict(buf, ms + doff):
            off, cnt = u16(buf, it), buf[it + 2] & 0x7F
            for k in range(cnt):
                setattr(md.materials[buf[ms + off + k]], which, tname)
    ss = m + shp_off
    md.shapes = []
    for _, it in read_dict(buf, ss):
        r = ss + u32(buf, it)
        md.shapes.append((r + u32(buf, r + 8), u32(buf, r + 0x0C)))
    md.sbc = (m + sbc_off, m + mat_off)
    md.buf = buf
    return md


def load_bmd(buf):
    b = blocks(buf)
    mdl0 = b[b'MDL0']
    return [parse_model(buf, mdl0 + u32(buf, it), n) for n, it in read_dict(buf, mdl0 + 8)]


# Display-list parameter word counts (GBATEK "DS 3D I/O Map").
PARAMS = {0x00: 0, 0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1, 0x15: 0, 0x16: 16, 0x17: 12, 0x18: 16,
          0x19: 12, 0x1A: 9, 0x1B: 3, 0x1C: 3, 0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2, 0x24: 1, 0x25: 1,
          0x26: 1, 0x27: 1, 0x28: 1, 0x29: 1, 0x2A: 1, 0x2B: 1, 0x30: 1, 0x31: 1, 0x32: 1, 0x33: 1,
          0x34: 32, 0x40: 1, 0x41: 0, 0x50: 1, 0x60: 1, 0x70: 3, 0x71: 2, 0x72: 1}


class Mesh:
    """Triangles grouped by material: groups[material index] = list of (pos, uv, rgba) triples."""
    def __init__(self):
        self.groups = {}
        self.billboard_shapes = 0
        self.warnings = []

    def tri(self, mat, a, b, c):
        self.groups.setdefault(mat, []).append((a, b, c))


def run_display_list(md, start, size, cur, stack, mat, mesh):
    buf = md.buf
    material = md.materials[mat]
    w = material.width or 1
    h = material.height or 1
    color = (1.0, 1.0, 1.0)
    if material.dif_amb & 0x8000:
        d = material.dif_amb & 0x7FFF
        color = ((d & 31) / 31, ((d >> 5) & 31) / 31, ((d >> 10) & 31) / 31)
    alpha = material.alpha / 31 if material.alpha else 0.0
    uv = (0.0, 0.0)
    prev = (0.0, 0.0, 0.0)
    verts = []
    prim = None
    sp = 0

    def end_prim():
        nonlocal verts, prim
        v, t = verts, prim
        if t == 0:
            for i in range(0, len(v) - 2, 3): mesh.tri(mat, v[i], v[i + 1], v[i + 2])
        elif t == 1:
            for i in range(0, len(v) - 3, 4):
                mesh.tri(mat, v[i], v[i + 1], v[i + 2]); mesh.tri(mat, v[i], v[i + 2], v[i + 3])
        elif t == 2:
            for k in range(len(v) - 2):
                if k % 2 == 0: mesh.tri(mat, v[k], v[k + 1], v[k + 2])
                else: mesh.tri(mat, v[k], v[k + 2], v[k + 1])
        elif t == 3:
            for i in range(0, len(v) - 3, 2):
                a, b, c, d = v[i], v[i + 1], v[i + 3], v[i + 2]
                mesh.tri(mat, a, b, c); mesh.tri(mat, a, c, d)
        verts, prim = [], None

    def vertex(local):
        nonlocal prev
        prev = local
        verts.append((apply(cur, local), (uv[0] / w, uv[1] / h), color + (alpha,)))

    p, end = start, start + size
    while p + 4 <= end:
        cmds = [buf[p + i] for i in range(4)]
        p += 4
        for c in cmds:
            n = PARAMS.get(c)
            if n is None:
                mesh.warnings.append('unknown GX command %#x' % c)
                n = 0
            par = [u32(buf, p + 4 * i) for i in range(n)]
            p += 4 * n
            if c == 0x14:
                cur = copy(stack[par[0] & 31])
            elif c == 0x13:
                stack[par[0] & 31] = copy(cur)
            elif c == 0x11:
                stack[sp & 31] = copy(cur); sp += 1
            elif c == 0x12:
                sp -= sext(par[0], 6); cur = copy(stack[sp & 31])
            elif c == 0x15:
                cur = ident()
            elif c in (0x16, 0x17, 0x18, 0x19, 0x1A):
                vals = [s32(struct.pack('<I', x), 0) / 4096.0 for x in par]
                if c == 0x16 or c == 0x18:
                    m = [[vals[4 * col + row] for col in range(4)] for row in range(4)]
                elif c == 0x1A:
                    m = from_gbatek(vals + [0, 0, 0], 4)
                else:
                    m = from_gbatek(vals, 4)
                cur = m if c in (0x16, 0x17) else mul(cur, m)
            elif c == 0x1B:
                f = [s32(struct.pack('<I', x), 0) / 4096.0 for x in par]
                cur = mul(cur, scale(*f))
            elif c == 0x1C:
                f = [s32(struct.pack('<I', x), 0) / 4096.0 for x in par]
                cur = mul(cur, trans(*f))
            elif c == 0x20:
                v = par[0]
                color = ((v & 31) / 31, ((v >> 5) & 31) / 31, ((v >> 10) & 31) / 31)
            elif c == 0x21:
                color = (1.0, 1.0, 1.0)  # lit vertex: lighting is applied by the renderer
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
                vertex((prev[0] + sext(v, 10) / 4096.0, prev[1] + sext(v >> 10, 10) / 4096.0,
                        prev[2] + sext(v >> 20, 10) / 4096.0))
            elif c == 0x30:
                if par[0] & 0x8000:
                    d = par[0] & 0x7FFF
                    color = ((d & 31) / 31, ((d >> 5) & 31) / 31, ((d >> 10) & 31) / 31)
            elif c == 0x40:
                end_prim(); prim = par[0] & 3
            elif c == 0x41:
                end_prim()
    end_prim()
    return cur


def run_sbc(md, mesh, world=None):
    """Runs the model's render commands; appends its triangles (in world units) to mesh."""
    buf = md.buf
    cur = copy(world) if world else ident()
    base = copy(cur)
    stack = [copy(base) for _ in range(32)]
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
            cur = copy(stack[buf[p]]); p += 1
        elif cmd == 0x04:
            mat = buf[p]; p += 1
        elif cmd == 0x05:
            s, n = md.shapes[buf[p]]; p += 1
            run_display_list(md, s, n, cur, stack, mat, mesh)
        elif cmd == 0x06:
            node = buf[p]; p += 3
            st = rs = None
            if opt & 0x20: st = buf[p]; p += 1
            if opt & 0x40: rs = buf[p]; p += 1
            if rs is not None: cur = copy(stack[rs])
            cur = mul(cur, md.nodes[node])
            if st is not None: stack[st] = copy(cur)
        elif cmd in (0x07, 0x08):
            p += 1
            st = rs = None
            if opt & 0x20: st = buf[p]; p += 1
            if opt & 0x40: rs = buf[p]; p += 1
            if rs is not None: cur = copy(stack[rs])
            mesh.billboard_shapes += 1
            if st is not None: stack[st] = copy(cur)
        elif cmd == 0x09:
            dst, n = buf[p], buf[p + 1]; p += 2 + 3 * n
            mesh.warnings.append('NODEMIX (skinned) not supported')
            stack[dst] = copy(cur)
        elif cmd == 0x0A:
            rel, size = u32(buf, p), u32(buf, p + 4)
            run_display_list(md, p - 1 + rel, size, cur, stack, mat, mesh); p += 8
        elif cmd == 0x0B:
            s = md.inv_pos_scale if opt else md.pos_scale
            cur = mul(cur, scale(s, s, s))
        elif cmd in (0x0C, 0x0D):
            p += 2
        else:
            raise ValueError('unknown SBC opcode %#x in %s' % (b, md.name))
    return mesh


# --- textures ---

def e5(x): return (x << 3) | (x >> 2)


def rgb555(v): return (e5(v & 31), e5((v >> 5) & 31), e5((v >> 10) & 31))


class Texture:
    def __init__(self, name, params, data_off, idx_off):
        self.name = name
        self.params = params
        self.width = 8 << ((params >> 20) & 7)
        self.height = 8 << ((params >> 23) & 7)
        self.format = (params >> 26) & 7
        self.color0 = (params >> 29) & 1
        self.data_off = data_off
        self.idx_off = idx_off


def load_btx(buf):
    """(textures {name: Texture}, palettes {name: offset}, buf) from a BTX0 (or a BMD0 with TEX0)."""
    t = blocks(buf).get(b'TEX0')
    if t is None:
        return {}, {}, buf
    tex_dict = t + u16(buf, t + 0x0E)
    tex_data = t + u32(buf, t + 0x14)
    c_data = t + u32(buf, t + 0x24)
    c_idx = t + u32(buf, t + 0x28)
    pal_dict = t + u32(buf, t + 0x34)
    pal_data = t + u32(buf, t + 0x38)
    textures = {}
    for name, it in read_dict(buf, tex_dict):
        params = u32(buf, it)
        off = (params & 0xFFFF) << 3
        if (params >> 26) & 7 == 5:
            textures[name] = Texture(name, params, c_data + off, c_idx + off // 2)
        else:
            textures[name] = Texture(name, params, tex_data + off, None)
    palettes = {name: pal_data + (u16(buf, it) << 3) for name, it in read_dict(buf, pal_dict)}
    for tex in textures.values():
        tex.pal_end = t + u32(buf, t + 4)
    return textures, palettes, buf


def decode_texture(buf, tex, pal_off):
    """RGBA8888 bytes (rows top first) of a texture with the given palette offset."""
    w, h, f = tex.width, tex.height, tex.format
    out = bytearray(w * h * 4)
    end = len(buf)

    def col(k):
        o = pal_off + 2 * k
        return rgb555(u16(buf, o)) if pal_off is not None and o + 1 < end else (0, 0, 0)

    def put(i, rgb, a):
        out[4 * i:4 * i + 4] = bytes((rgb[0], rgb[1], rgb[2], a))

    d = tex.data_off
    if f == 1:
        for i in range(w * h):
            v = buf[d + i]
            a3 = v >> 5
            put(i, col(v & 31), e5((a3 << 2) | (a3 >> 1)))
    elif f in (2, 3, 4):
        bpp = {2: 2, 3: 4, 4: 8}[f]
        mask = (1 << bpp) - 1
        for i in range(w * h):
            bit = i * bpp
            k = (buf[d + bit // 8] >> (bit % 8)) & mask
            put(i, col(k), 0 if (k == 0 and tex.color0) else 255)
    elif f == 5:
        bw = w // 4
        for by in range(h // 4):
            for bx in range(bw):
                blk = by * bw + bx
                word = u32(buf, d + 4 * blk)
                pal = u16(buf, tex.idx_off + 2 * blk)
                mode, base = pal >> 14, (pal & 0x3FFF) * 2
                c0, c1 = col(base), col(base + 1)
                if mode == 0: cs = [(c0, 255), (c1, 255), (col(base + 2), 255), ((0, 0, 0), 0)]
                elif mode == 1:
                    cs = [(c0, 255), (c1, 255), (tuple((a + b) // 2 for a, b in zip(c0, c1)), 255), ((0, 0, 0), 0)]
                elif mode == 2: cs = [(c0, 255), (c1, 255), (col(base + 2), 255), (col(base + 3), 255)]
                else:
                    cs = [(c0, 255), (c1, 255), (tuple((5 * a + 3 * b) // 8 for a, b in zip(c0, c1)), 255),
                          (tuple((3 * a + 5 * b) // 8 for a, b in zip(c0, c1)), 255)]
                for ty in range(4):
                    for tx in range(4):
                        k = (word >> (2 * (4 * ty + tx))) & 3
                        put((4 * by + ty) * w + 4 * bx + tx, cs[k][0], cs[k][1])
    elif f == 6:
        for i in range(w * h):
            v = buf[d + i]
            put(i, col(v & 7), e5(v >> 3))
    elif f == 7:
        for i in range(w * h):
            v = u16(buf, d + 2 * i)
            put(i, rgb555(v), 255 if v & 0x8000 else 0)
        if not any(out[3::4]):
            out[3::4] = b'\xff' * (w * h)
    return bytes(out)
