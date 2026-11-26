#!/usr/bin/env python3
"""Pokémon World: converts Unova (Pokémon Black) from the owner's own ROM dump.

    PYTHONPATH=~/pylib python3 tools/pokemon_world/import_bw.py "path/to/Pokemon - Black Version.nds"

Needs ndspy (pip install --target ~/pylib ndspy). Re-runnable: it rewrites everything it makes.
The ROM itself is never copied into the repository; only the converted output is.

What it makes
  graphics/unova/chunks/<c>.bin + .json  each map chunk's 3D model (map + buildings, chunk-local, see
                                         bw/export3d.py), its texture list, tile heights and BW tile data
  graphics/unova/matrices/m<id>.json     which chunk file is in each cell of a BW map matrix
  graphics/unova/zones.json              per zone: matrix, field camera
  graphics/unova/tex/<set>/<name>.png    textures: map sets per season (map<id>), buildings (bld<pack>[i])
  data/maps/Unova_*, data/layouts/Unova_* a GBA map per rectangle of a zone's chunks (zones that aren't
                                         rectangles are split), connected like BW's seamless map; their
                                         invisible tilesets (data/tilesets/*/unova3d*) only carry behaviours
  src/data/unova_zones.h                 map -> zone, matrix and chunk position, for src/platform/web3d.c
  map sections (MAPSEC_UNOVA_*), map groups gMapGroup_Unova*, the map script includes

Formats: bw/nitro.py (NITRO models/textures); BW's map, zone and text formats are in comments here.
BW zone ids, matrices and areas are Black's (BW1), not Black 2's.
"""

import json
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bw'))
import ndspy.narc  # noqa: E402
import ndspy.rom   # noqa: E402
import encounters  # noqa: E402
import scripts     # noqa: E402
import sprites     # noqa: E402
import vmdata      # noqa: E402
import trainers    # noqa: E402
import items as bw_items  # noqa: E402
import music as bw_music  # noqa: E402
import movement as bw_movement  # noqa: E402
import scenes as bw_scenes  # noqa: E402
import export3d    # noqa: E402
import nitro       # noqa: E402

OUT = 'graphics/unova'
TILE = 16.0          # world units per tile
CHUNK = 512.0        # world units per map chunk (32 x 32 tiles)
OVERWORLD = 0        # matrix 0: Unova's outdoors
FLAG_UNOVA_START = 0x2000   # include/constants/flags.h
VAR_UNOVA_START = 0x4A00    # include/constants/vars.h
UNOVA_VARS_COUNT = 0x140


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def s16(b, o): return struct.unpack_from('<h', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def s32(b, o): return struct.unpack_from('<i', b, o)[0]


class Rom:
    def __init__(self, path):
        self.rom = ndspy.rom.NintendoDSRom.fromFile(path)
        if bytes(self.rom.idCode[:3]) != b'IRB':
            sys.exit('expected Pokémon Black (game code IRB*), got %s' % bytes(self.rom.idCode))
        self.cache = {}

    def narc(self, path):
        if path not in self.cache:
            self.cache[path] = [bytes(f) for f in ndspy.narc.NARC(self.rom.getFileByName(path)).files]
        return self.cache[path]

    def file(self, path):
        return bytes(self.rom.getFileByName(path))


def container(data):
    """Sections of a GF container ("WB", "GC", "NG", "RD", "AB"): (magic, [bytes])."""
    n = u16(data, 2)
    offs = [u32(data, 4 + 4 * i) for i in range(n + 1)]
    return data[:2], [data[offs[i]:offs[i + 1]] for i in range(n)]


# --- text (a/0/0/2 system, a/0/0/3 story): each string is encrypted with a key that starts from
# its last character (which decrypts to 0xFFFF) and is rotated right by 3 bits per character, going backwards.

def decode_text(f):
    ne = u16(f, 2)
    so = u32(f, 12)
    out = []
    for e in range(ne):
        off, ln = u32(f, so + 4 + 8 * e), u16(f, so + 8 + 8 * e)
        enc = struct.unpack_from('<%dH' % ln, f, so + off)
        key = enc[-1] ^ 0xFFFF if ln else 0
        dec = [0] * ln
        for i in range(ln - 1, -1, -1):
            dec[i] = enc[i] ^ key
            key = ((key >> 3) | (key << 13)) & 0xFFFF
        out.append(dec)
    return out


def text_plain(codes):
    s, i = '', 0
    while i < len(codes):
        c = codes[i]
        if c == 0xFFFF:
            break
        if c == 0xFFFE:
            s += '\n'
        elif c == 0xF000:
            n = codes[i + 2]
            i += 2 + n
        elif c < 0xD800 or c > 0xDFFF:
            s += chr(c)
        i += 1
    return s


PLACE_NAMES = 89     # a/0/0/2 file with the place names (index = zone header's locNameNo)


class Zone:
    def __init__(self, rom, zid):
        r = rom.narc('a/0/1/2')[0][zid * 0x30:(zid + 1) * 0x30]
        self.id = zid
        self.area = u16(r, 2)
        self.matrix = u16(r, 4)
        self.scripts = u16(r, 6)
        self.init_scripts = u16(r, 8)
        self.text = u16(r, 0x0A)
        self.bgm = [u16(r, 0x0C + 2 * i) for i in range(4)]
        enc = u16(r, 0x14) & 0x1FFF
        self.encounters = None if enc == 0x1FFF else enc
        self.entities = u16(r, 0x16)
        self.parent = u16(r, 0x18)
        self.place = u16(r, 0x1A) & 0x3FF
        self.weather = u16(r, 0x1C) & 0x3F
        self.camera = u16(r, 0x1C) >> 9
        fl = u16(r, 0x1E)
        self.cycling, self.running, self.escape_rope, self.fly = fl & 0x400, fl & 0x800, fl & 0x1000, fl & 0x2000
        self.seasonal = 2 <= self.area < 210
        rec = rom.file('a/0/1/3')[self.area * 10:self.area * 10 + 10]
        self.exterior = rec[6] == 1
        m = rom.narc('a/0/0/9')[self.matrix]
        has_zones, w, h = u32(m, 0), u16(m, 4), u16(m, 6)
        cells = struct.unpack_from('<%di' % (w * h), m, 8)
        zl = struct.unpack_from('<%di' % (w * h), m, 8 + 4 * w * h) if has_zones == 1 else None
        self.matrix_size = (w, h)
        self.chunks = [(i % w, i // w, cells[i]) for i in range(w * h)
                       if cells[i] >= 0 and (zl is None or zl[i] == zid)]

    def texture_set(self, rom, season):
        area = self.area + season if self.seasonal else self.area
        return u16(rom.file('a/0/1/3'), area * 10 + 2)

    def building_pack(self):
        area = self.area
        return area - 210 if area >= 210 else (area - 2) // 4 if area >= 2 else 0


def rot_y(deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    m = nitro.ident()
    m[0][0], m[0][2], m[2][0], m[2][2] = c, s, -s, c
    return m


# --- 3D: one file per (chunk, zone) ---

class Converter:
    def __init__(self, rom):
        self.rom = rom
        self.written_textures = set()
        self.building_cache = {}

    def buildings(self, zone):
        pack = zone.building_pack()
        key = (pack, zone.exterior)
        if key not in self.building_cache:
            bld_narc, tex_narc = ('a/2/2/9', 'a/1/7/6') if zone.exterior else ('a/2/3/0', 'a/1/7/7')
            _, ab = container(self.rom.narc(bld_narc)[pack])
            half = len(ab) // 2
            models = {s16(ab[i], 0): ab[half + i] for i in range(half)}
            self.building_cache[key] = (models, export3d.TextureSet(self.rom.narc(tex_narc)[pack]),
                                        'bld%d%s' % (pack, '' if zone.exterior else 'i'))
        return self.building_cache[key]

    def write_textures(self, dirname, texset, names=None):
        os.makedirs('%s/tex/%s' % (OUT, dirname), exist_ok=True)
        for name, data, _, _ in texset.images:
            if (dirname, name) in self.written_textures or (names is not None and name not in names):
                continue
            self.written_textures.add((dirname, name))
            with open('%s/tex/%s/%s.png' % (OUT, dirname, name), 'wb') as f:
                f.write(data)

    def chunk(self, cid, zone):
        """Converts map chunk cid as used by zone; returns its file name and BW tile data."""
        rom = self.rom
        name = 'c%da%d' % (cid, zone.area)
        magic, sec = container(rom.narc('a/0/0/8')[cid])
        map_set = zone.texture_set(rom, 0)
        map_tex = export3d.TextureSet(rom.narc('a/0/1/4')[map_set])
        models, bld_tex, bld_dir = self.buildings(zone)
        writer = export3d.MeshWriter()
        centre = nitro.trans(CHUNK / 2, 0.0, CHUNK / 2)
        map_meshes = []
        for md in nitro.load_bmd(sec[0]):
            mesh = nitro.run_sbc(md, nitro.Mesh(), centre)
            map_meshes.append(mesh)
            writer.add(mesh, md.materials, map_tex, 'map')
        tiles = None
        if magic in (b'WB', b'GC'):
            t = sec[1]
            tw, th = u16(t, 0), u16(t, 2)
            tiles = [[(u16(t, 4 + 8 * (y * tw + x) + 4), u16(t, 4 + 8 * (y * tw + x) + 6)) for x in range(tw)]
                     for y in range(th)]
        bt = sec[-1]
        for i in range(u32(bt, 0) if len(bt) >= 4 else 0):
            o = 4 + 16 * i
            bx, by, bz = s32(bt, o) / 4096, s32(bt, o + 4) / 4096, s32(bt, o + 8) / 4096
            yaw = u16(bt, o + 12) * 360 / 65536
            uid = struct.unpack_from('>H', bt, o + 14)[0]
            if uid not in models:
                continue
            # placement Z runs north (the opposite way to the map model); checked against the
            # collision grid: with -z every building of Nuvema Town sits on its blocked footprint
            m = nitro.mul(nitro.trans(CHUNK / 2 + bx, by, CHUNK / 2 - bz), rot_y(yaw))
            for md in nitro.load_bmd(models[uid]):
                writer.add(nitro.run_sbc(md, nitro.Mesh(), m), md.materials, bld_tex, 'bld')

        os.makedirs(OUT + '/chunks', exist_ok=True)
        with open('%s/chunks/%s.bin' % (OUT, name), 'wb') as f:
            f.write(writer.data())
        # textures: map ones for every season's set, building ones once
        map_names = [t['name'] for t in writer.textures if t['kind'] == 'map']
        sets = []
        for s in range(4):
            tid = zone.texture_set(rom, s)
            sets.append('map%d' % tid)
            ts = map_tex if tid == map_set else export3d.TextureSet(rom.narc('a/0/1/4')[tid])
            for n in map_names:
                tex, _, pal = n.partition('@')
                ts.index(tex, pal or tex + '_pl')
            self.write_textures('map%d' % tid, ts, set(map_names))
        self.write_textures(bld_dir, bld_tex, {t['name'] for t in writer.textures if t['kind'] == 'bld'})
        info = {'mapTextures': sets, 'buildingTextures': bld_dir, 'textures': writer.textures,
                'heights': heights(map_meshes), 'magic': magic.decode('latin1')}
        with open('%s/chunks/%s.json' % (OUT, name), 'w') as f:
            json.dump(info, f, separators=(',', ':'))
        return name, tiles


def heights(meshes):
    """Ground height (world units) under each tile's centre: the highest upward surface of the map model."""
    n = 32
    h = [[None] * n for _ in range(n)]
    for mesh in meshes:
        for tris in mesh.groups.values():
            for (a, _, _), (b, _, _), (c, _, _) in tris:
                d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
                if abs(d) < 1e-6:
                    continue      # a wall (vertical in x/z)
                xs, zs = (a[0], b[0], c[0]), (a[2], b[2], c[2])
                for tz in range(max(0, int(min(zs) // TILE)), min(n, int(max(zs) // TILE) + 1)):
                    for tx in range(max(0, int(min(xs) // TILE)), min(n, int(max(xs) // TILE) + 1)):
                        px, pz = tx * TILE + 8, tz * TILE + 8
                        l1 = ((b[2] - c[2]) * (px - c[0]) + (c[0] - b[0]) * (pz - c[2])) / d
                        l2 = ((c[2] - a[2]) * (px - c[0]) + (a[0] - c[0]) * (pz - c[2])) / d
                        l3 = 1 - l1 - l2
                        if min(l1, l2, l3) < -1e-4:
                            continue
                        y = l1 * a[1] + l2 * b[1] + l3 * c[1]
                        if h[tz][tx] is None or y > h[tz][tx]:
                            h[tz][tx] = y
    return [round(h[z][x] or 0.0, 1) for z in range(n) for x in range(n)]


def camera(rom, zone):
    cam = rom.narc('a/0/6/0')[0][zone.camera * 0x2C:(zone.camera + 1) * 0x2C]
    return {'distance': s32(cam, 0), 'pitch': s32(cam, 4) * 180 / 32768, 'yaw': s32(cam, 8) * 180 / 32768,
            'fov': math.degrees(s16(cam, 0x12) / 4096), 'near': s32(cam, 0x14) / 4096, 'far': s32(cam, 0x18) / 4096,
            'target': [s32(cam, 0x20 + 4 * i) / 4096 for i in range(3)]}


# --- the game's side: GBA maps with an invisible tileset that only carries behaviours ---
#
# The 2D map layers draw nothing in Unova (the 3D model is the picture); the map grid keeps our
# engine's walking, collision, doors and encounters working on BW's own tiles.

# names kept for maps made before the whole of Unova was converted (zone -> map name)
NAMES = {389: 'Unova_NuvemaTown', 390: 'Unova_NuvemaTown_PlayersHouse_1F', 391: 'Unova_NuvemaTown_PlayersHouse_2F',
         392: 'Unova_NuvemaTown_House2_1F', 393: 'Unova_NuvemaTown_House2_2F', 394: 'Unova_NuvemaTown_House3_1F',
         395: 'Unova_NuvemaTown_House3_2F', 396: 'Unova_NuvemaTown_JunipersLab'}
GROUP_OUTDOORS = 'gMapGroup_Unova'
GROUP_INDOORS = 'gMapGroup_UnovaIndoor%d'    # interiors, in groups of 120
INDOORS_PER_GROUP = 120   # the engine keeps map numbers in s8 (WarpData): at most 127 per group

# metatiles of the invisible tileset (data/tilesets/primary/unova3d): index -> behaviour
METATILES = ['MB_NORMAL', 'MB_TALL_GRASS', 'MB_DEEP_WATER', 'MB_JUMP_EAST', 'MB_JUMP_WEST', 'MB_JUMP_NORTH',
             'MB_JUMP_SOUTH', 'MB_NON_ANIMATED_DOOR', 'MB_SOUTH_ARROW_WARP', 'MB_NORTH_ARROW_WARP',
             'MB_WEST_ARROW_WARP', 'MB_EAST_ARROW_WARP', 'MB_LONG_GRASS', 'MB_POND_WATER', 'MB_ICE',
             'MB_SAND', 'MB_COUNTER', 'MB_PC', 'MB_TELEVISION', 'MB_BOOKSHELF', 'MB_TRASH_CAN', 'MB_SHOP_SHELF']
MT = {b: i for i, b in enumerate(METATILES)}
ARROW = {1: 'MB_SOUTH_ARROW_WARP', 2: 'MB_NORTH_ARROW_WARP', 3: 'MB_EAST_ARROW_WARP', 4: 'MB_WEST_ARROW_WARP'}


def map_const(name):
    return 'MAP_' + re.sub(r'(?<=[a-z])([A-Z])', r'_\1', name).upper()


def ident(s):
    """'Nuvema Town' -> 'NuvemaTown'."""
    s = s.replace('é', 'e').replace('É', 'E').replace('♀', 'F').replace('♂', 'M')
    return ''.join(w[:1].upper() + w[1:] for w in re.split(r'[^A-Za-z0-9]+', s) if w)


def behaviours():
    """{MB_NAME: value} from include/constants/metatile_behaviors.h (a plain enum)."""
    src = open('include/constants/metatile_behaviors.h').read()
    names = re.findall(r'^\s+(MB_\w+)\s*,', src.split('enum {', 1)[1], re.M)
    return {n: i for i, n in enumerate(names)}


def write_tileset():
    import zlib
    mb = behaviours()
    for kind, name in (('primary', 'unova3d'), ('secondary', 'unova3d_2')):
        d = 'data/tilesets/%s/%s' % (kind, name)
        os.makedirs(d + '/palettes', exist_ok=True)
        w, h = 128, 256          # 512 blank 4bpp tiles
        raw = b''.join(b'\0' + b'\0' * (w // 2) for _ in range(h))

        def chunk(t, data):
            return struct.pack('>I', len(data)) + t + data + struct.pack('>I', zlib.crc32(t + data) & 0xFFFFFFFF)
        png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 4, 3, 0, 0, 0))
               + chunk(b'PLTE', b'\0\0\0' * 16) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
        open(d + '/tiles.png', 'wb').write(png)
        for i in range(16):
            open('%s/palettes/%02d.pal' % (d, i), 'w').write('JASC-PAL\r\n0100\r\n16\r\n' + '0 0 0\r\n' * 16)
        mts = METATILES if kind == 'primary' else []
        open(d + '/metatiles.bin', 'wb').write(b'\0' * 16 * max(1, len(mts)))
        open(d + '/metatile_attributes.bin', 'wb').write(
            b''.join(struct.pack('<H', mb[b]) for b in mts) or struct.pack('<H', 0))


GRASS = {0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x21, 0x22}
INTERACT = {0xD6: 'MB_PC', 0xD8: 'MB_TELEVISION', 0xE2: 'MB_SHOP_SHELF'}


def tile_block(beh, flags):
    """Map grid entry (metatile | collision << 10 | elevation << 12) for a BW tile (behaviour, flags)."""
    blocked = beh == 0xFF or (flags & 1)
    if beh in (0x72, 0x73, 0x74, 0x75) and flags & 1:
        return MT[('MB_JUMP_EAST', 'MB_JUMP_WEST', 'MB_JUMP_NORTH', 'MB_JUMP_SOUTH')[beh - 0x72]] | 3 << 12
    if flags & 2:
        return MT['MB_DEEP_WATER'] | 1 << 12
    if beh in (0x10, 0x18):
        return MT['MB_ICE'] | (1 << 10 if blocked else 0) | 3 << 12
    if beh in INTERACT:
        return MT[INTERACT[beh]] | 1 << 10 | 3 << 12
    if beh in GRASS or flags & 4:
        return MT['MB_LONG_GRASS' if beh in (0x05, 0x07, 0x09) else 'MB_TALL_GRASS'] | (1 << 10 if blocked else 0) | 3 << 12
    return MT['MB_NORMAL'] | (1 << 10 if blocked else 0) | 3 << 12


def rectangles(chunks):
    """Splits a zone's chunk cells into rectangles (x, y, w, h), widest first, row by row."""
    left = {(x, y) for x, y, _ in chunks}
    out = []
    while left:
        y0 = min(y for _, y in left)
        x0 = min(x for x, y in left if y == y0)
        w = 1
        while (x0 + w, y0) in left:
            w += 1
        h = 1
        while all((x0 + i, y0 + h) in left for i in range(w)):
            h += 1
        out.append((x0, y0, w, h))
        left -= {(x0 + i, y0 + j) for i in range(w) for j in range(h)}
    return out


def zone_warps(rom, zone):
    """BW warps of a zone: [(tiles [(x, y)] in matrix tiles, target zone, target warp, facing, transition)]."""
    e = rom.narc('a/1/2/5')[zone.entities]
    nf, nn, nw = e[4], e[5], e[6]
    o = 8 + 0x14 * nf + 0x24 * nn
    out = []
    for i in range(nw):
        r = e[o + 0x14 * i:o + 0x14 * (i + 1)]
        tz, tw = u16(r, 0), u16(r, 2)
        face, trans, rail = r[4], r[5], u16(r, 6)
        if rail:
            out.append(([], tz, tw, face, trans))
            continue
        x, z, w, h = s16(r, 8), s16(r, 12), s16(r, 14), s16(r, 16)
        tx, ty = x // 16, z // 16
        out.append(([(tx + i, ty + j) for j in range(max(h, 1)) for i in range(max(w, 1))], tz, tw, face, trans))
    return out


class GameMap:
    def __init__(self, zone, rect, name, group):
        self.zone, self.rect, self.name, self.group = zone, rect, name, group
        self.const = map_const(name)
        x, y, w, h = rect
        self.tiles = [[(0xFF, 1)] * (w * 32) for _ in range(h * 32)]
        self.warps = []          # [(x, y, dest map, dest BW warp (zone, index))]
        self.connections = []
        self.npcs = []           # object events (dicts) and their scripts
        self.signs = []
        self.script_text = []    # lines of scripts.inc after the map scripts
        self.actor_ids = []      # (BW NPC uid, our local id)
        self.triggers = []
        self.on_load = []        # scripts run when the map loads
        self.on_frame = []       # (var, value, script)

    def contains(self, tx, ty):
        x, y, w, h = self.rect
        return x * 32 <= tx < (x + w) * 32 and y * 32 <= ty < (y + h) * 32

    def local(self, tx, ty):
        return tx - self.rect[0] * 32, ty - self.rect[1] * 32


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rom = Rom(sys.argv[1])
    conv = Converter(rom)
    places = [text_plain(s) for s in decode_text(rom.narc('a/0/0/2')[PLACE_NAMES])]
    nz = len(rom.narc('a/0/1/2')[0]) // 0x30
    zones = [Zone(rom, z) for z in range(nz)]
    zones = [z for z in zones if z.chunks]
    only = os.environ.get('PW_UNOVA_ZONES')          # testing: convert only these zones' 3D (comma list)
    only = {int(v) for v in only.split(',')} if only else None

    # 3D chunks and matrices
    matrices = {}
    zone_info = {}
    chunk_tiles = {}
    for z in zones:
        zone_info[z.id] = {'matrix': z.matrix, 'camera': camera(rom, z), 'name': places[z.place] if z.place < len(places) else ''}
        m = matrices.setdefault(z.matrix, {'width': z.matrix_size[0], 'height': z.matrix_size[1],
                                           'cells': [None] * (z.matrix_size[0] * z.matrix_size[1])})
        for cx, cy, cid in z.chunks:
            if only is None or z.id in only:
                name, tiles = conv.chunk(cid, z)
                m['cells'][cy * m['width'] + cx] = name
            else:
                _, sec = container(rom.narc('a/0/0/8')[cid])
                t = sec[1] if rom.narc('a/0/0/8')[cid][:2] in (b'WB', b'GC') else None
                tiles = [[(u16(t, 4 + 8 * (yy * 32 + xx) + 4), u16(t, 4 + 8 * (yy * 32 + xx) + 6)) for xx in range(32)]
                         for yy in range(32)] if t else None
                name = 'c%da%d' % (cid, z.area)
                if os.path.exists('%s/chunks/%s.bin' % (OUT, name)):
                    m['cells'][cy * m['width'] + cx] = name
            chunk_tiles[(z.id, cx, cy)] = tiles
        print('zone %d (%s): %d chunks' % (z.id, zone_info[z.id]['name'], len(z.chunks)), flush=True)
    os.makedirs(OUT + '/matrices', exist_ok=True)
    for mid, m in matrices.items():
        with open('%s/matrices/m%d.json' % (OUT, mid), 'w') as f:
            json.dump(m, f, separators=(',', ':'))
    with open(OUT + '/zones.json', 'w') as f:
        json.dump(zone_info, f, separators=(',', ':'), ensure_ascii=False)

    # GBA maps: rectangles of each zone, named after BW's place names
    maps = []
    indoor_count = 0
    used_names = set()
    for z in zones:
        rects = rectangles(z.chunks)
        base = NAMES.get(z.id)
        place = ident(places[z.place]) if z.place < len(places) and places[z.place] else 'Zone%d' % z.id
        for i, rect in enumerate(rects):
            if base and len(rects) == 1:
                name = base
            elif z.matrix == OVERWORLD:
                name = 'Unova_%s' % place + ('_%d' % (i + 1) if len(rects) > 1 else '')
            else:
                name = 'Unova_%s_Z%d' % (place, z.id) + ('_%d' % (i + 1) if len(rects) > 1 else '')
            while name in used_names:
                name += 'B'
            used_names.add(name)
            if z.matrix == OVERWORLD:
                group = GROUP_OUTDOORS
            else:
                group = GROUP_INDOORS % (indoor_count // INDOORS_PER_GROUP + 1)
                indoor_count += 1
            gm = GameMap(z, rect, name, group)
            for cx, cy, _ in z.chunks:
                if gm.contains(cx * 32, cy * 32):
                    t = chunk_tiles[(z.id, cx, cy)]
                    for yy in range(32):
                        for xx in range(32):
                            lx, ly = gm.local(cx * 32 + xx, cy * 32 + yy)
                            gm.tiles[ly][lx] = t[yy][xx] if t else (0, 0x80)
            maps.append(gm)
    by_zone = {}
    for gm in maps:
        by_zone.setdefault(gm.zone.id, []).append(gm)

    # warps: BW warp (zone, index) -> (map, our warp event index)
    warp_event = {}
    for z in zones:
        for wi, (tiles, tz, tw, face, trans) in enumerate(zone_warps(rom, z)):
            for (tx, ty) in tiles:
                for gm in by_zone[z.id]:
                    if gm.contains(tx, ty):
                        lx, ly = gm.local(tx, ty)
                        warp_event.setdefault((z.id, wi), (gm, len(gm.warps)))
                        if trans == 3 or (z.exterior and trans != 1):
                            gm.tiles[ly][lx] = ('door', None)
                        else:
                            gm.tiles[ly][lx] = ('arrow', face)
                        gm.warps.append((lx, ly, tz, tw))
                        break

    # connections between rectangles that touch, in the same matrix
    for a in maps:
        for b in maps:
            if a is b or a.zone.matrix != b.zone.matrix:
                continue
            ax, ay, aw, ah = a.rect
            bx, by, bw, bh = b.rect
            if bx == ax + aw and by < ay + ah and by + bh > ay:
                a.connections.append({'map': b.const, 'offset': (by - ay) * 32, 'direction': 'right'})
            if bx + bw == ax and by < ay + ah and by + bh > ay:
                a.connections.append({'map': b.const, 'offset': (by - ay) * 32, 'direction': 'left'})
            if by == ay + ah and bx < ax + aw and bx + bw > ax:
                a.connections.append({'map': b.const, 'offset': (bx - ax) * 32, 'direction': 'down'})
            if by + bh == ay and bx < ax + aw and bx + bw > ax:
                a.connections.append({'map': b.const, 'offset': (bx - ax) * 32, 'direction': 'up'})

    # Black's own key items first (so items.h knows them), then trainers, then the people on the maps
    bw_items.write(rom, decode_text, text_plain)
    item_names = [vmdata.scripts_plain(s) for s in decode_text(rom.narc('a/0/0/2')[54])]
    known_items = vmdata.const_names('include/constants/items.h', 'ITEM_')

    def item_of(bw):
        return vmdata.item_const(item_names[bw], known_items) if 0 < bw < len(item_names) and item_names[bw] else None
    dex = encounters.species_names()

    def species_of(n, form):
        if n in encounters.FORMS and form < len(encounters.FORMS[n]):
            return encounters.FORMS[n][form]
        return dex.get(n)
    tr_consts, btypes = trainers.write(rom, decode_text, item_of, species_of)
    people = People(rom, tr_consts, btypes, trainers.texts(rom, decode_text), item_of)
    trainers.write_defeat_texts(people.texts)
    add_people(rom, zones, by_zone, people)
    bw_movement.write_c()
    check_scenes(rom, zones, by_zone, places)
    # Black's music: every zone's songs (four seasons each) and the ones our code plays in Unova
    seq_names = bw_music.names(rom.rom)
    want, seasonal = [], []
    for z in zones:
        ns = [seq_names[b] if b < len(seq_names) else None for b in z.bgm]
        want += [n for n in ns if n and n.startswith('SEQ_BGM_') and 'SILENCE' not in n and 'DUMMY' not in n]
        if all(ns) and len(set(ns)) > 1:
            seasonal.append(tuple(ns))
    want = list(dict.fromkeys(want + bw_music.ENGINE_SONGS))
    songs = bw_music.write(rom.rom, want, 0)
    bw_music.write_seasons(songs, seasonal)
    with open('src/data/bw_songs.h', 'w') as f:    # for BW's BGMPlay commands (src/bw_script.c)
        f.write('// Generated by tools/pokemon_world/import_bw.py: BW sound id -> our song.\n'
                'static const u16 sBwSongs[][2] =\n{\n')
        f.write(''.join('    {%d, %s},\n' % (i, songs[n]) for i, n in enumerate(seq_names) if n in songs))
        f.write('};\n')
    for z in zones:
        n = seq_names[z.bgm[0]] if z.bgm[0] < len(seq_names) else None
        ZONE_MUSIC[z.id] = songs.get(n) or ('MUS_NONE' if n and 'SILENCE' in n else None)
    write_tileset()
    write_game_maps(maps, warp_event, places)
    label = {gm.const: 'g' + gm.name for gm in maps}
    n = encounters.write(rom, [(gm.const, gm.zone) for gm in maps], label.get)
    print('%d wild encounter tables' % n)
    vmdata.write(rom, sys.argv[1], zones, maps, places, decode_text, encounters.species_names())
    print('%d zones, %d maps' % (len(zones), len(maps)))


FACE = {0: 'MOVEMENT_TYPE_FACE_UP', 1: 'MOVEMENT_TYPE_FACE_DOWN', 2: 'MOVEMENT_TYPE_FACE_LEFT', 3: 'MOVEMENT_TYPE_FACE_RIGHT'}
NPC_LIMIT = 60     # object event templates per map (Emerald allows 64)


def check_scenes(rom, zones, by_zone, places):
    """Simulates every BW scene's movements on our maps (bw/scenes.py): a step into a wall means the
    conversion of a command or a position is off."""
    def blocked(zid, x, z):
        for gm in by_zone.get(zid, []):
            if gm.contains(x, z):
                lx, ly = gm.local(x, z)
                t = gm.tiles[ly][lx]
                if t[0] in ('door', 'arrow'):
                    return False
                return bool(tile_block(*t) & (1 << 10))
        return None
    starts, triggers = {}, {}
    for z in zones:
        e = rom.narc('a/1/2/5')[z.entities]
        nf, nn, nw, nt = e[4], e[5], e[6], e[7]
        for i in range(nn):
            r = e[8 + 0x14 * nf + 0x24 * i:8 + 0x14 * nf + 0x24 * (i + 1)]
            if not u32(r, 0x18):
                starts.setdefault(z.id, {})[u16(r, 0)] = (u16(r, 0x1C), u16(r, 0x1E))
        o = 8 + 0x14 * nf + 0x24 * nn + 0x14 * nw
        for i in range(nt):
            r = e[o + 0x16 * i:o + 0x16 * (i + 1)]
            gx, gz = struct.unpack_from('<hh', r, 10)
            triggers.setdefault(z.id, {}).setdefault(u16(r, 0), (gx, gz))
    names = {z.id: places[z.place] if z.place < len(places) else '' for z in zones}
    bw_scenes.check(rom, zones, blocked, starts, triggers, names)
    # the same inputs, for running the check on its own while working on it (bw/scenes.py main)
    grid = {}
    for z in zones:
        for gm in by_zone.get(z.id, []):
            x0, y0 = gm.rect[0] * 32, gm.rect[1] * 32
            for ly, row in enumerate(gm.tiles):
                for lx, t in enumerate(row):
                    grid[(z.id, x0 + lx, y0 + ly)] = blocked(z.id, x0 + lx, y0 + ly)
    import pickle
    with open(os.path.expanduser('~/.cache_bw_scenes.pickle'), 'wb') as f:
        pickle.dump({'grid': grid, 'starts': starts, 'triggers': triggers, 'names': names,
                     'zones': [(z.id, z.scripts) for z in zones]}, f)


ZONE_MUSIC = {}               # BW zone -> its (spring) song, MUS_BW_*
BW_HIDDEN_FLAG_BASE = 900     # BW's hidden item flags start here (UNOVA_HIDDEN_ITEM_FLAG_BASE in flags.h)


class People:
    """What the NPC conversion needs from the other converters."""
    def __init__(self, rom, trainers, btypes, texts, item_of):
        self.trainers, self.btypes, self.texts, self.item_of = trainers, btypes, texts, item_of
        self.balls = rom.narc('a/0/5/7')[864]
        self.ball_entries = scripts.entries(self.balls)
        self.hidden = rom.narc('a/0/5/7')[865]
        self.hidden_entries = scripts.entries(self.hidden)


def text_block(gm, label, codes):
    gm.script_text.append('%s:\n\t.string "%s$"\n\n' % (label, scripts.to_script_string(codes) if codes else '...'))


def trainer_script(gm, label, tid, people):
    """A BW trainer NPC (script 3000 + trainer id) -> trainerbattle with BW's lines."""
    t = people.texts.get(tid, {})
    intro, defeat, post = (t.get(0) or t.get(3)), (t.get(1) or t.get(4)), (t.get(2) or t.get(5))
    const = people.trainers[tid]
    if people.btypes.get(tid) in (1, 2):
        battle = '\ttrainerbattle_double %s, %s_Intro, %s_Defeat, %s_NotEnough\n' % (const, label, label, label)
    else:
        battle = '\ttrainerbattle_single %s, %s_Intro, %s_Defeat\n' % (const, label, label)
    gm.script_text.append('%s::\n%s\tmsgbox %s_Post, MSGBOX_AUTOCLOSE\n\tend\n\n' % (label, battle, label))
    text_block(gm, label + '_Intro', intro)
    text_block(gm, label + '_Defeat', defeat)
    text_block(gm, label + '_Post', post or defeat)
    if people.btypes.get(tid) in (1, 2):
        gm.script_text.append('%s_NotEnough:\n\t.string "You need two Pokémon to battle us!$"\n\n' % label)


def item_ball(gm, label, scrid, people):
    """BW's item balls (scripts 7000+: var 0x800C = item, 0x800D = how many) -> finditem."""
    k = scrid - 7000
    if k >= len(people.ball_entries):
        return False
    vals = {a[0]: a[1] for op, a in scripts.walk(people.balls, people.ball_entries[k], 8) if op == 0x28}
    item = people.item_of(vals.get(0x800C, 0))
    if not item:
        return False
    gm.script_text.append('%s::\n\tfinditem %s, %d\n\tend\n\n' % (label, item, max(1, vals.get(0x800D, 1))))
    return True


def hidden_item(scrid, people):
    """BW's hidden items (scripts 8000+: var 0x8000 = item, 0x8001 = how many, 0x8002 = its flag)."""
    k = scrid - 8000
    if k >= len(people.hidden_entries):
        return None
    vals = {a[0]: a[1] for op, a in scripts.walk(people.hidden, people.hidden_entries[k], 8) if op == 0x2A}
    item = people.item_of(vals.get(0x8000, 0))
    flag = vals.get(0x8002, 0)
    if not item or not BW_HIDDEN_FLAG_BASE <= flag < BW_HIDDEN_FLAG_BASE + 0x200:
        return None
    return item, min(127, max(1, vals.get(0x8001, 1))), flag


def add_people(rom, zones, by_zone, people):
    """BW's NPCs and signs (zone entities, a/1/2/5) -> object events and bg events, with what they say."""
    stats = {'npcs': 0, 'hidden': 0, 'signs': 0, 'triggers': 0, 'trainers': 0, 'items': 0, 'hidden_items': 0}
    objs = set()
    per_zone = {}
    for z in zones:
        e = rom.narc('a/1/2/5')[z.entities]
        nf, nn = e[4], e[5]
        furn = [e[8 + 0x14 * i:8 + 0x14 * (i + 1)] for i in range(nf)]
        npcs = [e[8 + 0x14 * nf + 0x24 * i:8 + 0x14 * nf + 0x24 * (i + 1)] for i in range(nn)]
        per_zone[z.id] = (furn, npcs)
        for r in npcs:
            objs.add(u16(r, 2))
    objs |= scripts.actor_new_objs(rom)      # characters cutscenes bring in
    gfx = sprites.write(rom, objs)
    with open('src/data/bw_obj_gfx.h', 'w') as f:  # for ActorNew (src/bw_script.c)
        f.write('// Generated by tools/pokemon_world/import_bw.py: BW objCode -> our object graphics.\n'
                'static const u16 sBwObjGfx[][2] =\n{\n'
                + ''.join('    {%d, %s},\n' % (o, g) for o, g in sorted(gfx.items())) + '};\n')
    # flags BW's scripts clear somewhere: characters with one of these as their spawn flag show up
    # later in the story, so they start hidden (src/bw_script.c sets these the first time you enter Unova)
    cleared = scripts.flags_cleared(rom)
    later = set()
    for z in zones:
        furn, npcs = per_zone[z.id]
        maps = by_zone[z.id]

        def find(tx, ty):
            for gm in maps:
                if gm.contains(tx, ty):
                    return gm
            return None

        def run(gm, label, scrid):
            gm.script_text.append('%s::\n\tbwscript %d, %d\n\tend\n\n' % (label, z.id, scrid))

        for r in npcs:
            uid, obj, move, evtype, flag, scrid, face = struct.unpack_from('<7H', r, 0)
            if u32(r, 0x18):
                continue                 # on a rail (BW's free-movement paths): not yet
            gx, gz = u16(r, 0x1C), u16(r, 0x1E)
            gm = find(gx, gz)
            if gm is None or len(gm.npcs) >= NPC_LIMIT:
                continue
            stats['npcs'] += 1
            if flag:
                stats['hidden'] += 1
                if flag in cleared:
                    later.add(flag)
            lx, ly = gm.local(gx, gz)
            label = '%s_EventScript_Npc%d' % (gm.name, uid)
            sight = 0
            if 3000 <= scrid < 7000 and scrid - 3000 in people.trainers:
                trainer_script(gm, label, scrid - 3000, people)    # BW's trainer scripts: ours
                sight = max(1, u16(r, 0x0E))
                stats['trainers'] += 1
            elif 7000 <= scrid < 8000 and item_ball(gm, label, scrid, people):
                stats['items'] += 1
            else:
                run(gm, label, scrid)
            wander = move in (2, 3)
            gm.npcs.append({'graphics_id': gfx.get(obj, 'OBJ_EVENT_GFX_BOY_1'), 'x': lx, 'y': ly, 'elevation': 3,
                            'movement_type': 'MOVEMENT_TYPE_WANDER_AROUND' if wander else FACE.get(face, 'MOVEMENT_TYPE_FACE_DOWN'),
                            'movement_range_x': 1 if wander else 0, 'movement_range_y': 1 if wander else 0,
                            'trainer_type': 'TRAINER_TYPE_NORMAL' if sight else 'TRAINER_TYPE_NONE',
                            'trainer_sight_or_berry_tree_id': str(sight),
                            'script': label, 'flag': '0x%X' % (FLAG_UNOVA_START + flag) if flag else '0'})
            gm.actor_ids.append((uid, len(gm.npcs)))
        for i, r in enumerate(furn):
            scrid = u16(r, 0)
            if u16(r, 6):
                continue
            gx, gz = s32(r, 8), s32(r, 12)
            gm = find(gx, gz)
            if gm is None:
                continue
            lx, ly = gm.local(gx, gz)
            if 8000 <= scrid < 9000:
                hid = hidden_item(scrid, people)
                if hid:
                    item, qty, bwflag = hid
                    gm.signs.append({'type': 'hidden_item', 'x': lx, 'y': ly, 'elevation': 0, 'item': item,
                                     'flag': '(FLAG_HIDDEN_ITEMS_START+UNOVA_HIDDEN_ITEM_ID_BASE+%d)' % (bwflag - BW_HIDDEN_FLAG_BASE),
                                     'quantity': qty, 'underfoot': False})
                    stats['hidden_items'] += 1
                continue
            label = '%s_EventScript_Sign%d' % (gm.name, i)
            run(gm, label, scrid)
            stats['signs'] += 1
            gm.signs.append({'type': 'sign', 'x': lx, 'y': ly, 'elevation': 0,
                             'player_facing_dir': 'BG_EVENT_PLAYER_FACING_ANY', 'script': label})
        # step-on triggers: run their script when the BW work variable has the value
        e = rom.narc('a/1/2/5')[z.entities]
        nf, nn, nw, nt = e[4], e[5], e[6], e[7]
        o = 8 + 0x14 * nf + 0x24 * nn + 0x14 * nw
        for i in range(nt):
            r = e[o + 0x16 * i:o + 0x16 * (i + 1)]
            scrid, ref, work, _, rail = struct.unpack_from('<4Hh', r, 0)
            if rail or not (0x4000 <= work < 0x4000 + UNOVA_VARS_COUNT):
                continue
            gx, gz, w, h = struct.unpack_from('<hhHH', r, 10)
            label = None
            for yy in range(gz, gz + max(h, 1)):
                for xx in range(gx, gx + max(w, 1)):
                    gm = find(xx, yy)
                    if gm is None:
                        continue
                    if label is None or not label.startswith(gm.name + '_'):
                        label = '%s_EventScript_Trigger%d' % (gm.name, i)
                        if label + '::' not in ''.join(gm.script_text):
                            run(gm, label, scrid)
                    lx, ly = gm.local(xx, yy)
                    gm.triggers.append({'type': 'trigger', 'x': lx, 'y': ly, 'elevation': 0,
                                        'var': '0x%X' % (VAR_UNOVA_START + work - 0x4000), 'var_value': str(ref),
                                        'script': label})
                    stats['triggers'] += 1
        # BW's zone init scripts (after the triggers): tag 1 = a per-frame table of (work, value, script),
        # other tags = scripts run when the zone loads
        p0 = 4 + u32(e, 0)
        init, frame = [], []
        q = p0
        while q + 2 <= len(e):
            tag = u16(e, q)
            if tag == 0:
                break
            if tag == 1:
                t = q + 6 + s32(e, q + 2)
                while t + 2 <= len(e) and u16(e, t):
                    frame.append((u16(e, t), u16(e, t + 2), u16(e, t + 4)))
                    t += 6
            else:
                init.append((tag, u16(e, q + 2)))
            q += 6
        for gm in maps:
            for tag, scrid in init:
                label = '%s_OnLoad_%d' % (gm.name, scrid)
                gm.on_load.append(label)
                gm.script_text.append('%s:\n\tbwscript_init %d, %d\n\treturn\n\n' % (label, z.id, scrid))
            for work, value, scrid in frame:
                if 0x4000 <= work < 0x4000 + UNOVA_VARS_COUNT:
                    label = '%s_OnFrame_%d' % (gm.name, scrid)
                    gm.on_frame.append(('0x%X' % (VAR_UNOVA_START + work - 0x4000), value, label))
                    if label + '::' not in ''.join(gm.script_text):
                        run(gm, label, scrid)
    with open('src/data/bw_start_flags.h', 'w') as f:
        f.write('// Generated by tools/pokemon_world/import_bw.py: BW flags set the first time the player enters\n'
                '// Unova, hiding characters who only appear later in the story (their scripts clear the flag).\n'
                'static const u16 sBwStartFlags[] = {%s};\n' % ', '.join(str(x) for x in sorted(later)))
    print('people: %(npcs)d NPCs (%(hidden)d with story flags, %(trainers)d trainers, %(items)d item balls), %(signs)d signs/objects, %(hidden_items)d hidden items, %(triggers)d trigger tiles' % stats)
    print('%d overworld sprites' % len(gfx))


def mapsec_for(z, places, zones_by_id):
    """Map section id and name of a zone: its own place name, or its parent's for interiors without one."""
    name = places[z.place] if z.place < len(places) else ''
    if not name or name.startswith('－'):
        p = zones_by_id.get(z.parent)
        name = places[p.place] if p and p.place < len(places) else 'Unova'
    if '－' in name:
        name = 'Unova'
    return 'MAPSEC_UNOVA_' + re.sub(r'[^A-Z0-9]+', '_', name.upper().replace('É', 'E')).strip('_'), name


def map_scripts(gm):
    """The map's header: BW's on-load scripts on transition, its per-frame table."""
    out = '%s_MapScripts::\n' % gm.name
    if gm.on_load:
        out += '\tmap_script MAP_SCRIPT_ON_TRANSITION, %s_OnTransition\n' % gm.name
    if gm.on_frame:
        out += '\tmap_script MAP_SCRIPT_ON_FRAME_TABLE, %s_OnFrameTable\n' % gm.name
    out += '\t.byte 0\n\n'
    if gm.on_load:
        out += '%s_OnTransition:\n' % gm.name + ''.join('\tcall %s\n' % l for l in gm.on_load) + '\tend\n\n'
    if gm.on_frame:
        out += '%s_OnFrameTable:\n' % gm.name + ''.join('\tmap_script_2 %s, %d, %s\n' % (v, val, l) for v, val, l in gm.on_frame) + '\t.2byte 0\n\n'
    return out


def write_game_maps(maps, warp_event, places):
    zones_by_id = {gm.zone.id: gm.zone for gm in maps}
    layouts = json.load(open('data/layouts/layouts.json'))
    groups = json.load(open('data/maps/map_groups.json'))
    layouts['layouts'] = [l for l in layouts['layouts'] if not l.get('id', '').startswith('LAYOUT_UNOVA_')]
    for g in [g for g in groups['group_order'] if g.startswith('gMapGroup_Unova')]:
        groups['group_order'].remove(g)
        del groups[g]
    for gm in maps:
        if gm.group not in groups:
            groups['group_order'].append(gm.group)
            groups[gm.group] = []
    # old maps' folders are removed so renamed ones don't linger
    for d in ('data/maps', 'data/layouts'):
        for n in os.listdir(d):
            if n.startswith('Unova_') and n not in {gm.name for gm in maps}:
                import shutil
                shutil.rmtree(os.path.join(d, n))
    mapsecs = [('MAPSEC_UNOVA_NUVEMA_TOWN', 'Nuvema Town')]   # first: regions.h's UNOVA_MAPSEC_START
    zone_table = []
    for gm in maps:
        z = gm.zone
        w, h = gm.rect[2] * 32, gm.rect[3] * 32
        grid = []
        for row in gm.tiles:
            out = []
            for t in row:
                if t[0] == 'door':
                    out.append(MT['MB_NON_ANIMATED_DOOR'] | 3 << 12)
                elif t[0] == 'arrow':
                    out.append(MT[ARROW.get(t[1], 'MB_SOUTH_ARROW_WARP')] | 3 << 12)
                else:
                    out.append(tile_block(*t))
            grid.append(out)
        events = []
        for lx, ly, tz, tw in gm.warps:
            dest = warp_event.get((tz, tw))
            if dest is None:
                continue
            events.append({'x': lx, 'y': ly, 'elevation': 0, 'dest_map': dest[0].const, 'dest_warp_id': str(dest[1])})
        os.makedirs('data/layouts/' + gm.name, exist_ok=True)
        with open('data/layouts/%s/map.bin' % gm.name, 'wb') as f:
            f.write(b''.join(struct.pack('<H', v) for row in grid for v in row))
        with open('data/layouts/%s/border.bin' % gm.name, 'wb') as f:
            f.write(struct.pack('<4H', *([MT['MB_NORMAL'] | 1 << 10 | 3 << 12] * 4)))
        lid = 'LAYOUT_' + gm.const[4:]
        layouts['layouts'].append({'id': lid, 'name': gm.name + '_Layout', 'width': w, 'height': h,
                                   'primary_tileset': 'gTileset_Unova3D', 'secondary_tileset': 'gTileset_Unova3D_2',
                                   'border_filepath': 'data/layouts/%s/border.bin' % gm.name,
                                   'blockdata_filepath': 'data/layouts/%s/map.bin' % gm.name})
        sec, secname = mapsec_for(z, places, zones_by_id)
        if sec not in [s for s, _ in mapsecs]:
            mapsecs.append((sec, secname))
        place = secname.lower()
        maptype = ('MAP_TYPE_ROUTE' if 'route' in place else 'MAP_TYPE_CITY' if 'city' in place else 'MAP_TYPE_TOWN') \
            if z.exterior else 'MAP_TYPE_INDOOR'
        music = ZONE_MUSIC.get(z.id) or ('MUS_LITTLEROOT' if maptype in ('MAP_TYPE_TOWN', 'MAP_TYPE_CITY') or not z.exterior else 'MUS_ROUTE101')
        m = {'id': gm.const, 'name': gm.name, 'layout': lid, 'music': music, 'region_map_section': sec,
             'requires_flash': False, 'weather': 'WEATHER_NONE', 'map_type': maptype, 'allow_cycling': bool(z.cycling),
             'allow_escaping': bool(z.escape_rope), 'allow_running': True, 'show_map_name': z.exterior,
             'battle_scene': 'MAP_BATTLE_SCENE_NORMAL', 'connections': gm.connections or None,
             'object_events': gm.npcs, 'warp_events': events, 'coord_events': gm.triggers, 'bg_events': gm.signs}
        os.makedirs('data/maps/' + gm.name, exist_ok=True)
        with open('data/maps/%s/map.json' % gm.name, 'w') as f:
            json.dump(m, f, indent=2, ensure_ascii=False); f.write('\n')
        with open('data/maps/%s/scripts.inc' % gm.name, 'w') as f:
            f.write('@ Generated by tools/pokemon_world/import_bw.py from Pokémon Black\'s scripts and text.\n')
            f.write(map_scripts(gm))
            f.write(''.join(gm.script_text))
        groups[gm.group].append(gm.name)
        zone_table.append((gm.const, z.id, z.matrix, gm.rect[0], gm.rect[1]))
    with open('data/layouts/layouts.json', 'w') as f:
        json.dump(layouts, f, indent=2, ensure_ascii=False); f.write('\n')
    with open('data/maps/map_groups.json', 'w') as f:
        json.dump(groups, f, indent=2, ensure_ascii=False); f.write('\n')

    # place names, in a block at the end (regions.h: UNOVA_MAPSEC_START)
    rms = json.load(open('src/data/region_map/region_map_sections.json'))
    rms['map_sections'] = [s for s in rms['map_sections'] if not s['id'].startswith('MAPSEC_UNOVA_')
                           and s['id'] != 'MAPSEC_NUVEMA_TOWN']
    for sec, name in mapsecs:
        rms['map_sections'].append({'id': sec, 'name': name.upper().replace('É', 'é')})   # like POKéMON
    with open('src/data/region_map/region_map_sections.json', 'w', encoding='utf-8') as f:
        json.dump(rms, f, indent=2, ensure_ascii=False); f.write('\n')

    with open('src/data/unova_zones.h', 'w') as f:
        f.write('// Generated by tools/pokemon_world/import_bw.py: the BW zone each Unova map shows in 3D,\n'
                '// its BW map matrix and where the map\'s top-left chunk is in that matrix.\n')
        f.write('static const struct { u16 map; u16 zone; u16 matrix; u8 chunkX; u8 chunkY; } sUnovaZones[] =\n{\n')
        for mc, zid, mx, cx, cy in zone_table:
            f.write('    { %s, %d, %d, %d, %d },\n' % (mc, zid, mx, cx, cy))
        f.write('};\n')
        f.write('#define UNOVA_MAPSEC_FIRST %s\n' % mapsecs[0][0])

    es = open('data/event_scripts.s').read()
    inc = ''.join('\t.include "data/maps/%s/scripts.inc"\n' % gm.name for gm in maps)
    marker = '@ Pokémon World: Unova (tools/pokemon_world/import_bw.py)\n'
    start = es.index(marker)
    end = es.index('@ end of Unova\n', start) + len('@ end of Unova\n')
    es = es[:start] + marker + inc + '@ end of Unova\n' + es[end:]
    with open('data/event_scripts.s', 'w') as f:
        f.write(es)


if __name__ == '__main__':
    main()
