"""Pokémon World: BW's rails (free-movement paths: Castelia's streets, Skyarrow Bridge, Victory Road,
Dragonspiral Tower, Gear Station...) -> tile positions, for the NPCs that stand on them.

Format from CTRMapV (ds-pokemon-hacking/CTRMapV, formats/pokemon/gen5/rail, field/rail/VRailMath):
  a/0/7/9 file: GF container "RL" of 5 parts - info (fx32 tile size), points (0x70: line[4], dir[4],
  width[4], fx32 x/y/z at 0x30), lines (0x48: point1, point2, angle, curve, camera, length in tiles),
  cameras, curves (0x24: type 0 straight / 1 arc in XZ / 2 arc in 3D, fx32 centre at 0x04).
  A spot on a rail is (line, front, side): `front` tiles along the line, `side` tiles across it.
Which zone uses which rail: a table of u16 (zone | rail index << 11) in BW's ARM9 (found by its first
entries), and the rail index's header in a/1/0/9 (u16 rail file at 0x00).
"""

import math
import struct

import ndspy.codeCompression


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def s32(b, o): return struct.unpack_from('<i', b, o)[0]


def _container(data):
    n = u16(data, 2)
    offs = [struct.unpack_from('<I', data, 4 + 4 * i)[0] for i in range(n + 1)]
    return data[:2], [data[offs[i]:offs[i + 1]] for i in range(n)]


def _parse(f):
    magic, sec = _container(f)
    if magic != b'RL' or len(sec) != 5:
        return None
    tile = s32(sec[0], 0) / 4096
    pts = [tuple(v / 4096 for v in struct.unpack_from('<3i', sec[1], o + 0x30)) for o in range(0, len(sec[1]) - 0x6F, 0x70)]
    lines = [struct.unpack_from('<6i', sec[2], o) for o in range(0, len(sec[2]) - 0x47, 0x48)]
    curves = [(s32(sec[4], o), tuple(v / 4096 for v in struct.unpack_from('<3i', sec[4], o + 4)))
              for o in range(0, len(sec[4]) - 0x23, 0x24)]
    return tile, pts, lines, curves


def zone_rails(rom):
    """{zone id: parsed rail}."""
    arm9 = bytes(rom.rom.arm9)
    try:
        arm9 = ndspy.codeCompression.decompress(arm9)
    except Exception:
        pass
    i = arm9.find(struct.pack('<2H', 28, 249 | 11 << 11))   # Castelia (rail 0), Skyarrow Bridge (rail 11)
    if i < 0:
        print('rails: zone table not found')
        return {}
    headers = rom.narc('a/1/0/9')
    files = rom.narc('a/0/7/9')
    out = {}
    while i + 2 <= len(arm9):
        v = u16(arm9, i)
        zone, idx = v & 0x7FF, v >> 11
        if not v or zone > 700 or idx >= len(headers):
            break
        rf = u16(headers[idx], 0)
        if zone not in out and rf < len(files):
            rail = _parse(files[rf])
            if rail:
                out[zone] = rail
        i += 2
    return out


def _norm(v):
    n = math.sqrt(sum(c * c for c in v)) or 1
    return tuple(c / n for c in v)


def position(rail, line, front, side):
    """(line, front, side) -> BW world (x, y, z), or None."""
    tile, pts, lines, curves = rail
    if not 0 <= line < len(lines):
        return None
    p1i, p2i, _, ci, _, length = lines[line]
    if not (0 <= p1i < len(pts) and 0 <= p2i < len(pts)):
        return None
    p1, p2 = pts[p1i], pts[p2i]
    w = front / length if length else 0
    x = side * tile * 16
    ctype, cp = curves[ci] if 0 <= ci < len(curves) else (0, (0, 0, 0))
    if ctype == 0:
        d = tuple(b - a for a, b in zip(p1, p2))
        pos = tuple(a + dd * w for a, dd in zip(p1, d))
        perp = _norm((-d[2], 0, d[0]))                 # (p2 - p1) x up
        return tuple(a + b * x for a, b in zip(pos, perp))
    c = (cp[0], p1[1] + (p2[1] - p1[1]) * w, cp[2])  # arcs around the curve's centre
    a1 = math.atan2(p1[2] - c[2], p1[0] - c[0])
    a2 = math.atan2(p2[2] - c[2], p2[0] - c[0])
    da = (a2 - a1 + math.pi) % (2 * math.pi) - math.pi
    r = math.hypot(p1[0] - c[0], p1[2] - c[2]) * (1 - w) + math.hypot(p2[0] - c[0], p2[2] - c[2]) * w
    ang = a1 + da * w
    s = (math.cos(ang), math.sin(ang))
    sgn = 1 if da < 0 else -1
    return (c[0] + s[0] * (r + x * sgn), c[1], c[2] + s[1] * (r + x * sgn))


def tile_of(rail, line, front, side):
    p = position(rail, line, front, side)
    return None if p is None else (int(p[0] // 16), int(p[2] // 16))
