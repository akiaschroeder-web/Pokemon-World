"""Pokémon World: Pokémon Black's wild encounter tables (a/1/2/6) -> src/data/wild_encounters.json.

A table file is one 232-byte block used all year, or four (spring, summer, autumn, winter).
A block: 7 rate bytes (grass, dark grass, shaking grass, surf, rippling surf, fishing, rippling
fishing), a pad byte, then 12 + 12 + 12 + 5 + 5 + 5 + 5 slots of 4 bytes: u16 species (bits 0-10 =
National Dex number, 11-15 = form), u8 min level, u8 max level. (Universal Pokémon Randomizer's
Gen5RomHandler; PokeFinder's BW tables.)

Each Unova map gets one table per season, in order; src/wild_encounter.c picks the season's
(GetCurrentMapWildMonHeaderId). For now: grass, surfing and fishing. Dark grass (double battles),
shaking grass and rippling water spots aren't used yet.
"""

import json
import re
import struct

BLOCK = 232
COUNTS = [12, 12, 12, 5, 5, 5, 5]
# forms BW's tables use (National Dex number -> species constant per form)
FORMS = {550: ['SPECIES_BASCULIN_RED_STRIPED', 'SPECIES_BASCULIN_BLUE_STRIPED'],
         585: ['SPECIES_DEERLING_SPRING', 'SPECIES_DEERLING_SUMMER', 'SPECIES_DEERLING_AUTUMN', 'SPECIES_DEERLING_WINTER'],
         586: ['SPECIES_SAWSBUCK_SPRING', 'SPECIES_SAWSBUCK_SUMMER', 'SPECIES_SAWSBUCK_AUTUMN', 'SPECIES_SAWSBUCK_WINTER']}


def species_names():
    """National Dex number -> SPECIES_ constant (base forms), from include/constants/species.h."""
    out = {}
    for name, num in re.findall(r'^\s*(SPECIES_\w+)\s*=\s*(\d+)\s*,', open('include/constants/species.h').read(), re.M):
        out.setdefault(int(num), name)
    return out


def parse(data):
    """[(rates[7], slots[7][n] of (dex, form, min, max))] per season block."""
    blocks = []
    for b in range(len(data) // BLOCK):
        d = data[b * BLOCK:(b + 1) * BLOCK]
        rates = list(d[:7])
        o, groups = 8, []
        for n in COUNTS:
            g = []
            for _ in range(n):
                sp, lo, hi = struct.unpack_from('<HBB', d, o)
                g.append((sp & 0x7FF, sp >> 11, lo, hi))
                o += 4
            groups.append(g)
        blocks.append((rates, groups))
    return blocks


def write(rom, maps, label_of):
    """maps: [(map const, zone)]; label_of(map const) -> base label."""
    names = species_names()

    def mon(dex, form, lo, hi):
        sp = FORMS.get(dex, [None])[form] if dex in FORMS and form < len(FORMS[dex]) else names.get(dex)
        return {'min_level': lo, 'max_level': max(lo, hi), 'species': sp or 'SPECIES_NONE'}

    path = 'src/data/wild_encounters.json'
    data = json.load(open(path, encoding='utf-8'))
    group = next(g for g in data['wild_encounter_groups'] if g['label'] == 'gWildMonHeaders')
    group['encounters'] = [e for e in group['encounters'] if not e['map'].startswith('MAP_UNOVA_')]
    files = rom.narc('a/1/2/6')
    count = 0
    for const, zone in maps:
        if zone.encounters is None or zone.encounters >= len(files):
            continue
        blocks = parse(files[zone.encounters])
        if not blocks:
            continue
        for season, (rates, groups) in enumerate(blocks):
            e = {'map': const, 'base_label': label_of(const) + ('_S%d' % season if len(blocks) > 1 else '')}
            if rates[0] and any(s[0] for s in groups[0]):
                e['land_mons'] = {'encounter_rate': min(100, rates[0] * 2), 'mons': [mon(*s) for s in groups[0]]}
            if rates[3] and any(s[0] for s in groups[3]):
                e['water_mons'] = {'encounter_rate': min(100, rates[3] * 2), 'mons': [mon(*s) for s in groups[3]]}
            if rates[5] and any(s[0] for s in groups[5]):
                f = [mon(*s) for s in groups[5]]
                # old rod: BW's first two slots, good rod: the first three, super rod: all five
                e['fishing_mons'] = {'encounter_rate': min(100, rates[5] * 2), 'mons': f[:2] + f[:3] + f}
            if len(e) > 2:
                group['encounters'].append(e)
                count += 1
            elif len(blocks) > 1:
                group['encounters'].append(e)        # keep the four seasons together even if one is empty
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write('\n')
    return count
