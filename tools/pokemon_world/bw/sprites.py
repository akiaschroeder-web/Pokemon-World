"""Pokémon World: Pokémon Black's overworld characters -> object event sprites.

BW's moving models (registry a/0/4/8 file 0: u32 count, 0x1C-byte entries; resources a/0/4/9) are,
for people, an NSBTX with one 32x32 16-colour texture per animation frame and one palette
(CTRMapV VNPCRegistry / VMoveModel). The textures are named <set>.<n>; the sets seen:
  t4x4nflip (12 frames), t4x4x14, t4x4x20 ...: up, down, left, right x (idle, step, step), then extras
  t4x4flip (7 frames): up (idle, step), down (idle, step), left (idle, step, step); the other foot's
                       step is the same frame mirrored, and right is left mirrored
Other objects (one frame, Pokémon, ...) become one still frame for now.

They become Emerald-layout sheets (9 frames: face S, N, W, then S, S, N, N, W, W steps; east is west
flipped) in graphics/object_events/pics/unova/, and generated headers like Heart & Soul's
(src/data/object_events/*_unova.h, the UNOVA block of include/constants/event_objects.h).
"""

import os
import re
import struct

import nitro

PIC_DIR = 'graphics/object_events/pics/unova'
PAL_TAG_START = 0x2100     # OBJ_EVENT_PAL_TAG_UNOVA_*: after Heart & Soul's 0x1Fxx tags


def registry_index(obj):
    if obj < 223:
        return obj
    if 4096 <= obj < 4662:
        return obj - 3873
    if 8192 <= obj < 8202:
        return obj - 7403
    return 10


def frames_of(rom, obj):
    """(frames [RGBA bytes 32x32], palette [16 RGB555], size, layout) for objCode, or None."""
    reg = rom.narc('a/0/4/8')[0]
    i = registry_index(obj)
    if i >= struct.unpack_from('<I', reg, 0)[0]:
        return None
    e = reg[4 + 0x1C * i:4 + 0x1C * (i + 1)]
    res = struct.unpack_from('<H', e, 0x10)[0]
    if res < 6 or res >= len(rom.narc('a/0/4/9')):
        return None                  # a full 3D model, not a sprite
    f = rom.narc('a/0/4/9')[res]
    if f[:4] != b'BTX0':
        return None
    tx, pl, buf = nitro.load_btx(f)
    if not tx or not pl:
        return None
    pal_off = list(pl.values())[0]
    pal = [struct.unpack_from('<H', buf, pal_off + 2 * k)[0] for k in range(16)]

    def order(n):
        m = re.search(r'\.(\d+)$', n)
        return int(m.group(1)) if m else 0
    names = sorted(tx, key=order)
    texs = [tx[n] for n in names]
    if any(t.format != 3 for t in texs):
        return None                  # only 16-colour sprites for now
    w, h = texs[0].width, texs[0].height
    frames = []
    for t in texs:
        if (t.width, t.height) != (w, h):
            return None
        d = t.data_off
        idx = bytearray(w * h)
        for k in range(w * h):
            b = buf[d + k // 2]
            idx[k] = (b >> 4) if k & 1 else (b & 15)
        frames.append(bytes(idx))
    layout = names[0].rsplit('.', 1)[0]
    return frames, pal, (w, h), layout


def mirror(frame, w, h):
    return b''.join(frame[y * w:(y + 1) * w][::-1] for y in range(h))


def emerald_frames(frames, w, h, layout):
    """The 9 Emerald frames (or 1 for a still object)."""
    n = len(frames)
    if layout.endswith('flip') and not layout.endswith('nflip') and n >= 7:
        f = frames
        return [f[2], f[0], f[4], f[3], mirror(f[3], w, h), f[1], mirror(f[1], w, h), f[5], f[6]]
    if n >= 12:
        f = frames
        return [f[3], f[0], f[6], f[4], f[5], f[1], f[2], f[7], f[8]]
    return [frames[0]]


def png_indexed(path, w, h, frames, pal):
    import zlib

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    W = w * len(frames)
    rows = []
    for y in range(h):
        row = b''.join(f[y * w:(y + 1) * w] for f in frames)
        rows.append(b'\0' + bytes((row[i] << 4) | row[i + 1] for i in range(0, W, 2)))
    plte = b''.join(bytes(((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31)) for c in pal)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, h, 4, 3, 0, 0, 0))
                + chunk(b'PLTE', plte) + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b''))


def write(rom, objs):
    """Converts the given objCodes; returns {objCode: OBJ_EVENT_GFX_ constant}."""
    os.makedirs(PIC_DIR, exist_ok=True)
    done = {}
    gfx, pics, infos, pals, ptrs, externs, consts, tags = [], [], [], [], [], [], [], []
    for obj in sorted(objs):
        r = frames_of(rom, obj)
        if not r:
            continue
        frames, pal, (w, h), layout = r
        em = emerald_frames(frames, w, h, layout)
        name = 'obj%d' % obj
        camel = 'UnovaObj%d' % obj
        const = 'OBJ_EVENT_GFX_UNOVA_OBJ%d' % obj
        tag = 'OBJ_EVENT_PAL_TAG_UNOVA_OBJ%d' % obj
        png_indexed('%s/%s.png' % (PIC_DIR, name), w, h, em, pal)
        tw, th = w // 8, h // 8
        gfx.append('const u32 gObjectEventPic_%s[] = INCGFX_U32("%s/%s.png", ".4bpp", "-mwidth %d -mheight %d");\n'
                   % (camel, PIC_DIR, name, tw, th))
        gfx.append('const u16 gObjectEventPal_%s[] = INCGFX_U16("%s/%s.png", ".gbapal");\n' % (camel, PIC_DIR, name))
        pics.append('static const struct SpriteFrameImage sPicTable_%s[] = {\n%s};\n\n' % (
            camel, ''.join('    overworld_frame(gObjectEventPic_%s, %d, %d, %d),\n' % (camel, tw, th, k) for k in range(len(em)))))
        size = '%dx%d' % (w, h)
        infos.append('''const struct ObjectEventGraphicsInfo gObjectEventGraphicsInfo_%s = {
    .tileTag = TAG_NONE,
    .paletteTag = %s,
    .reflectionPaletteTag = OBJ_EVENT_PAL_TAG_NONE,
    .size = %d,
    .width = %d,
    .height = %d,
    .paletteSlot = PALSLOT_NPC_1,
    .shadowSize = SHADOW_SIZE_M,
    .inanimate = %s,
    .compressed = FALSE,
    .tracks = TRACKS_FOOT,
    .oam = &gObjectEventBaseOam_%s,
    .subspriteTables = sOamTables_%s,
    .anims = %s,
    .images = sPicTable_%s,
    .affineAnims = gDummySpriteAffineAnimTable,
};

''' % (camel, tag, w * h // 2, w, h, 'TRUE' if len(em) == 1 else 'FALSE', size, size,
            'sAnimTable_Inanimate' if len(em) == 1 else 'sAnimTable_Standard', camel))
        pals.append('    {gObjectEventPal_%s, %s},\n' % (camel, tag))
        ptrs.append('    [%s] = &gObjectEventGraphicsInfo_%s,\n' % (const, camel))
        externs.append('extern const struct ObjectEventGraphicsInfo gObjectEventGraphicsInfo_%s;\n' % camel)
        consts.append('    %s,\n' % const)
        tags.append('#define %-40s 0x%04X\n' % (tag, PAL_TAG_START + len(tags)))
        done[obj] = const

    head = '// Generated by tools/pokemon_world/import_bw.py (bw/sprites.py) - edit that script, not this file.\n' \
           '// Pokémon Black\'s overworld characters (graphics/object_events/pics/unova).\n'
    d = 'src/data/object_events/'
    for fn, body, note in (('object_event_graphics_unova.h', gfx, 'object_event_graphics.h'),
                           ('object_event_pic_tables_unova.h', pics, 'object_event_pic_tables.h'),
                           ('object_event_graphics_info_unova.h', infos, 'object_event_graphics_info.h'),
                           ('object_event_palettes_unova.h', pals, 'sObjectEventSpritePalettes'),
                           ('object_event_graphics_info_pointers_unova.h', ptrs, 'gObjectEventGraphicsInfoPointers'),
                           ('object_event_graphics_info_pointers_unova_extern.h', externs, 'event_objects.h users')):
        with open(d + fn, 'w') as f:
            f.write(head + '// Included from %s.\n' % note + ''.join(body))
    # constants: the UNOVA block after Heart & Soul's
    p = 'include/constants/event_objects.h'
    s = open(p).read()
    s = re.sub(r'    // BEGIN UNOVA_OBJ_EVENT_GFX.*?    // END UNOVA_OBJ_EVENT_GFX\n', '', s, flags=re.S)
    s = s.replace('    // END HNS_OBJ_EVENT_GFX\n', '    // END HNS_OBJ_EVENT_GFX\n'
                  '    // BEGIN UNOVA_OBJ_EVENT_GFX: Pokémon World, Pokémon Black\'s overworld characters (tools/pokemon_world/bw/sprites.py)\n'
                  + ''.join(consts) + '    // END UNOVA_OBJ_EVENT_GFX\n', 1)
    s = re.sub(r'// BEGIN UNOVA_PAL_TAGS\n.*?// END UNOVA_PAL_TAGS\n', '', s, flags=re.S)
    s = s.replace('#define OBJ_EVENT_PAL_TAG_HNS_BUGSY', '// BEGIN UNOVA_PAL_TAGS\n' + ''.join(tags)
                  + '// END UNOVA_PAL_TAGS\n#define OBJ_EVENT_PAL_TAG_HNS_BUGSY', 1)
    open(p, 'w').write(s)
    return done
