#!/usr/bin/env python3
"""Pokémon World web build, debugging aid: draw one text-mode background from a
memory dump made by drive.js ["vram", name] (IO regs, palette, VRAM, OAM).
usage: render_bg.py dump.bin bg out.png   (needs Pillow)"""
import struct, sys
from PIL import Image
d = open(sys.argv[1], 'rb').read()
bg = int(sys.argv[2])
io, pal, vram = d[:0x400], d[0x400:0x800], d[0x800:0x18800]
cnt = struct.unpack_from('<H', io, 8 + 2 * bg)[0]
cb, sb, bpp8 = (cnt >> 2) & 3, (cnt >> 8) & 0x1F, (cnt >> 7) & 1
print(f'BG{bg}CNT={cnt:#06x} charbase={cb} screenbase={sb:#x} 8bpp={bpp8} prio={cnt & 3}')
colors = struct.unpack('<512H', pal)
img = Image.new('RGB', (256, 256))
px = img.load()
for ty in range(32):
    for tx in range(32):
        e = struct.unpack_from('<H', vram, sb * 0x800 + (ty * 32 + tx) * 2)[0]
        tile, hf, vf, pn = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1, e >> 12
        base = cb * 0x4000 + tile * 32
        for y in range(8):
            for x in range(8):
                sx, sy = (7 - x if hf else x), (7 - y if vf else y)
                if base + 32 > 0x10000:
                    c = 0
                else:
                    byte = vram[base + sy * 4 + sx // 2]
                    c = (byte >> 4) if sx & 1 else (byte & 0xF)
                col = colors[pn * 16 + c] if c else 0
                px[tx * 8 + x, ty * 8 + y] = ((col & 31) * 8, ((col >> 5) & 31) * 8, ((col >> 10) & 31) * 8)
img.save(sys.argv[3])
