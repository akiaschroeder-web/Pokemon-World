"""Pokémon World: writes NITRO models as files the web build's 3D renderer loads.

A mesh file (.bin, 'PWM2') is little-endian binary:
    char[4] 'PWM2'; u32 group count; u32 vertex count; u32 index count
    groups (16 bytes each): u16 texture index (0xFFFF = none), u8 wrapS, u8 wrapT (0 clamp, 1 repeat,
              2 mirror), u8 alpha (0-31), u8 cull (0 none, 1 back, 2 front, 3 all), u8 flags (1 = alpha-tested
              texture, 2 = translucent), u8 pad, u32 first index, u32 index count
    vertices (20 bytes each): s16 x, y, z (world units x 8), s16 pad, f32 u, v, u8 r, g, b, a
    indices: u16 each (u32 if the vertex count is over 65535), 3 per triangle
Textures are PNGs; a mesh's texture index points into a list kept next to it (JSON).
"""

import struct
import zlib

import nitro

WRAP = {'clamp': 0, 'repeat': 1, 'mirror': 2}
CULL = {'none': 0, 'back': 1, 'front': 2, 'all': 3}
POS_SCALE = 8


def png(w, h, rgba):
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


class TextureSet:
    """Decodes textures on demand from one BTX0 and remembers which ones were used."""
    def __init__(self, btx):
        self.textures, self.palettes, self.buf = nitro.load_btx(btx)
        self.used = {}       # (texture, palette) -> index
        self.images = []     # (name, png bytes, translucent?, alpha-tested?)

    def index(self, tex, pal):
        key = (tex, pal)
        if key in self.used:
            return self.used[key]
        t = self.textures.get(tex)
        if t is None:
            return None
        rgba = nitro.decode_texture(self.buf, t, self.palettes.get(pal))
        alphas = set(rgba[3::4])
        translucent = any(0 < a < 255 for a in alphas)
        tested = 0 in alphas
        self.used[key] = len(self.images)
        self.images.append((tex if pal in (None, tex + '_pl') else tex + '@' + pal, png(t.width, t.height, rgba),
                            translucent, tested))
        return self.used[key]


class MeshWriter:
    """Collects triangles from several models (each with its own texture set) into one indexed mesh."""
    def __init__(self, origin=(0.0, 0.0, 0.0)):
        self.origin = origin
        self.groups = []     # [texture key or None, wrapS, wrapT, alpha, cull, flags, [indices]]
        self.group_index = {}
        self.verts = []
        self.vert_index = {}
        self.textures = []   # [{'kind', 'name', 'translucent'}]
        self.tex_index = {}

    def add(self, mesh, materials, texset, kind):
        for mat_idx in sorted(mesh.groups):
            m = materials[mat_idx]
            ti = texset.index(m.texture, m.palette) if m.texture else None
            flags = 0
            tkey = None
            if ti is not None:
                name, _, translucent, tested = texset.images[ti]
                flags |= (2 if translucent else 0) | (1 if tested else 0)
                tkey = (kind, name)
                if tkey not in self.tex_index:
                    self.tex_index[tkey] = len(self.textures)
                    self.textures.append({'kind': kind, 'name': name})
            if 0 < m.alpha < 31:
                flags |= 2
            ws, wt = m.wrap
            gkey = (self.tex_index.get(tkey, 0xFFFF), WRAP[ws], WRAP[wt], m.alpha or 31, CULL[m.cull], flags)
            if gkey not in self.group_index:
                self.group_index[gkey] = len(self.groups)
                self.groups.append(list(gkey) + [[]])
            out = self.groups[self.group_index[gkey]][-1]
            for tri in mesh.groups[mat_idx]:
                for pos, uv, col in tri:
                    v = (round((pos[0] - self.origin[0]) * POS_SCALE), round((pos[1] - self.origin[1]) * POS_SCALE),
                         round((pos[2] - self.origin[2]) * POS_SCALE), round(uv[0], 5), round(uv[1], 5),
                         tuple(max(0, min(255, round(c * 255))) for c in col))
                    i = self.vert_index.get(v)
                    if i is None:
                        i = self.vert_index[v] = len(self.verts)
                        self.verts.append(v)
                    out.append(i)

    def data(self):
        big = len(self.verts) > 0xFFFF
        idx_fmt = '<%dI' if big else '<%dH'
        groups, indices = [], []
        for tex, ws, wt, alpha, cull, flags, idx in self.groups:
            groups.append(struct.pack('<HBBBBBBII', tex, ws, wt, alpha, cull, flags, 0, len(indices), len(idx)))
            indices += idx
        verts = b''.join(struct.pack('<4h2f4B', x, y, z, 0, u, v, *c) for x, y, z, u, v, c in self.verts)
        return (b'PWM2' + struct.pack('<III', len(groups), len(self.verts), len(indices)) + b''.join(groups)
                + verts + struct.pack(idx_fmt % len(indices), *indices))
