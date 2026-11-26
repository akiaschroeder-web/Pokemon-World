"""Pokémon World: Pokémon Black's trainers -> ours.

  trainer data a/0/9/2 (20 bytes each: party flags, class, battle type, count, items, AI, money)
  parties     a/0/9/3 (8-byte records: IV level, gender/ability, level, species, form; + item, + moves)
  names       a/0/0/2 file 190 (9-bit packed strings), classes file 191, dialogue file 189 with its
              index a/0/9/0 (u16 trainer, u16 message type) - 0 before the battle, 1 when beaten in
              battle, 2 afterwards; 3-8 the same for each side of a double battle
  pictures    a/0/7/2: 8 files per picture - LZ11 NCGR (80x80, drawn by the RECN cell at +2),
              NCLR palette at +7. Class -> picture is a 105-byte table in the ARM9 binary.

Writes:
  src/data/trainers_unova.party                 TRAINER_BW_* (trainerproc builds trainers_unova.h)
  include/constants/opponents_unova.h           their ids, from UNOVA_TRAINERS_START
  graphics/trainers/front_pics/unova/*.png      BW's battle pictures, 80x80 scaled to our 64x64
  src/data/graphics/trainers_unova_gfx.h/_table.h, src/data/trainer_classes_unova.h, and the
  UNOVA_TRAINER_PICS / UNOVA_TRAINER_CLASSES blocks of include/constants/trainers.h
  src/data/bw_trainer_text.h                    each trainer's in-battle defeat line (for BW's scripts)
Trainer NPCs (scripts 3000+) get our trainerbattle scripts in import_bw.py, using texts().
"""

import os
import re
import struct

import ndspy.codeCompression

import scripts

PIC_DIR = 'graphics/trainers/front_pics/unova'
CLASS_PIC_MAGIC = bytes(range(47)) + bytes([40, 47])     # the start of BW's class -> picture table

# BW class names -> ours where we have the same class
OUR_CLASSES = {
    'PKMN TRAINER': 'TRAINER_CLASS_PKMN_TRAINER_1', 'YOUNGSTER': 'TRAINER_CLASS_YOUNGSTER',
    'LASS': 'TRAINER_CLASS_LASS', 'SCHOOL KID': 'TRAINER_CLASS_SCHOOL_KID', 'TWINS': 'TRAINER_CLASS_TWINS',
    'PKMN BREEDER': 'TRAINER_CLASS_PKMN_BREEDER', 'LEADER': 'TRAINER_CLASS_LEADER',
    'PKMN RANGER': 'TRAINER_CLASS_PKMN_RANGER', 'FISHERMAN': 'TRAINER_CLASS_FISHERMAN',
    'ARTIST': 'TRAINER_CLASS_ARTIST', 'PSYCHIC': 'TRAINER_CLASS_PSYCHIC', 'RICH BOY': 'TRAINER_CLASS_RICH_BOY',
    'LADY': 'TRAINER_CLASS_LADY', 'SCIENTIST': 'TRAINER_CLASS_SCIENTIST', 'ACE TRAINER': 'TRAINER_CLASS_ACE_TRAINER',
    'BLACK BELT': 'TRAINER_CLASS_BLACK_BELT', 'POKEFAN': 'TRAINER_CLASS_POKEFAN', 'BATTLE GIRL': 'TRAINER_CLASS_BATTLE_GIRL',
    'PARASOL LADY': 'TRAINER_CLASS_PARASOL_LADY', 'VETERAN': 'TRAINER_CLASS_VETERAN', 'HIKER': 'TRAINER_CLASS_HIKER',
    'SOCIALITE': 'TRAINER_CLASS_SOCIALITE', 'GENTLEMAN': 'TRAINER_CLASS_GENTLEMAN', 'ELITE FOUR': 'TRAINER_CLASS_ELITE_FOUR',
    'POLICEMAN': 'TRAINER_CLASS_POLICEMAN', 'MAID': 'TRAINER_CLASS_MAID', 'CHAMPION': 'TRAINER_CLASS_CHAMPION',
    'CYCLIST': 'TRAINER_CLASS_CYCLIST', 'SWIMMER M': 'TRAINER_CLASS_SWIMMER_M', 'SWIMMER F': 'TRAINER_CLASS_SWIMMER_F',
}
# pictures of women and girls (for the encounter music and our trainer gender)
FEMALE_PICS = {1, 3, 5, 6, 9, 13, 15, 16, 18, 19, 21, 23, 25, 28, 34, 36, 38, 42, 46, 47, 48, 53, 54, 59, 61, 63,
               64, 68, 70, 74, 76, 77, 80, 84, 86, 90, 91, 94}


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]


def unpack9(codes):
    """BW's 9-bit packed strings (first unit 0xF100) -> plain code units."""
    if not codes or codes[0] != 0xF100:
        return codes
    out, cont, bit = [], 0, 0
    for x in codes[1:]:
        cont |= x << bit
        bit += 16
        while bit >= 9:
            bit -= 9
            ch = cont & 0x1FF
            cont >>= 9
            if ch == 0x1FF:
                return out + [0xFFFF]
            out.append(ch)
    return out + [0xFFFF]


def lz11(d):
    size = d[1] | d[2] << 8 | d[3] << 16
    p, out = 4, bytearray()
    while len(out) < size:
        flags = d[p]
        p += 1
        for b in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> b):
                out.append(d[p])
                p += 1
                continue
            ind = d[p] >> 4
            if ind == 0:
                n = (((d[p] & 15) << 4) | (d[p + 1] >> 4)) + 0x11
                disp = ((d[p + 1] & 15) << 8 | d[p + 2]) + 1
                p += 3
            elif ind == 1:
                n = (((d[p] & 15) << 12) | (d[p + 1] << 4) | (d[p + 2] >> 4)) + 0x111
                disp = ((d[p + 2] & 15) << 8 | d[p + 3]) + 1
                p += 4
            else:
                n = ind + 1
                disp = ((d[p] & 15) << 8 | d[p + 1]) + 1
                p += 2
            for _ in range(n):
                out.append(out[-disp])
    return bytes(out)


SHAPES = {(0, 0): (8, 8), (0, 1): (16, 16), (0, 2): (32, 32), (0, 3): (64, 64), (1, 0): (16, 8), (1, 1): (32, 8),
          (1, 2): (32, 16), (1, 3): (64, 32), (2, 0): (8, 16), (2, 1): (8, 32), (2, 2): (16, 32), (2, 3): (32, 64)}


def picture(files, k):
    """BW battle picture k: (80x80 palette indices, 16 RGB555 colours)."""
    g = lz11(files[8 * k])
    pd = g[16 + 0x20:]
    c = files[8 * k + 2]
    cb = 16 + 8
    ncell, btype = struct.unpack_from('<HH', c, cb)
    cell = cb + u32(c, cb + 4)
    noam, _, ooff = struct.unpack_from('<HHI', c, cell)
    oams = cell + ncell * (16 if btype else 8) + ooff
    idx = bytearray(6400)
    for j in range(noam):
        a0, a1, a2 = struct.unpack_from('<3H', c, oams + 6 * j)
        y = a0 & 0xFF
        y = y - 256 if y >= 128 else y
        x = a1 & 0x1FF
        x = x - 512 if x >= 256 else x
        w, h = SHAPES[(a0 >> 14, a1 >> 14)]
        t0 = (a2 & 0x3FF) * 2
        for ty in range(h // 8):
            for tx in range(w // 8):
                t = t0 + ty * (w // 8) + tx
                for yy in range(8):
                    for xx in range(8):
                        q = t * 32 + yy * 4 + xx // 2
                        if q >= len(pd):
                            continue
                        b = pd[q]
                        px, py = x + 40 + tx * 8 + xx, y + 40 + ty * 8 + yy
                        if 0 <= px < 80 and 0 <= py < 80:
                            idx[py * 80 + px] = (b >> 4) if xx & 1 else (b & 15)
    pl = files[8 * k + 7]
    pal = [u16(pl, 16 + 0x18 + 2 * i) for i in range(16)]
    return bytes(idx), pal


def scale_to_64(idx, pal):
    """80x80 -> 64x64: each target pixel takes the most common colour of the source pixels it covers
    (weighted by overlap), so outlines survive better than plain nearest-neighbour."""
    out = bytearray(64 * 64)
    f = 80 / 64
    for y in range(64):
        for x in range(64):
            votes = {}
            y0, y1, x0, x1 = y * f, (y + 1) * f, x * f, (x + 1) * f
            for sy in range(int(y0), min(80, int(y1 + 0.999))):
                wy = min(y1, sy + 1) - max(y0, sy)
                for sx in range(int(x0), min(80, int(x1 + 0.999))):
                    wx = min(x1, sx + 1) - max(x0, sx)
                    c = idx[sy * 80 + sx]
                    w = wx * wy * (1.35 if c and pal_dark(pal[c]) else 1.0)   # keep the dark outline
                    votes[c] = votes.get(c, 0) + w
            out[y * 64 + x] = max(votes, key=votes.get)
    return bytes(out)


def pal_dark(c):
    return ((c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31)) < 24


def class_pictures(rom):
    arm9 = bytes(rom.rom.arm9)
    try:
        arm9 = ndspy.codeCompression.decompress(arm9)
    except Exception:
        pass
    i = arm9.find(CLASS_PIC_MAGIC)
    if i < 0:
        raise SystemExit('BW trainer class -> picture table not found')
    return list(arm9[i:i + 105])


def plain(codes):
    s = ''
    for c in codes:
        if c == 0xFFFF:
            break
        if c < 0xD800 or 0xDFFF < c < 0xF000:
            s += chr(c)
    return s


# BW's trainer eye-contact themes, SEQ_BGM_EYE_01..13 and EYE_PLASMA (SDAT 1114..1127); a Unova
# trainer's encounterMusic holds the theme's index (src/battle_setup.c PlayTrainerEncounterMusic)
EYE_FIRST, EYE_LAST = 1114, 1127
MUSIC_ENUM = ['TRAINER_ENCOUNTER_MUSIC_MALE', 'TRAINER_ENCOUNTER_MUSIC_FEMALE', 'TRAINER_ENCOUNTER_MUSIC_GIRL',
              'TRAINER_ENCOUNTER_MUSIC_SUSPICIOUS', 'TRAINER_ENCOUNTER_MUSIC_INTENSE', 'TRAINER_ENCOUNTER_MUSIC_COOL',
              'TRAINER_ENCOUNTER_MUSIC_AQUA', 'TRAINER_ENCOUNTER_MUSIC_MAGMA', 'TRAINER_ENCOUNTER_MUSIC_SWIMMER',
              'TRAINER_ENCOUNTER_MUSIC_TWINS', 'TRAINER_ENCOUNTER_MUSIC_ELITE_FOUR', 'TRAINER_ENCOUNTER_MUSIC_HIKER',
              'TRAINER_ENCOUNTER_MUSIC_INTERVIEWER', 'TRAINER_ENCOUNTER_MUSIC_RICH']


def class_eye_music(rom):
    """BW class -> eye theme index (0-13): a table of (u16 class, u16 SDAT id) pairs in ARM9."""
    arm9 = bytes(rom.rom.arm9)
    try:
        arm9 = ndspy.codeCompression.decompress(arm9)
    except Exception:
        pass
    i = arm9.find(struct.pack('<4H', 2, 1114, 3, 1115))
    out = {}
    while i >= 0 and i + 4 <= len(arm9):
        c, m = struct.unpack_from('<2H', arm9, i)
        if not EYE_FIRST <= m <= EYE_LAST or c > 200:
            break
        out.setdefault(c, m - EYE_FIRST)
        i += 4
    return out


def ident(s):
    s = s.replace('é', 'e').replace('É', 'E').replace('♂', ' M').replace('♀', ' F')
    s = re.sub(r'[^A-Za-z0-9]+', '_', s).strip('_').upper()
    return s or 'X'


def class_name(codes):
    s = ''
    for c in codes:
        if c == 0xFFFF:
            break
        if c in (0x2486, 0x2487):          # BW's "PK" "MN" glyphs
            if c == 0x2486:
                s += 'PKMN'
            continue
        if c == 0x246D:
            s += ' M'
        elif c == 0x246E:
            s += ' F'
        elif c < 0xF000:
            s += chr(c)
    return s.strip()


def write(rom, decode_text, item_of, species_of):
    """Converts every trainer. item_of(bw item) -> ITEM_ constant or None; species_of(dex, form) -> SPECIES_."""
    sys_text = rom.narc('a/0/0/2')
    names = [plain(unpack9(n)) for n in decode_text(sys_text[190])]
    cls_names = [class_name(unpack9(c)) for c in decode_text(sys_text[191])]
    tdata, tparty = rom.narc('a/0/9/2'), rom.narc('a/0/9/3')
    pics_of_class = class_pictures(rom)
    eye = class_eye_music(rom)
    moves = {int(n): m for m, n in re.findall(r'^\s*(MOVE_\w+)\s*=\s*(\d+)\s*,', open('include/constants/moves.h').read(), re.M)}

    # classes
    our_class, new_classes = {}, {}
    money = {}
    for t in tdata[1:]:
        money.setdefault(t[1], []).append(t[0x11])
    for i, n in enumerate(cls_names):
        key = n.upper().replace('POKé', 'POKE').replace('É', 'E')
        if key in OUR_CLASSES:
            our_class[i] = OUR_CLASSES[key]
            continue
        base = re.sub(r' [MF]$', '', key)
        const = 'TRAINER_CLASS_%s_BW' % ident(base)
        our_class[i] = const
        if const not in new_classes:
            m = money.get(i, [5])
            new_classes[const] = (base.replace('POKE', 'POKé')[:12], max(1, round(sum(m) / len(m))))

    # pictures
    pic_files = rom.narc('a/0/7/2')
    os.makedirs(PIC_DIR, exist_ok=True)
    used_pics = sorted({pics_of_class[t[1]] if t[1] < len(pics_of_class) else 0 for t in tdata[1:]})
    pic_const = {}
    gfx = ['// Generated by tools/pokemon_world/bw/trainers.py: Pokémon Black\'s battle pictures.\n']
    tab = ['// Generated by tools/pokemon_world/bw/trainers.py. Included inside gTrainerPicInfo.\n']
    enum = []
    pic_name = {}
    for ci, p in enumerate(pics_of_class):
        pic_name.setdefault(p, ident(cls_names[ci]) if ci < len(cls_names) else 'P%d' % p)
    import sprites
    for p in used_pics:
        idx, pal = picture(pic_files, p)
        small = scale_to_64(idx, pal)
        name = '%d_%s' % (p, pic_name.get(p, 'p').lower())
        path = '%s/%s.png' % (PIC_DIR, name)
        sprites.png_indexed(path, 64, 64, [small], pal)
        const = 'TRAINER_PIC_BW_%d' % p
        pic_const[p] = const
        camel = 'Bw%d' % p
        enum.append('    %s,\n' % const)
        gfx.append('const u32 gTrainerFrontPic_%s[] = INCGFX_U32("%s", ".4bpp.smol");\n'
                   'const u16 gTrainerPalette_%s[] = INCGFX_U16("%s", ".gbapal");\n' % (camel, path, camel, path))
        tab.append('    [%s] =\n    {\n        .frontPic = TRAINER_FRONT_PIC(gTrainerFrontPic_%s, gTrainerPalette_%s),\n    },\n'
                   % (const, camel, camel))
    with open('src/data/graphics/trainers_unova_gfx.h', 'w') as f:
        f.write(''.join(gfx))
    with open('src/data/graphics/trainers_unova_table.h', 'w') as f:
        f.write(''.join(tab))
    splice('include/constants/trainers.h', 'UNOVA_TRAINER_PICS', ''.join(enum),
           '    TRAINER_PIC_COUNT,', "Pokémon Black's battle pictures")
    splice('include/constants/trainers.h', 'UNOVA_TRAINER_CLASSES', ''.join('    %s,\n' % c for c in new_classes),
           '\n    TRAINER_CLASS_COUNT,', "Pokémon Black's classes")
    with open('src/data/trainer_classes_unova.h', 'w') as f:
        f.write('// Generated by tools/pokemon_world/bw/trainers.py. Pokémon Black\'s classes (included inside gTrainerClasses).\n')
        for c, (n, m) in new_classes.items():
            f.write('    [%s] = { _("%s"), %d },\n' % (c, n, m))

    # parties
    consts = {}
    out = ['/* Generated by tools/pokemon_world/bw/trainers.py - edit that script, not this file.\n'
           '   Pokémon Black\'s trainers: parties, items and AI as in Black; classes are ours where we have\n'
           '   the same one; pictures are Black\'s (TRAINER_PIC_BW_*). */\n\n']
    for tid in range(1, len(tdata)):
        t = tdata[tid]
        flags, cls, btype, count = t[0], t[1], t[2], t[3]
        items = [item_of(u16(t, 4 + 2 * k)) for k in range(4)]
        items = [i for i in items if i]
        ai = u32(t, 0xC)
        name = names[tid] if tid < len(names) else ''
        const = 'TRAINER_BW_%s_%d' % (ident(name) if name else 'X', tid)
        consts[tid] = const
        pic = pics_of_class[cls] if cls < len(pics_of_class) else 0
        female = pic in FEMALE_PICS
        out.append('=== %s ===\nName: %s\nClass: %s\nPic: %s\nGender: %s\nMusic: %s\nDouble Battle: %s\nAI: %s\n'
                   % (const, safe_name(name), our_class.get(cls, 'TRAINER_CLASS_PKMN_TRAINER_1'), pic_const.get(pic, 'TRAINER_PIC_BW_0'),
                      'Female' if female else 'Male', MUSIC_ENUM[eye.get(cls, 0)],
                      'Yes' if btype in (1, 2) and count >= 2 else 'No',
                      'Check Bad Move / Try To Faint / Check Viability' if ai & 6 else 'Check Bad Move'))
        if items:
            out.append('Items: %s\n' % ' / '.join(items))
        out.append('\n')
        size = 8 + (2 if flags & 2 else 0) + (8 if flags & 1 else 0)
        p = tparty[tid]
        mons = 0
        for k in range(count):
            r = p[k * size:(k + 1) * size]
            if len(r) < 8:
                break
            iv = r[0] * 31 // 255
            gen = r[1] & 3
            level = u16(r, 2)
            sp = species_of(u16(r, 4), u16(r, 6))
            if not sp:
                continue
            line = sp + (' (M)' if gen == 1 else ' (F)' if gen == 2 else '')
            o = 8
            if flags & 2:
                it = item_of(u16(r, o))
                if it:
                    line += ' @ ' + it
                o += 2
            out.append(line + '\nLevel: %d\nIVs: %d HP / %d Atk / %d Def / %d SpA / %d SpD / %d Spe\n' % ((max(1, level),) + (iv,) * 6))
            if flags & 1:
                ms = [moves.get(u16(r, o + 2 * m)) for m in range(4)]
                for m in ms:
                    if m and m != 'MOVE_NONE':
                        out.append('- %s\n' % m)
            out.append('\n')
            mons += 1
        if not mons:
            out.append('SPECIES_PATRAT\nLevel: 2\n\n')
    with open('src/data/trainers_unova.party', 'w', encoding='utf-8') as f:
        f.write(''.join(out))
    with open('include/constants/opponents_unova.h', 'w') as f:
        f.write('// Generated by tools/pokemon_world/bw/trainers.py: Pokémon Black\'s trainers, from UNOVA_TRAINERS_START\n'
                '// (BW trainer n is UNOVA_TRAINERS_START + n).\n#ifndef GUARD_CONSTANTS_OPPONENTS_UNOVA_H\n'
                '#define GUARD_CONSTANTS_OPPONENTS_UNOVA_H\n\n')
        for tid, c in consts.items():
            f.write('#define %s (UNOVA_TRAINERS_START + %d)\n' % (c, tid))
        f.write('\n#define UNOVA_TRAINERS_COUNT %d\n\n#endif // GUARD_CONSTANTS_OPPONENTS_UNOVA_H\n' % len(tdata))
    print('trainers: %d (%d new classes, %d pictures, %d classes with eye music)' % (len(consts), len(new_classes), len(used_pics), len(eye)))
    return consts, btypes(tdata)


def btypes(tdata):
    return {tid: tdata[tid][2] if tdata[tid][3] >= 2 else 0 for tid in range(1, len(tdata))}


def safe_name(n):
    n = n.replace('é', 'é')
    n = ''.join(ch if ch in scripts.ALLOWED else '?' for ch in n)
    return n[:10] or '???'


def texts(rom, decode_text):
    """{trainer id: {message type: decoded text}} from a/0/0/2 file 189 and its index a/0/9/0."""
    idx = rom.narc('a/0/9/0')[0]
    lines = decode_text(rom.narc('a/0/0/2')[189])
    out = {}
    for k in range(len(idx) // 4):
        tid, typ = struct.unpack_from('<2H', idx, 4 * k)
        if k < len(lines):
            out.setdefault(tid, {})[typ] = unpack9(lines[k])
    return out


def splice(path, tag, text, before, what):
    s = open(path, encoding='utf-8').read()
    begin = '    // BEGIN %s: Pokémon World, %s (generated by tools/pokemon_world/bw/trainers.py)\n' % (tag, what)
    end = '    // END %s\n' % tag
    if begin.split(':')[0] in s:
        i = s.index(begin.split(':')[0])
        j = s.index(end, i) + len(end)
        s = s[:i] + begin + text + end + s[j:]
    else:
        i = s.index(before)
        if before.startswith('\n'):
            i += 1
        s = s[:i] + begin + text + end + s[i:]
    open(path, 'w', encoding='utf-8').write(s)


def write_defeat_texts(texts):
    """data/unova_trainer_text.s: each trainer\x27s line when beaten in battle, for battles BW\x27s own
    scripts start (gym leaders, rivals...): gBwTrainerDefeatTexts[BW trainer id]."""
    n = max(texts) + 1 if texts else 1
    out = ["@ Generated by tools/pokemon_world/bw/trainers.py: Pokémon Black\x27s trainers\x27 defeat lines.\n",
           "\t.section .rodata\n"]
    for tid in sorted(texts):
        t = texts[tid].get(1) or texts[tid].get(4)
        if t:
            out.append("BwTrainerDefeat_%d:\n\t.string \"%s$\"\n" % (tid, scripts.to_script_string(t)))
    out.append("\t.align 2\n\t.global gBwTrainerDefeatTexts\ngBwTrainerDefeatTexts:\n")
    for tid in range(n):
        t = texts.get(tid, {})
        out.append("\t.4byte %s\n" % ("BwTrainerDefeat_%d" % tid if (t.get(1) or t.get(4)) else "0"))
    out.append("\t.global gBwTrainerDefeatTextCount\ngBwTrainerDefeatTextCount:\n\t.4byte %d\n" % n)
    with open("data/unova_trainer_text.s", "w", encoding="utf-8") as f:
        f.write("".join(out))
