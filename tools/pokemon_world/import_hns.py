#!/usr/bin/env python3
"""Pokémon World: bring Heart & Soul's Johto and Gold/Silver-era Kanto into the build.

Heart & Soul (HnS) is a pokeemerald-based project (used with permission, see
PROJECT_CREDITS.md). This script copies and converts its Johto and later-Kanto maps:
every map group except gMapGroup_Emerald1-5 (Emerald leftovers we already have).

Naming: everything that comes from Heart & Soul gets the suffix _Hns (_HNS in
constants), the same way FireRed's Kanto got _Frlg:
    data/maps/NewBarkTown_Hns, MAP_NEW_BARK_TOWN_HNS, LAYOUT_NEW_BARK_TOWN_HNS,
    gMapGroup_TownsAndRoutes_Hns, gTileset_Johto_General_Hns,
    data/tilesets/primary/johto_general_hns, HEAL_LOCATION_NEW_BARK_TOWN_HNS,
    script labels NewBarkTown_Hns_EventScript_... (labels that don't start with
    a map name get _Hns at the end), METATILE_Hns_<HnS name>.
  So later Kanto (PalletTown_Hns) can never be mistaken for FireRed's
  (PalletTown_Frlg) or clash with Hoenn or Sinnoh maps.
  Flags, vars and trainers keep Heart & Soul's names, unless the name already
  means something else in our game: then it gets HNS_ after the prefix
  (FLAG_HNS_SYS_GAME_CLEAR, TRAINER_HNS_ALAN, ...). Names that mean the same
  thing in both (FLAG_SYS_B_DASH, FLAG_TEMP_*, VAR_RESULT, ...) stay ours, and
  Heart & Soul's badges become FLAG_JOHTO_BADGE0x_GET / FLAG_KANTO_BADGE0x_GET.

What it writes (all generated: edit this script, not the output):
  data/maps/*_Hns/                          maps (map.json + converted scripts.inc)
  data/layouts/*_Hns/                       layouts (map.bin, border.bin)
  data/tilesets/{primary,secondary}/*_hns/  tilesets (attributes renumbered to our
                                            metatile behaviors)
  graphics/door_anims/hns/                  door animations
  data/maps/map_groups.json                 *_Hns groups (re-written in place)
  data/layouts/layouts.json                 *_HNS layouts (layout_version "hns")
  src/data/region_map/region_map_sections.json   Johto + later-Kanto map sections
  src/data/heal_locations.json              HEAL_LOCATION_*_HNS
  include/constants/flags_hns.h             Johto/later-Kanto flags (JOHTO_FLAGS_START)
  include/constants/vars_hns.h              ... vars (JOHTO_VARS_START)
  include/constants/opponents_hns.h         ... trainer IDs (JOHTO_TRAINERS_START)
  include/constants/metatile_labels_hns.h   METATILE_Hns_*
  include/constants/hns_compat.h            temporary music/item stand-ins, Heart & Soul sprites = ours
  include/constants/event_objects.h         OBJ_EVENT_GFX_HNS_* and OBJ_EVENT_PAL_TAG_HNS_* (between its
                                            "// BEGIN HNS_OBJ_EVENT_..." / "// END" lines), with
  src/data/object_events/*_hns.h            Heart & Soul's overworld sprites we lacked (hns_sprites.py)
  graphics/object_events/{pics,palettes}/hns/   ... their pictures and palettes
  include/constants/region_map_sections_hns.h    JOHTO_/KANTO_HNS_ map section ranges
  src/data/trainers_hns.party               trainers (parties, AI flags, items; classes and
                                            pics ours where the same, else Heart & Soul's)
  include/constants/trainers.h              TRAINER_CLASS_*_HNS and TRAINER_PIC_*_HNS, between
                                            its "// BEGIN HNS_TRAINER_..." / "// END" lines
  src/data/trainer_classes_hns.h            their names/money (inside gTrainerClasses)
  src/data/graphics/trainers_hns_{gfx,table}.h  their front pics
  graphics/trainers/front_pics/hns/         Heart & Soul's trainer front pics we lacked
  include/constants/trainer_music_hns.h     Heart & Soul's encounter music -> our styles
                                            (table: ENCOUNTER_MUSIC below; HGSS music is stage 4)
  src/data/wild_encounters.json             gWildMonHeaders entries for *_HNS maps (day
                                            table gX_Hns, night table gX_Hns_Night)
  src/data/tilesets/{graphics,metatiles,headers}_hns.h
  src/data/field_door_hns_gfx.h, field_door_hns_table.h
  data/scripts/hns_maps.inc                 includes every *_Hns scripts.inc
  data/scripts/hns_missing.inc              stub labels for scripts HnS shares from
                                            files we don't import (TODO list)
  tools/pokemon_world/hns_import_state.json what was added to the shared JSON files,
                                            so a re-run can take it out first
Hand-written companions: asm/macros/hns_compat.inc (HnS script commands),
src/hns_specials.c, src/hns_bug_contest.c, src/hns_sliding_puzzle.c (the specials/natives they call,
Hns_*), src/data/tilesets/tileset_anims_hns.h.

Run from the repo root (reads Heart & Soul read-only; needs a host gcc to read our
own constant values):
    python3 tools/pokemon_world/import_hns.py [--hns ~/ref/pokemonHnS]
Re-running replaces everything it wrote before. A report of what was stubbed or
mapped temporarily is printed at the end.
"""
import argparse, glob, hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile, zlib
from collections import OrderedDict, defaultdict

GEN_C = "// Generated by tools/pokemon_world/import_hns.py - edit that script, not this file.\n"
GEN_ASM = "@ Generated by tools/pokemon_world/import_hns.py - edit that script, not this file.\n"
GEN_PARTY = "/* Generated by tools/pokemon_world/import_hns.py - edit that script, not this file. */\n"
STATE = "tools/pokemon_world/hns_import_state.json"

REPORT = defaultdict(set)


def note(kind, what):
    REPORT[kind].add(what)


def rd(p):
    return open(p, encoding="utf-8", errors="replace").read()


def wr(p, s):
    os.makedirs(os.path.dirname(p) or ".", exist_ok=True)
    with open(p, "w", encoding="utf-8", newline="\n") as f:
        f.write(s)


def wjson(p, obj):
    wr(p, json.dumps(obj, indent=2, ensure_ascii=False) + "\n")


def camel_to_const(s):
    s = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", s)
    return s.upper()


# ---------------------------------------------------------------------------------
# constant evaluation (Heart & Soul headers are only parsed, never compiled or run)

def parse_defines(paths):
    defs = OrderedDict()
    for p in paths:
        if not os.path.exists(p):
            continue
        txt = re.sub(r"/\*.*?\*/", "", rd(p), flags=re.S)
        for m in re.finditer(r"(?m)^\s*#define\s+(\w+)\s+([^\n]*?)\s*(?://.*)?$", txt):
            defs[m.group(1)] = m.group(2).strip()
    return defs


def evaluator(defs):
    cache = {}

    def ev(name, depth=0):
        if name in cache:
            return cache[name]
        if depth > 50 or name not in defs:
            return None
        expr = defs[name]

        def sub(m):
            t = m.group(0)
            if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", t):
                return str(int(t, 0))
            v = ev(t, depth + 1)
            if v is None:
                raise KeyError(t)
            return str(v)
        try:
            e = re.sub(r"\b0[xX][0-9a-fA-F]+\b|\b[A-Za-z_]\w*\b|\b\d+\b", sub, expr)
            if not re.fullmatch(r"[\d\s+\-*/()<>|&~]*", e):
                return None
            v = int(eval(e.replace("/", "//"), {"__builtins__": {}}))
        except (KeyError, SyntaxError, ZeroDivisionError, TypeError):
            return None
        cache[name] = v
        return v
    return ev


def splice(path, tag, text):
    """Replace what's between the lines "// BEGIN <tag>" and "// END <tag>" in one of our files
    (for lists that can't come from an #include, e.g. enums the assembler's preprocessor reads)."""
    s = rd(path)
    m = re.search(r"(?m)^[ \t]*// BEGIN " + re.escape(tag) + r"[^\n]*\n(.*?)^[ \t]*// END " + re.escape(tag), s, re.S)
    if not m:
        sys.exit(f"{path}: no '// BEGIN {tag}' ... '// END {tag}' block")
    wr(path, s[:m.start(1)] + text + s[m.end(1):])


def png_key(path):
    """A hash of what an indexed PNG looks like (15-bit colours, index 0 = transparent), so the same
    picture saved differently still matches. None if it isn't a PNG we can read."""
    try:
        d = open(path, "rb").read()
        if d[:8] != b"\x89PNG\r\n\x1a\n":
            return None
        i, idat, pal = 8, b"", None
        while i < len(d):
            n, t = struct.unpack(">I4s", d[i:i + 8])
            c = d[i + 8:i + 8 + n]
            i += 12 + n
            if t == b"IHDR":
                w, h, bd, ct = struct.unpack(">IIBB", c[:10])
            elif t == b"PLTE":
                pal = [tuple(c[j:j + 3]) for j in range(0, len(c), 3)]
            elif t == b"IDAT":
                idat += c
        if ct != 3 or bd not in (4, 8) or not pal:
            return None
        raw = zlib.decompress(idat)
        stride = (w * bd + 7) // 8
        prev, k, rows = bytearray(stride), 0, []
        for _ in range(h):
            f, line = raw[k], bytearray(raw[k + 1:k + 1 + stride])
            k += 1 + stride
            for x in range(stride):
                a = line[x - 1] if x else 0
                b = prev[x]
                cc = prev[x - 1] if x else 0
                if f == 1:
                    line[x] = (line[x] + a) & 255
                elif f == 2:
                    line[x] = (line[x] + b) & 255
                elif f == 3:
                    line[x] = (line[x] + (a + b) // 2) & 255
                elif f == 4:
                    p = a + b - cc
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - cc)
                    line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else cc)) & 255
            prev = line
            idx = list(line[:w]) if bd == 8 else [(line[x // 2] >> (4 * (1 - x % 2))) & 15 for x in range(w)]
            rows.append(tuple(-1 if v == 0 else tuple(ch >> 3 for ch in pal[v]) for v in idx))
        return hashlib.sha1(repr((w, h, rows)).encode()).hexdigest()
    except (OSError, zlib.error, struct.error, IndexError, UnboundLocalError):
        return None


def our_values(names, headers, extra_cflags=()):
    """Values of our own constants, read with the host compiler (our code, not HnS's)."""
    src = "#include <limits.h>\n"
    for h in headers:
        src += f'#include "{h}"\n'
    src += "#include <stdio.h>\nint main(void){\n"
    for n in names:
        src += f'printf("{n} %ld\\n",(long)({n}));\n'
    src += "return 0;}\n"
    d = tempfile.mkdtemp()
    c = os.path.join(d, "v.c")
    open(c, "w").write(src)
    exe = os.path.join(d, "v")
    r = subprocess.run(["gcc", "-w", "-std=gnu17", "-DMODERN=1", "-DTESTING=0", "-DPORTABLE", *extra_cflags,
                        "-iquote", "include", "-iquote", "gflib", "-o", exe, c], capture_output=True, text=True)
    if r.returncode:
        sys.exit("gcc failed:\n" + r.stderr[:3000])
    out = subprocess.run([exe], capture_output=True, text=True).stdout
    return {l.split()[0]: int(l.split()[1]) for l in out.splitlines()}


# ---------------------------------------------------------------------------------

class Importer:
    def __init__(self, hns):
        self.H = hns
        self.state = json.load(open(STATE)) if os.path.exists(STATE) else {}
        self.new_state = {}
        self.hns_gfx_used = set()   # Heart & Soul object sprites we don't have (hns_sprites.py)

    # --- maps -------------------------------------------------------------------
    def load_maps(self):
        g = json.load(open(f"{self.H}/data/maps/map_groups.json"))
        self.groups = [grp for grp in g["group_order"] if not grp.startswith("gMapGroup_Emerald")]
        self.group_maps = {grp: g[grp] for grp in self.groups}
        self.maps = [m for grp in self.groups for m in g[grp]]
        self.mapjson = {m: json.load(open(f"{self.H}/data/maps/{m}/map.json")) for m in self.maps}
        self.map_const = {}   # HnS MAP_X -> MAP_X_HNS
        for m in self.maps:
            self.map_const[self.mapjson[m]["id"]] = self.mapjson[m]["id"] + "_HNS"
        self.map_names = sorted(self.maps, key=len, reverse=True)
        # every map we already have, by constant
        ours = json.load(open("data/maps/map_groups.json"))
        self.our_map_consts = set()
        for grp in ours["group_order"]:
            if grp.endswith("_Hns"):
                continue
            for m in ours[grp]:
                p = f"data/maps/{m}/map.json"
                if os.path.exists(p):
                    self.our_map_consts.add(json.load(open(p))["id"])

    # --- layouts and tilesets ----------------------------------------------------
    def load_tilesets(self):
        H = self.H
        hdr = rd(f"{H}/src/data/tilesets/headers.h")
        gfx = rd(f"{H}/src/data/tilesets/graphics.h") + rd(f"{H}/src/graphics.c")
        mt = rd(f"{H}/src/data/tilesets/metatiles.h")
        self.ts = {}
        for m in re.finditer(r"const struct Tileset (gTileset_\w+)\s*=\s*\{(.*?)\};", hdr, re.S):
            body = m.group(2)
            f = dict(re.findall(r"\.(\w+)\s*=\s*([^,\n]+)", body))
            self.ts[m.group(1)] = f
        self.ts_tiles = dict(re.findall(r"const u32 (gTilesetTiles_\w+)\[\]\s*=\s*INCBIN_U32\(\"([^\"]+)\"\)", gfx))
        self.ts_pals = {}
        for m in re.finditer(r"const u16 (?:ALIGNED\(4\) )?(gTilesetPalettes_\w+)\[\]\[16\]\s*=\s*\{(.*?)\};", gfx, re.S):
            self.ts_pals[m.group(1)] = re.findall(r"\"([^\"]+)\"", m.group(2))
        self.ts_mt = dict(re.findall(r"const u16 (gMetatiles_\w+)\[\]\s*=\s*INCBIN_U16\(\"([^\"]+)\"\)", mt))
        self.ts_attr = dict(re.findall(r"const u16 (gMetatileAttributes_\w+)\[\]\s*=\s*INCBIN_U16\(\"([^\"]+)\"\)", mt))
        lays = json.load(open(f"{H}/data/layouts/layouts.json"))["layouts"]
        self.layouts = {l["id"]: l for l in lays if l}

    def behavior_map(self):
        hns = parse_defines([f"{self.H}/include/constants/metatile_behaviors.h"])
        hv = {n: int(v, 0) for n, v in hns.items() if n.startswith("MB_") and re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", v)}
        names = [n for n in hv]
        ourtxt = rd("include/constants/metatile_behaviors.h")
        our_names = set(re.findall(r"\b(MB_\w+)\b", ourtxt))
        common = [n for n in names if n in our_names]
        ov = our_values(common, ["constants/metatile_behaviors.h"])
        self.mb = {}
        for n, v in hv.items():
            if n in ov:
                self.mb.setdefault(v, ov[n])
            else:
                note("metatile behaviors with no counterpart (became MB_NORMAL)", n)
                self.mb.setdefault(v, 0)

    def remap_attrs(self, data):
        out = bytearray(data)
        for i in range(0, len(data) - 1, 2):
            a = data[i] | data[i + 1] << 8
            b = self.mb.get(a & 0xFF, 0)
            if b > 0xFF:
                note("metatile behaviors above 0xFF (dropped)", hex(b))
                b = 0
            a = (a & 0xFF00) | b
            out[i], out[i + 1] = a & 0xFF, a >> 8
        return bytes(out)

    def our_tileset_index(self):
        """Hash of every tileset directory we already have, to reuse identical ones."""
        idx = {}
        hdr = rd("src/data/tilesets/headers.h")
        gfx = rd("src/data/tilesets/graphics.h")
        tiles = dict(re.findall(r"const u32 (gTilesetTiles_\w+)\[\]\s*=\s*INCGFX_U32\(\"([^\"]+)\"", gfx))
        for m in re.finditer(r"const struct Tileset (gTileset_\w+)\s*=\s*\{(.*?)\};", hdr, re.S):
            f = dict(re.findall(r"\.(\w+)\s*=\s*([^,\n]+)", m.group(2)))
            t = tiles.get(f.get("tiles", "").strip())
            if not t:
                continue
            d = os.path.dirname(t)
            h = self.dir_hash(d, None)
            if h:
                idx[h] = m.group(1)
        return idx

    def dir_hash(self, d, attrs):
        try:
            h = hashlib.sha1()
            h.update(open(f"{d}/tiles.png", "rb").read())
            h.update(open(f"{d}/metatiles.bin", "rb").read())
            h.update(attrs if attrs is not None else open(f"{d}/metatile_attributes.bin", "rb").read())
            for i in range(16):
                p = f"{d}/palettes/{i:02}.pal"
                if os.path.exists(p):
                    h.update(open(p, "rb").read().replace(b"\r\n", b"\n"))
            return h.hexdigest()
        except OSError:
            return None

    def import_tilesets(self):
        used = []
        for m in self.maps:
            L = self.layouts[self.mapjson[m]["layout"]]
            for k in ("primary_tileset", "secondary_tileset"):
                if L[k] not in used:
                    used.append(L[k])
        ours = self.our_tileset_index()
        self.ts_new = {}       # HnS tileset symbol -> our symbol
        self.ts_out = []       # (symbol, info) to generate
        for old in self.state.get("tileset_dirs", []):
            shutil.rmtree(old, ignore_errors=True)
        dirs = []
        for sym in used:
            f = self.ts[sym]
            tiles_sym = f["tiles"].strip()
            src_dir = os.path.dirname(self.ts_tiles[tiles_sym])
            sec = f.get("isSecondary", "FALSE").strip() == "TRUE"
            attrs = self.remap_attrs(open(f"{self.H}/{self.ts_attr[f['metatileAttributes'].strip()]}", "rb").read())
            h = self.dir_hash(f"{self.H}/{src_dir}", attrs)
            if h in ours:
                self.ts_new[sym] = ours[h]
                note("tilesets reused (identical to ours)", f"{sym} -> {ours[h]}")
                continue
            base = os.path.basename(src_dir)
            kind = "secondary" if sec else "primary"
            dst = f"data/tilesets/{kind}/{base}_hns"
            dirs.append(dst)
            shutil.rmtree(dst, ignore_errors=True)
            os.makedirs(f"{dst}/palettes")
            shutil.copy(f"{self.H}/{src_dir}/tiles.png", f"{dst}/tiles.png")
            shutil.copy(f"{self.H}/{self.ts_mt[f['metatiles'].strip()]}", f"{dst}/metatiles.bin")
            open(f"{dst}/metatile_attributes.bin", "wb").write(attrs)
            for i in range(16):
                p = f"{self.H}/{src_dir}/palettes/{i:02}.pal"
                if os.path.exists(p):
                    shutil.copy(p, f"{dst}/palettes/{i:02}.pal")
            if os.path.isdir(f"{self.H}/{src_dir}/anim"):
                shutil.copytree(f"{self.H}/{src_dir}/anim", f"{dst}/anim")
            new = sym + "_Hns"
            self.ts_new[sym] = new
            cb = f.get("callback", "NULL").strip()
            if cb not in ANIM_PORTED and cb != "NULL":
                note("tileset animations not ported yet (callback NULL)", f"{sym}: {cb}")
            self.ts_out.append((new, dst, sec, ANIM_PORTED.get(cb, "NULL"),
                                f.get("isCompressed", "TRUE").strip()))
        self.new_state["tileset_dirs"] = dirs
        g = [GEN_C, "// Heart & Soul tilesets (Johto and later Kanto). Included from graphics.h.\n"]
        mtt = [GEN_C, "// Heart & Soul tilesets. Included from metatiles.h.\n"]
        hd = [GEN_C, "// Heart & Soul tilesets (640-tile primaries, see MapLayout.isHns). Included from headers.h.\n"]
        for new, dst, sec, cb, comp in self.ts_out:
            n = new[len("gTileset_"):]
            g.append(f'const u32 gTilesetTiles_{n}[] = INCGFX_U32("{dst}/tiles.png", ".4bpp.fastSmol");\n\n'
                     f"const u16 gTilesetPalettes_{n}[][16] =\n{{\n")
            for i in range(16):
                p = f"{dst}/palettes/{i:02}.pal"
                if os.path.exists(p):
                    g.append(f'    INCGFX_U16("{p}", ".gbapal"),\n')
                else:
                    g.append("    {},\n")
            g.append("};\n\n")
            mtt.append(f'const u16 gMetatiles_{n}[] = INCBIN_U16("{dst}/metatiles.bin");\n'
                       f'const u16 gMetatileAttributes_{n}[] = INCBIN_U16("{dst}/metatile_attributes.bin");\n')
            hd.append(f"const struct Tileset {new} =\n{{\n    .isCompressed = {comp},\n"
                      f"    .isSecondary = {'TRUE' if sec else 'FALSE'},\n"
                      f"    .tiles = gTilesetTiles_{n},\n    .palettes = gTilesetPalettes_{n},\n"
                      f"    .metatiles = gMetatiles_{n},\n    .metatileAttributes = gMetatileAttributes_{n},\n"
                      f"    .callback = {cb},\n}};\n\n")
        wr("src/data/tilesets/graphics_hns.h", "".join(g))
        wr("src/data/tilesets/metatiles_hns.h", "".join(mtt))
        wr("src/data/tilesets/headers_hns.h", "".join(hd))
        decl = [GEN_C, "#ifndef GUARD_TILESETS_HNS_H\n#define GUARD_TILESETS_HNS_H\n\n"]
        for new, *_ in self.ts_out:
            decl.append(f"extern const struct Tileset {new};\n")
        decl.append("\n#endif\n")
        wr("include/tilesets_hns.h", "".join(decl))

    def import_layouts(self):
        for old in self.state.get("layout_dirs", []):
            shutil.rmtree(old, ignore_errors=True)
        lj = json.load(open("data/layouts/layouts.json"))
        lj["layouts"] = [l for l in lj["layouts"] if not (l and l.get("layout_version") == "hns")]
        self.layout_const = {}
        dirs = []
        for m in self.maps:
            L = self.layouts[self.mapjson[m]["layout"]]
            if L["id"] in self.layout_const:
                continue
            d = os.path.dirname(L["blockdata_filepath"])
            base = os.path.basename(d) + "_Hns"
            dst = f"data/layouts/{base}"
            dirs.append(dst)
            shutil.rmtree(dst, ignore_errors=True)
            os.makedirs(dst)
            shutil.copy(f"{self.H}/{L['blockdata_filepath']}", f"{dst}/map.bin")
            shutil.copy(f"{self.H}/{L['border_filepath']}", f"{dst}/border.bin")
            new_id = L["id"] + "_HNS"
            name = L["name"]
            name = name[:-len("_Layout")] + "_Hns_Layout" if name.endswith("_Layout") else name + "_Hns"
            self.layout_const[L["id"]] = new_id
            lj["layouts"].append(OrderedDict([
                ("id", new_id), ("name", name), ("width", L["width"]), ("height", L["height"]),
                ("primary_tileset", self.ts_new[L["primary_tileset"]]),
                ("secondary_tileset", self.ts_new[L["secondary_tileset"]]),
                ("border_filepath", f"{dst}/border.bin"), ("blockdata_filepath", f"{dst}/map.bin"),
                ("layout_version", "hns")]))
        self.new_state["layout_dirs"] = dirs
        wjson("data/layouts/layouts.json", lj)

    # --- flags, vars, trainers ---------------------------------------------------
    def collect_used(self):
        txt = "".join(rd(f"{self.H}/data/maps/{m}/scripts.inc") for m in self.maps)
        js = "".join(json.dumps(self.mapjson[m]) for m in self.maps)
        self.used_flags = sorted(set(re.findall(r"\bFLAG_\w+", txt + js)) | set(ENGINE_FLAGS))
        self.used_vars = sorted(set(re.findall(r"\bVAR_\w+", txt + js)))
        self.used_trainers = sorted(set(re.findall(r"\bTRAINER_\w+", txt)) -
                                    {"TRAINER_TYPE_NONE", "TRAINER_TYPE_NORMAL", "TRAINER_TYPE_SEE_ALL_DIRECTIONS",
                                     "TRAINER_TYPE_BURIED", "TRAINER_NONE"})

    def gen_flags_vars(self):
        H = self.H
        hdefs = parse_defines([f"{H}/include/constants/flags.h", f"{H}/include/constants/vars.h",
                               f"{H}/include/constants/opponents.h", f"{H}/include/constants/global.h"])
        hev = evaluator(hdefs)
        ourtxt_f = rd("include/constants/flags.h") + rd("include/constants/flags_kanto.h")
        ourtxt_v = rd("include/constants/vars.h") + rd("include/constants/vars_kanto.h")
        our_flags = set(re.findall(r"#define\s+(FLAG_\w+)", ourtxt_f))
        our_vars = set(re.findall(r"#define\s+(VAR_\w+)", ourtxt_v))
        self.rename = {}
        # ---- flags
        out = [GEN_C, "#ifndef GUARD_CONSTANTS_FLAGS_HNS_H\n#define GUARD_CONSTANTS_FLAGS_HNS_H\n\n",
               "// Heart & Soul's Johto and later-Kanto flags, in Johto's range (SaveBlock3).\n",
               "// Names are Heart & Soul's; FLAG_HNS_* ones already meant something else here.\n\n"]
        slot_of, slots, shared = {}, 0, []
        unknown = []
        for n in self.used_flags:
            mb = re.fullmatch(r"FLAG_BADGE(\d\d)_GET", n)
            if mb:
                k = int(mb.group(1))
                self.rename[n] = f"FLAG_JOHTO_BADGE{k:02}_GET" if k <= 8 else f"FLAG_KANTO_BADGE{k - 8:02}_GET"
                continue
            v = hev(n)
            if n in our_flags and (n.startswith("FLAG_TEMP_") or n in SHARED_FLAGS or n.startswith("FLAG_DAILY_")
                                   or (v is not None and v >= 0x4000)):
                shared.append(n)
                continue
            if v is None:
                unknown.append(n)
                continue
            if v == 0:
                self.rename[n] = "0"
                continue
            new = n if n not in our_flags else "FLAG_HNS_" + n[len("FLAG_"):]
            if new != n:
                self.rename[n] = new
            if v >= 0x4000:
                note("Heart & Soul special flags given normal Johto flags", n)
            if v not in slot_of:
                slot_of[v] = slots
                slots += 1
            out.append(f"#define {new:<60} (JOHTO_FLAGS_START+0x{slot_of[v]:03X}) // HnS 0x{v:04X}\n")
        for n in unknown:
            new = n if n not in our_flags else "FLAG_HNS_" + n[len("FLAG_"):]
            self.rename[n] = new
            out.append(f"#define {new:<60} (JOHTO_FLAGS_START+0x{slots:03X}) // not defined in Heart & Soul's flags.h\n")
            note("flags not defined in Heart & Soul (given a slot anyway)", n)
            slots += 1
        out.append(f"\n#define JOHTO_FLAGS_USED 0x{slots:03X}\n")
        out.append(f"\n// Shared with the rest of the game (same meaning): {', '.join(shared)}\n")
        out.append("\n#endif // GUARD_CONSTANTS_FLAGS_HNS_H\n")
        assert slots <= 0x500, f"Johto flags: {slots} > 0x500"
        wr("include/constants/flags_hns.h", "".join(out))
        self.flag_count, self.flag_shared = slots, shared
        # ---- vars
        out = [GEN_C, "#ifndef GUARD_CONSTANTS_VARS_HNS_H\n#define GUARD_CONSTANTS_VARS_HNS_H\n\n",
               "// Heart & Soul's Johto and later-Kanto vars, in Johto's range (SaveBlock3).\n\n"]
        slot_of, slots, shared = {}, 0, []
        for n in self.used_vars:
            v = hev(n)
            if n in our_vars and (n.startswith("VAR_TEMP_") or n in SHARED_VARS or (v is not None and v >= 0x8000)
                                  or n.startswith("VAR_0x")):
                shared.append(n)
                continue
            if v is not None and v >= 0x8000:
                note("Heart & Soul special vars we don't have", n)
            new = n if n not in our_vars else "VAR_HNS_" + n[len("VAR_"):]
            if new != n:
                self.rename[n] = new
            key = v if v is not None else n
            if v is None:
                note("vars not defined in Heart & Soul (given a slot anyway)", n)
            if key not in slot_of:
                slot_of[key] = slots
                slots += 1
            out.append(f"#define {new:<50} (JOHTO_VARS_START+0x{slot_of[key]:02X})" +
                       (f" // HnS 0x{v:04X}\n" if v is not None else "\n"))
        out.append(f"\n#define JOHTO_VARS_USED 0x{slots:02X}\n")
        out.append(f"\n// Shared with the rest of the game: {', '.join(shared)}\n")
        out.append("\n#endif // GUARD_CONSTANTS_VARS_HNS_H\n")
        assert slots <= 0x100, f"Johto vars: {slots} > 0x100"
        wr("include/constants/vars_hns.h", "".join(out))
        self.var_count, self.var_shared = slots, shared
        # ---- trainers
        ourtr = set(re.findall(r"#define\s+(TRAINER_\w+)", rd("include/constants/opponents.h") +
                               rd("include/constants/opponents_kanto.h")))
        ourtr |= set(re.findall(r"\b(TRAINER_\w+)\b", rd("include/constants/trainers.h")))
        vals = [(hev(n), n) for n in self.used_trainers]
        bad = [n for v, n in vals if v is None]
        for n in bad:
            note("trainer constants not defined in Heart & Soul (left out)", n)
        vals = sorted((v, n) for v, n in vals if v is not None)
        out = [GEN_C, "#ifndef GUARD_CONSTANTS_OPPONENTS_HNS_H\n#define GUARD_CONSTANTS_OPPONENTS_HNS_H\n\n",
               "// Heart & Soul's trainers (Johto and later Kanto), after Kanto's (FireRed's).\n",
               "// Only the trainers the imported maps use; in Heart & Soul's order.\n\n"]
        self.trainer_new = {}
        for i, (v, n) in enumerate(vals):
            new = n if n not in ourtr else "TRAINER_HNS_" + n[len("TRAINER_"):]
            self.trainer_new[n] = new
            if new != n:
                self.rename[n] = new
            out.append(f"#define {new:<40} (JOHTO_TRAINERS_START+{i})\n")
        out.append(f"\n#define JOHTO_TRAINERS_COUNT {len(vals)}\n\n#endif // GUARD_CONSTANTS_OPPONENTS_HNS_H\n")
        wr("include/constants/opponents_hns.h", "".join(out))
        self.trainer_order = [n for v, n in vals]

    # --- map sections ---------------------------------------------------------------
    def gen_mapsecs(self):
        hs = json.load(open(f"{self.H}/src/data/region_map/region_map_sections.json"))["map_sections"]
        hs = {s["map_section"]: s for s in hs}
        rj = json.load(open("src/data/region_map/region_map_sections.json"))
        added = set(self.state.get("mapsecs", []))
        rj["map_sections"] = [s for s in rj["map_sections"] if s["id"] not in added]
        ours = [s["id"] for s in rj["map_sections"]]
        kanto = ours[ours.index("MAPSEC_PALLET_TOWN"):ours.index("MAPSEC_SPECIAL_AREA") + 1]
        used = []
        for m in self.maps:
            s = self.mapjson[m]["region_map_section"]
            if s not in used:
                used.append(s)
        self.mapsec = {}
        johto, kanto_new = [], []
        for s in used:
            region = mapsec_region(s)
            if region == "hoenn":
                if s in ours:
                    self.mapsec[s] = s
                    note("Hoenn map sections kept on imported maps", s)
                else:
                    self.mapsec[s] = "MAPSEC_NONE"
                continue
            if region == "kanto":
                if s in kanto:
                    self.mapsec[s] = s
                    continue
                k2 = "MAPSEC_KANTO_" + s[len("MAPSEC_"):]
                if k2 in kanto:
                    self.mapsec[s] = k2
                    continue
                new = s if s not in ours else k2
                kanto_new.append((new, s))
            else:
                new = s if s not in ours else "MAPSEC_JOHTO_" + s[len("MAPSEC_"):]
                johto.append((new, s))
            self.mapsec[s] = new
        entries = []
        for new, s in johto + kanto_new:
            e = OrderedDict([("id", new), ("name", hs.get(s, {}).get("name", new[7:].replace("_", " ")))])
            for k in ("x", "y", "width", "height"):
                if k in hs.get(s, {}):
                    e[k] = hs[s][k]
            entries.append(e)
        rj["map_sections"] += entries
        self.new_state["mapsecs"] = [e["id"] for e in entries]
        wjson("src/data/region_map/region_map_sections.json", rj)
        h = [GEN_C, "#ifndef GUARD_CONSTANTS_REGION_MAP_SECTIONS_HNS_H\n#define GUARD_CONSTANTS_REGION_MAP_SECTIONS_HNS_H\n\n",
             "// Heart & Soul's map sections, after all the others: Johto's, then the later-Kanto\n",
             "// places FireRed's Kanto block doesn't have. Kanto towns reuse FireRed's sections.\n"]
        if johto:
            h.append(f"#define JOHTO_MAPSEC_START {johto[0][0]}\n#define JOHTO_MAPSEC_END   {johto[-1][0]}\n")
        if kanto_new:
            h.append(f"#define KANTO_HNS_MAPSEC_START {kanto_new[0][0]}\n#define KANTO_HNS_MAPSEC_END   {kanto_new[-1][0]}\n")
        else:
            h.append("#define KANTO_HNS_MAPSEC_START MAPSEC_NONE\n#define KANTO_HNS_MAPSEC_END   MAPSEC_NONE\n")
        h.append("\n#endif\n")
        wr("include/constants/region_map_sections_hns.h", "".join(h))
        self.mapsec_lists = (johto, kanto_new)

    def map_region(self, m):
        s = self.mapjson[m]["region_map_section"]
        r = mapsec_region(s)
        return {"johto": "REGION_JOHTO", "kanto": "REGION_KANTO", "hoenn": "REGION_HOENN"}[r]

    # --- object graphics, items, music ------------------------------------------------
    def load_our_names(self):
        ev = rd("include/constants/event_objects.h")
        self.our_gfx = set(re.findall(r"\b(OBJ_EVENT_GFX_\w+)", ev))
        itxt = rd("include/constants/items.h")
        self.our_items = set(re.findall(r"\b(ITEM_\w+)\b", itxt))
        tms = rd("include/constants/tms_hms.h")
        tml = re.search(r"#define FOREACH_TM\(F\)(.*?)\n\n", tms, re.S).group(1)
        hml = re.search(r"#define FOREACH_HM\(F\)(.*?)\n\n", tms, re.S).group(1)
        for mv in re.findall(r"F\((\w+)\)", tml):
            self.our_items.add("ITEM_TM_" + mv)
        for mv in re.findall(r"F\((\w+)\)", hml):
            self.our_items.add("ITEM_HM_" + mv)
        self.our_songs = set(re.findall(r"#define\s+((?:MUS|SE)_\w+)", rd("include/constants/songs.h")))
        self.our_species = set(re.findall(r"\b(SPECIES_\w+)\b", rd("include/constants/species.h")))
        self.our_moves = set(re.findall(r"\b(MOVE_\w+)\b", rd("include/constants/moves.h")))

    def gfx(self, g):
        m = re.fullmatch(r"OBJ_EVENT_GFX_MON_BASE\s*\+\s*SPECIES_(\w+?)(\s*\+\s*SPECIES_SHINY_TAG)?", g)
        if m:
            return f"OBJ_EVENT_GFX_SPECIES{'_SHINY' if m.group(2) else ''}({m.group(1)})"
        if g in self.our_gfx:
            return g
        if g in hns_sprites.available(self.H):
            # Heart & Soul's sprite (or the same one of ours): gen_object_sprites() in run()
            self.hns_gfx_used.add(g)
            return "OBJ_EVENT_GFX_HNS_" + g[len("OBJ_EVENT_GFX_"):]
        note("object sprites Heart & Soul has no graphics for (became OBJ_EVENT_GFX_BOY_1)", g)
        return "OBJ_EVENT_GFX_BOY_1"

    def item(self, it):
        if it in ITEM_RENAME:
            return ITEM_RENAME[it]
        if it in self.our_items:
            return it
        note("items with no expansion counterpart (ITEM_HNS_* = ITEM_NONE for now)", it)
        self.missing_items.add(it)
        return "ITEM_HNS_" + it[len("ITEM_"):]

    # --- scripts --------------------------------------------------------------------
    def build_label_table(self):
        self.labels = {}
        self.defined = set()
        for m in self.maps:
            t = rd(f"{self.H}/data/maps/{m}/scripts.inc")
            for lab in re.findall(r"(?m)^[ \t]*(\w+)::?", t):
                self.defined.add(lab)
            for loc in re.findall(r"(?m)^\s*\.set\s+(LOCALID_\w+)", t):
                self.rename[loc] = "LOCALID_HNS_" + loc[len("LOCALID_"):]
        for lab in self.defined:
            for mn in self.map_names:
                if lab == mn or lab.startswith(mn + "_"):
                    self.labels[lab] = mn + "_Hns" + lab[len(mn):]
                    break
            else:
                self.labels[lab] = lab + "_Hns"
        # labels we already have
        ours = set()
        for root, _, files in os.walk("data"):
            for f in files:
                if f.endswith((".inc", ".s")) and "_Hns" not in root and not f.startswith("hns_"):
                    ours |= set(re.findall(r"(?m)^\s*(\w+)::?", rd(os.path.join(root, f))))
        self.our_labels = ours
        csrc = ""
        for root, _, files in os.walk("src"):
            for f in files:
                if f.endswith(".c"):
                    csrc += rd(os.path.join(root, f))
        self.our_c = csrc

    def token_sub(self, code):
        def rep(m):
            t = m.group(0)
            if t in self.labels:
                return self.labels[t]
            if t in self.rename:
                return self.rename[t]
            if t.startswith("MAP_") and t in self.map_const:
                return self.map_const[t]
            if t.startswith("LAYOUT_") and t in self.layout_const:
                return self.layout_const[t]
            if t.startswith("METATILE_"):
                self.used_metatiles.add(t)
                return "METATILE_Hns_" + t[len("METATILE_"):]
            if t.startswith("ITEM_"):
                return self.item(t)
            if t.startswith("OBJ_EVENT_GFX_") and t != "OBJ_EVENT_GFX_MON_BASE":
                return self.gfx(t)
            if t.startswith("MAPSEC_") and t in self.mapsec:
                return self.mapsec[t]
            if t.startswith("HEAL_LOCATION_") and t in self.heal_new:
                return self.heal_new[t]
            if t.startswith("MUS_") or t.startswith("SE_"):
                if t not in self.our_songs:
                    self.missing_songs.add(t)
            return t
        code = re.sub(r"OBJ_EVENT_GFX_MON_BASE\s*\+\s*SPECIES_\w+(?:\s*\+\s*SPECIES_SHINY_TAG)?",
                      lambda m: self.gfx(re.sub(r"\s", "", m.group(0)).replace("+", " + ")), code)
        code = re.sub(r"\bMAP_(GROUP|NUM)\((\w+)\)",
                      lambda m: f"MAP_{m.group(1)}({self.map_const.get('MAP_' + m.group(2), 'MAP_' + m.group(2))[4:]})", code)
        return re.sub(r"\b[A-Za-z_]\w*\b", rep, code)

    def convert_line(self, line, mapname):
        # split code / string / comment
        m = re.match(r'^(\s*\.string\s+)(".*)$', line)
        if m:
            # Heart & Soul's {RIVAL} is Silver: Johto's own rival name (named in Elm's lab),
            # not our {RIVAL} (the Hoenn/Sinnoh rival chosen before the intro)
            return line.replace("{RIVAL}", "{JOHTO_RIVAL}")
        code, comment = line, ""
        cm = re.search(r'\s@|^@|//', line)
        if cm and '"' not in line[:cm.start()]:
            code, comment = line[:cm.start()], line[cm.start():]
        s = code.strip()
        # Heart & Soul's randomizer / challenge helpers: not brought over
        if re.match(r"callnative\s+(Enable|Disable)StaticRandomizer\b", s):
            note("randomizer/challenge calls removed", s)
            return f"\t@ Pokémon World: removed ({s})"
        mm = re.match(r"(special|specialvar\s+\w+\s*,)\s*(IsNuzlockeNicknamingActive|IsPokecenterChallengeActivated|IsRandomMovesActivated)\b", s)
        if mm or s in ("checkrandomizer", "checknuzlocke"):
            note("randomizer/challenge calls removed", s)
            dest = "VAR_RESULT"
            if mm and mm.group(1).startswith("specialvar"):
                dest = mm.group(1).split()[1].rstrip(",")
            return f"\tsetvar {self.token_sub(dest)}, FALSE @ Pokémon World: was {s} (not brought over)"
        mm = re.match(r"^\s*(\w+)::?\s*$", code)
        if mm and mm.group(1) in DROPPED_SCRIPTS:
            note("scripts emptied (Heart & Soul options not brought over)", mm.group(1))
            return self.token_sub(code) + comment + f"\n\tend @ Pokémon World: {DROPPED_SCRIPTS[mm.group(1)]}"
        mm = re.match(r"(special|specialvar\s+\w+\s*,)\s*(\w+)\s*$", s)
        if mm and mm.group(2) in DROPPED_SPECIALS:
            note("specials left out (Heart & Soul options not brought over)", mm.group(2))
            return f"\t@ Pokémon World: removed ({s}): {DROPPED_SPECIALS[mm.group(2)]}"
        if mm and mm.group(2) in HNS_SPECIALS:
            note("Heart & Soul specials ported (src/hns_specials.c etc., Hns_*)", mm.group(2))
            self.stub_specials.add(mm.group(2))
            return self.token_sub(code.replace(mm.group(2), "Hns_" + mm.group(2))) + comment
        if mm and mm.group(2) not in self.our_specials and mm.group(2) not in HNS_SPECIALS:
            note("specials we don't have (call removed)", mm.group(2))
            return f"\t@ Pokémon World: removed ({s}) TODO"
        mm = re.match(r"callnative\s+(\w+)", s)
        if mm:
            fn = mm.group(1)
            if fn in DROPPED_NATIVES:
                note("natives left out", f"{fn}: {DROPPED_NATIVES[fn]}")
                return f"\t@ Pokémon World: removed ({s}): {DROPPED_NATIVES[fn]}"
            if not re.search(r"\b" + fn + r"\s*\(", self.our_c):
                note("natives we don't have (call removed)", fn)
                return f"\t@ Pokémon World: removed ({s}) TODO"
        mm = re.match(r"checkpartymove\s+MOVE_(\w+)", s)
        if mm and mm.group(1) in self.our_field_moves:
            return f"\tcheckfieldmove FIELD_MOVE_{mm.group(1)} @ Pokémon World: was {s}"
        if mm:   # not a field move here (Headbutt): Heart & Soul's rule, asm/macros/hns_compat.inc
            return f"\thnscheckpartymove MOVE_{mm.group(1)} @ Pokémon World: was {s}"
        mm = re.match(r"^(\s+)([A-Z][A-Z0-9_]*)\b(.*)$", code)
        if mm and mm.group(2).lower() in self.our_macros:
            code = mm.group(1) + mm.group(2).lower() + mm.group(3)   # Heart & Soul's assembler ignored case
        mm = re.match(r"^(\s*)\.align\s+(\d+)\s*$", code)
        if mm and int(mm.group(2)) > 2:
            return f"{mm.group(1)}.align 2" + comment
        mm = re.match(r"^(\s+)([a-z_][a-z0-9_]*)([ \t]+)(\S.*?)\s*$", code)
        if mm:
            ind, cmd, _, args = mm.groups()
            # Heart & Soul's assembler took arguments separated by spaces only
            args = re.sub(r"(?<=[\w)])[ \t]+(?=[A-Za-z_])", ", ", args)
            parts = [a.strip() for a in args.split(",")]
            if cmd in ("checkmoney", "removemoney", "addmoney"):
                args = parts[0]
            elif cmd in ("updatemoneybox", "hidecoinsbox", "updatecoinsbox", "hidemoneybox"):
                return self.token_sub(f"{ind}{cmd}") + comment
            elif cmd == "trainerbattle" and parts[0] in OLD_TRAINERBATTLE:
                kind = parts[0]
                t, intro, lose = parts[1], parts[3], parts[4] if len(parts) > 4 else parts[3]
                if kind in ("TRAINER_BATTLE_CONTINUE_SCRIPT", "TRAINER_BATTLE_CONTINUE_SCRIPT_NO_MUSIC"):
                    music = ", FALSE" if kind.endswith("NO_MUSIC") else ""
                    new = f"trainerbattle_single {t}, {intro}, {lose}, {parts[5]}{music}"
                else:
                    new = f"trainerbattle_single {t}, {intro}, {lose}"
                return self.token_sub(f"{ind}{new}") + f" @ Pokémon World: was trainerbattle {kind}" + comment
            code = f"{ind}{cmd} {args}"
        return self.token_sub(code) + comment

    def convert_scripts(self):
        self.used_metatiles, self.missing_songs, self.missing_items = set(), set(), set()
        self.stub_specials, self.stub_natives = set(), set()
        self.our_specials = set(re.findall(r"def_special\s+(\w+)", rd("data/specials.inc")))
        self.our_field_moves = set(re.findall(r"\bFIELD_MOVE_(\w+)", rd("include/constants/field_move.h")))
        self.our_macros = set()
        for r, _, fs in os.walk("asm"):
            for f in fs:
                if f.endswith(".inc"):
                    self.our_macros |= set(re.findall(r"\.macro\s+(\w+)", rd(os.path.join(r, f))))
                    self.our_macros |= set(re.findall(r"create_movement_action\s+(\w+)", rd(os.path.join(r, f))))
        for old in self.state.get("map_dirs", []):
            shutil.rmtree(old, ignore_errors=True)
        shared = self.hns_shared_blocks()
        extra = OrderedDict()   # shared HnS label -> its source block
        covered = set()
        while True:
            outs = {}
            for m in self.maps:
                src = rd(f"{self.H}/data/maps/{m}/scripts.inc")
                out = [GEN_ASM.rstrip("\n") + f" (Heart & Soul: data/maps/{m}/scripts.inc)"]
                for line in src.split("\n"):
                    out.append(self.convert_line(line, m))
                outs[m] = "\n".join(out).rstrip("\n") + "\n"
            sh = [GEN_ASM, "@ Scripts, texts and movements from Heart & Soul's shared files (data/scripts,\n",
                  "@ data/text, its Emerald maps) that the Johto and later-Kanto maps use.\n\n"]
            for lab, (where, block) in extra.items():
                sh.append(f"@ from Heart & Soul's {where}\n")
                sh += [self.convert_line(l, None) + "\n" for l in self.shared_sets.get(where, [])]
                sh += [self.convert_line(l, None) + "\n" for l in block]
                sh.append("\n")
            shared_txt = "".join(sh)
            # references to labels nobody defines
            refs = set()
            for t in list(outs.values()) + [shared_txt]:
                t = re.sub(r'"[^"\n]*"', "", t)
                t = re.sub(r"@[^\n]*", "", t)
                refs |= set(re.findall(r"\b([A-Z][A-Za-z0-9]*_[A-Za-z0-9_]*[a-z][A-Za-z0-9_]*)\b", t))
                refs |= set(re.findall(r"\b([A-Z][a-z][A-Za-z0-9_]*)\b", t))   # CamelCase labels
            for m in self.maps:
                for k in ("object_events", "coord_events", "bg_events"):
                    for o in self.mapjson[m].get(k, []):
                        s = o.get("script")
                        if s and s not in ("NULL", "0", "0x0"):
                            refs.add(self.labels.get(s, s))
            refs |= set(C_SCRIPTS)   # Heart & Soul scripts our C code runs
            newdefs = set(self.labels.values())
            missing = sorted(r for r in refs if r not in newdefs and r not in self.our_labels
                             and not r.startswith(("METATILE_", "OBJ_EVENT_GFX_", "LOCALID_", "Hns_", "VAR_", "FLAG_"))
                             and not re.search(r"\b" + r + r"\b", self.our_c))
            found = [r for r in missing if r in shared and r not in covered]
            if not found:
                break
            for r in found:
                if r in covered:
                    continue
                where, block, labs = shared[r]
                for k, l in enumerate(block):
                    mm = re.match(r"^\s*(\w+)::?", l)
                    if k and mm and (mm.group(1) in covered or mm.group(1) in self.our_labels):
                        block = block[:k]   # the rest is already brought over (or is ours)
                        labs = labs[:labs.index(mm.group(1))]
                        break
                extra[r] = (where, block)
                for lab in labs:
                    self.labels[lab] = lab + "_Hns"
                    covered.add(lab)
        for m, t in outs.items():
            wr(f"data/maps/{m}_Hns/scripts.inc", t)
        wr("data/scripts/hns_shared.inc", shared_txt)
        for r in extra:
            note("shared Heart & Soul scripts brought over (data/scripts/hns_shared.inc)", r)
        out = [GEN_ASM, "@ Labels the Heart & Soul maps use but that live in files we don't import\n",
               "@ (Heart & Soul's data/scripts/*.inc or Emerald maps it changed). TODO: bring the real\n",
               "@ scripts over; for now each one does nothing.\n\n"]
        for lab in missing:
            note("missing shared script labels (stubbed in data/scripts/hns_missing.inc)", lab)
            if "_Text_" in lab or lab.startswith("Text_"):
                out.append(f'{lab}::\n\t.string "…$"\n\n')
            elif "_Movement_" in lab or lab.startswith("Movement_"):
                out.append(f"{lab}::\n\tstep_end\n\n")
            else:
                out.append(f"{lab}::\n\treleaseall\n\tend\n\n")
        wr("data/scripts/hns_missing.inc", "".join(out))
        inc = [GEN_ASM, "@ Heart & Soul's map scripts (Johto and later Kanto).\n"]
        for m in self.maps:
            inc.append(f'\t.include "data/maps/{m}_Hns/scripts.inc"\n')
        inc.append('\t.include "data/scripts/hns_shared.inc"\n')
        inc.append('\t.include "data/scripts/hns_missing.inc"\n')
        wr("data/scripts/hns_maps.inc", "".join(inc))

    def hns_shared_blocks(self):
        """label -> (file, lines, labels defined in those lines), from HnS files we don't import whole."""
        files = []
        for root, _, fs in os.walk(f"{self.H}/data"):
            for f in fs:
                p = os.path.join(root, f)
                if not f.endswith((".inc", ".s")):
                    continue
                rel = os.path.relpath(p, self.H)
                mm = re.match(r"data/maps/(\w+)/", rel)
                if mm and mm.group(1) in self.mapjson:
                    continue
                files.append((rel, p))
        blocks = {}
        term = re.compile(r"^\s*(end|return|goto\s+\w+|step_end|releaseall\s*$|\.string\s+\".*\$\"|\.2byte\s+0|\.byte\s+0)\b")
        self.shared_sets = {}
        for rel, p in files:
            lines = rd(p).split("\n")
            sets = [l for l in lines if re.match(r"^\s*\.set\s+LOCALID_\w+", l)]
            self.shared_sets[rel] = sets
            for l in sets:
                loc = re.match(r"^\s*\.set\s+(LOCALID_\w+)", l).group(1)
                self.rename.setdefault(loc, "LOCALID_HNS_" + loc[len("LOCALID_"):])
            starts = [i for i, l in enumerate(lines) if re.match(r"^[ \t]*\w+::?(\s|$)", l)]
            for si, i in enumerate(starts):
                lab = re.match(r"^\s*(\w+)", lines[i]).group(1)
                if lab in blocks:
                    continue
                # take labels until the code stops falling through
                j, labs = si, []
                while True:
                    a = starts[j]
                    b = starts[j + 1] if j + 1 < len(starts) else len(lines)
                    labs.append(re.match(r"^\s*(\w+)", lines[a]).group(1))
                    code = [l for l in lines[a:b] if l.strip() and not l.strip().startswith(("@", "//", ".align"))]
                    if not code or term.match(code[-1]) or j + 1 >= len(starts) or j - si > 20:
                        break
                    j += 1
                end = starts[j + 1] if j + 1 < len(starts) else len(lines)
                blocks[lab] = (rel, [l for l in lines[i:end] if not l.startswith(("#include", "\t.include", ".include"))], labs)
        return blocks

    def convert_maps(self):
        dirs = []
        for m in self.maps:
            j = self.mapjson[m]
            o = OrderedDict()
            o["id"] = self.map_const[j["id"]]
            o["name"] = m + "_Hns"
            o["layout"] = self.layout_const[j["layout"]]
            o["music"] = j["music"]
            if j["music"] not in self.our_songs:
                self.missing_songs.add(j["music"])
            o["region"] = self.map_region(m)
            o["region_map_section"] = self.mapsec[j["region_map_section"]]
            for k in ("requires_flash", "weather", "map_type", "allow_cycling", "allow_escaping", "allow_running",
                      "show_map_name"):
                o[k] = j[k]
            o["floor_number"] = j.get("floor_number", 0)
            o["battle_scene"] = j["battle_scene"]
            conns = []
            for c in j.get("connections") or []:
                c = dict(c)
                if c["map"] in self.map_const:
                    c["map"] = self.map_const[c["map"]]
                else:
                    note("connections to maps we didn't import (dropped)", f"{m} -> {c['map']}")
                    continue
                conns.append(c)
            o["connections"] = conns
            objs = []
            for ob in j.get("object_events", []):
                ob = OrderedDict(ob)
                ob["graphics_id"] = self.gfx(ob["graphics_id"])
                s = ob.get("script", "NULL")
                ob["script"] = self.labels.get(s, s)
                f = ob.get("flag", "0")
                ob["flag"] = self.rename.get(f, f)
                objs.append(ob)
            o["object_events"] = objs
            warps = []
            for w in j.get("warp_events", []):
                w = OrderedDict(w)
                d = w["dest_map"]
                if d in self.map_const:
                    w["dest_map"] = self.map_const[d]
                elif d in self.our_map_consts or d in ("MAP_DYNAMIC", "MAP_UNDEFINED"):
                    if d not in ("MAP_DYNAMIC", "MAP_UNDEFINED"):
                        note("warps into our (Emerald) maps kept as they are", f"{m} -> {d}")
                else:
                    note("warps to maps we don't have (now MAP_UNDEFINED)", f"{m} -> {d}")
                    w["dest_map"] = "MAP_UNDEFINED"
                warps.append(w)
            o["warp_events"] = warps
            coords = []
            for c in j.get("coord_events", []):
                c = OrderedDict(c)
                if c.get("type") == "trigger":
                    c["var"] = self.rename.get(c["var"], c["var"])
                    c["script"] = self.labels.get(c["script"], c["script"])
                coords.append(c)
            o["coord_events"] = coords
            bgs = []
            for b in j.get("bg_events", []):
                b = OrderedDict(b)
                if b.get("type") == "sign":
                    b["script"] = self.labels.get(b["script"], b["script"])
                elif b.get("type") == "hidden_item":
                    b["item"] = self.item(b["item"])
                    b["flag"] = self.rename.get(b["flag"], b["flag"])
                bgs.append(b)
            o["bg_events"] = bgs
            dirs.append(f"data/maps/{m}_Hns")
            wjson(f"data/maps/{m}_Hns/map.json", o)
        self.new_state["map_dirs"] = dirs
        mg = json.load(open("data/maps/map_groups.json"), object_pairs_hook=OrderedDict)
        for grp in [g for g in mg["group_order"] if g.endswith("_Hns")]:
            mg["group_order"].remove(grp)
            mg.pop(grp, None)
        for grp in self.groups:
            ms = self.group_maps[grp]
            if not ms:
                continue
            ng = grp + "_Hns"
            mg["group_order"].append(ng)
            mg[ng] = [m + "_Hns" for m in ms]
        wjson("data/maps/map_groups.json", mg)

    # --- heal locations -------------------------------------------------------------
    def gen_heal_locations(self):
        txt = rd(f"{self.H}/src/data/heal_locations.h")
        self.heal_new = {}
        hj = json.load(open("src/data/heal_locations.json"), object_pairs_hook=OrderedDict)
        hj["heal_locations"] = [h for h in hj["heal_locations"] if not h["id"].endswith("_HNS")]
        for hid, grp, x, y in re.findall(r"\[(HEAL_LOCATION_\w+) - 1\]\s*=\s*\{MAP_GROUP\((\w+)\),\s*MAP_NUM\(\w+\),\s*(-?\d+),\s*(-?\d+)\}", txt):
            mc = "MAP_" + grp
            if mc not in self.map_const:
                continue
            new = hid + "_HNS"
            self.heal_new[hid] = new
            e = OrderedDict([("id", new), ("map", self.map_const[mc]), ("x", int(x)), ("y", int(y))])
            # where to go after a whiteout: the Pokémon Center whose door is just above
            town = next(m for m in self.maps if self.mapjson[m]["id"] == mc)
            for w in self.mapjson[town]["warp_events"]:
                if w["x"] == int(x) and w["y"] == int(y) - 1 and w["dest_map"] in self.map_const:
                    pc = next(m for m in self.maps if self.mapjson[m]["id"] == w["dest_map"])
                    for i, ob in enumerate(self.mapjson[pc]["object_events"]):
                        if "Nurse" in ob.get("script", "") or "NURSE" in ob.get("graphics_id", ""):
                            e["respawn_map"] = self.map_const[w["dest_map"]]
                            e["respawn_npc"] = str(i + 1)
                            e["respawn_x"] = ob["x"]
                            e["respawn_y"] = ob["y"] + 2
                            break
                    break
            hj["heal_locations"].append(e)
        # the ones with no Pokémon Center at the door: the player's home, or the nearest center
        byid = {e["id"]: e for e in hj["heal_locations"]}
        for hid, fix in RESPAWN_FIX.items():
            e = byid.get(hid + "_HNS")
            if not e or "respawn_map" in e:
                continue
            if isinstance(fix, str):
                src = byid[fix + "_HNS"]
                for k in ("respawn_map", "respawn_npc", "respawn_x", "respawn_y"):
                    e[k] = src[k]
            else:
                e["respawn_map"], e["respawn_npc"], e["respawn_x"], e["respawn_y"] = \
                    self.map_const[fix[0]], fix[1], fix[2], fix[3]
            note("heal locations given a respawn by hand (RESPAWN_FIX)", f"{e['id']} -> {e['respawn_map']}")
        for e in hj["heal_locations"]:
            if e["id"].endswith("_HNS") and "respawn_map" not in e:
                note("heal locations without a whiteout respawn (TODO)", e["id"])
        wjson("src/data/heal_locations.json", hj)

    # --- metatile labels and doors ------------------------------------------------------
    def gen_metatiles_doors(self):
        H = self.H
        mdefs = parse_defines([f"{H}/include/constants/metatile_labels.h"])
        door_src = rd(f"{H}/src/field_door.c")
        # which tileset each label belongs to
        cur, label_ts = None, {}
        for line in rd(f"{H}/include/constants/metatile_labels.h").split("\n"):
            mm = re.match(r"//\s*(gTileset_\w+)", line)
            if mm:
                cur = mm.group(1)
            mm = re.match(r"#define\s+(METATILE_\w+)", line)
            if mm:
                label_ts[mm.group(1)] = cur
        doors = re.findall(r"\{\s*(METATILE_\w+)\s*,\s*(DOOR_SOUND_\w+)\s*,\s*(DOOR_SIZE_\w+)\s*,\s*(sDoorAnimTiles_\w+)\s*,\s*(sDoorAnimPalettes_\w+)\s*\}", door_src)
        imported_ts = {s: n for s, n in self.ts_new.items()}
        table, gfx_needed = [], OrderedDict()
        for mt, snd, size, tiles, pals in doors:
            ts = label_ts.get(mt)
            if ts not in imported_ts or not imported_ts[ts].endswith("_Hns"):
                continue
            self.used_metatiles.add(mt)
            gfx_needed[tiles] = 1
            gfx_needed[pals] = 1
            table.append(f"    {{ .metatileNum = METATILE_Hns_{mt[9:]}, .tileset = &{imported_ts[ts]}, .sound = {snd}, "
                         f".size = {size}, .tiles = {tiles}_Hns, .palettes = {pals}_Hns }},\n")
        shutil.rmtree("graphics/door_anims/hns", ignore_errors=True)
        os.makedirs("graphics/door_anims/hns")
        g = [GEN_C, "// Heart & Soul's door animations. Included from field_door.c.\n"]
        for sym in gfx_needed:
            mm = re.search(r"static const u8 " + sym + r"\[\]\s*=\s*(INCBIN_U8\(\"([^\"]+)\"\)|\{([^}]*)\})", door_src)
            if not mm:
                note("door graphics not found in Heart & Soul", sym)
                continue
            if mm.group(2):
                png = mm.group(2).replace(".4bpp", ".png")
                dst = "graphics/door_anims/hns/" + os.path.basename(png)
                shutil.copy(f"{H}/{png}", dst)
                g.append(f'static const u8 {sym}_Hns[] = INCGFX_U8("{dst}", ".4bpp");\n')
            else:
                g.append(f"static const u8 {sym}_Hns[] = {{{mm.group(3)}}};\n")
        wr("src/data/field_door_hns_gfx.h", "".join(g))
        wr("src/data/field_door_hns_table.h", GEN_C + "// Heart & Soul's doors. Included inside sDoorAnimGraphicsTable.\n" + "".join(table))
        # metatile labels the imported scripts and doors use
        hev = evaluator(mdefs)
        out = [GEN_C, "#ifndef GUARD_METATILE_LABELS_HNS_H\n#define GUARD_METATILE_LABELS_HNS_H\n\n",
               "// Heart & Soul's metatile labels, METATILE_Hns_<Heart & Soul's name>.\n\n"]
        for mt in sorted(self.used_metatiles):
            v = hev(mt)
            if v is None:
                note("metatile labels Heart & Soul doesn't define (0 for now)", mt)
                v = 0
            out.append(f"#define METATILE_Hns_{mt[9:]:<56} 0x{v:03X}\n")
        out.append("\n#endif\n")
        wr("include/constants/metatile_labels_hns.h", "".join(out))

    # --- multichoice menus Heart & Soul added -------------------------------------------------
    def gen_multichoices(self):
        ours = set(re.findall(r"\b(MULTI_\w+)\b", rd("include/constants/script_menu.h")))
        used = set()
        for m in self.maps:
            used |= set(re.findall(r"\b(MULTI_\w+)\b", rd(f"data/maps/{m}_Hns/scripts.inc")))
        used |= set(re.findall(r"\b(MULTI_\w+)\b", rd("data/scripts/hns_shared.inc")))
        hsm = rd(f"{self.H}/src/data/script_menu.h")
        table = dict(re.findall(r"\[(MULTI_\w+)\]\s*=\s*MULTICHOICE\((\w+)\)", hsm))
        hstr = rd(f"{self.H}/src/strings.c")
        our_src = self.our_c + rd("include/strings.h")
        self.multi_new = []
        lists, entries, enum = [GEN_C, "// Heart & Soul's multichoice menus. Included from src/data/script_menu.h.\n\n"], \
            [GEN_C, "// Included inside sMultichoiceLists.\n"], \
            [GEN_C, "// Heart & Soul's multichoice ids, after ours (constants/script_menu.h).\n"]
        for mu in sorted(used - ours):
            if mu not in table:
                continue
            ln = table[mu]
            mm = re.search(r"static const struct MenuAction " + ln + r"\[\]\s*=\s*\{(.*?)\};", hsm, re.S)
            if not mm:
                note("multichoice menus not found in Heart & Soul", mu)
                continue
            items = []
            for t in re.findall(r"\{\s*(\w+)", mm.group(1)):
                if re.search(r"\b" + t + r"\b", our_src):
                    items.append(t)
                    continue
                sm = re.search(r"const u8 " + t + r"\[\]\s*=\s*_\(\"([^\"]*)\"\)", hstr)
                items.append(f'COMPOUND_STRING("{sm.group(1)}")' if sm else 'COMPOUND_STRING("???")')
            lists.append(f"static const struct MenuAction {ln}_Hns[] =\n{{\n" +
                         "".join(f"    {{{it}}},\n" for it in items) + "};\n\n")
            entries.append(f"    [{mu}] = MULTICHOICE({ln}_Hns),\n")
            enum.append(f"#define {mu:<28} (MULTI_HNS_START + {len(self.multi_new)})\n")
            self.multi_new.append(mu)
            note("multichoice menus brought over from Heart & Soul", mu)
        wr("src/data/script_menu_hns_lists.h", "".join(lists))
        wr("src/data/script_menu_hns_table.h", "".join(entries))
        wr("include/constants/script_menu_hns.h", "".join(enum))

    # --- constants Heart & Soul has and we don't ----------------------------------------------
    def find_unknown_constants(self):
        ours = set()
        for root in ("include", "asm", "constants"):
            for r, _, fs in os.walk(root):
                for f in fs:
                    if f.endswith((".h", ".inc")) and f != "hns_compat.h":
                        ours |= set(re.findall(r"\b[A-Z][A-Z0-9_]{2,}\b", rd(os.path.join(r, f))))
        ours |= set(re.findall(r"\b[A-Z][A-Z0-9_]{2,}\b", rd("src/data/region_map/region_map_sections.json")))
        ours |= self.our_items | set(self.multi_new)
        used, local = set(), set()
        texts = [rd(f"data/maps/{m}_Hns/scripts.inc") for m in self.maps] + [rd("data/scripts/hns_shared.inc")]
        for m in self.maps:
            texts.append(rd(f"data/maps/{m}_Hns/map.json").replace('"', " "))
        for t in texts:
            t = re.sub(r'"[^"\n]*"', "", t)
            t = re.sub(r"@[^\n]*", "", t)
            t = re.sub(r"OBJ_EVENT_GFX_SPECIES(_SHINY)?\(\w+\)", "", t)
            local |= set(re.findall(r"\.(?:set|equ|equiv)\s+(\w+)", t))
            for args in re.findall(r"\.macro\s+(.*)", t):
                local |= set(re.findall(r"\w+", args))
            t = re.sub(r"\\\w+", "", t)
            used |= set(re.findall(r"\b[A-Z][A-Z0-9_]{2,}\b", t))
        skip = ("LAYOUT_", "TRAINER_", "ITEM_HNS_", "OBJ_EVENT_GFX_HNS_", "LOCALID_HNS_",
                "HEAL_LOCATION_", "MAPSEC_", "MUS_", "SE_")
        ours |= set(self.map_const.values()) | set(self.rename.values()) | {"JOHTO_FLAGS_START", "JOHTO_VARS_START"}
        unknown = sorted(u for u in used if u not in ours and u not in local and not u.startswith(skip))
        for u in unknown:
            if u.startswith("MAP_BATTLE_SCENE_"):
                CONST_STANDIN.setdefault(u, "MAP_BATTLE_SCENE_NORMAL")
        hdefs = {}
        for r, _, fs in os.walk(f"{self.H}/include"):
            for f in fs:
                if f.endswith(".h"):
                    hdefs.update(parse_defines([os.path.join(r, f)]))
                    # simple enums
                    for body in re.findall(r"enum\s*\w*\s*\{(.*?)\}", re.sub(r"//[^\n]*|/\*.*?\*/", "", rd(os.path.join(r, f)), flags=re.S), re.S):
                        v = -1
                        for item in body.split(","):
                            item = item.strip()
                            mm = re.fullmatch(r"(\w+)(?:\s*=\s*(.+))?", item)
                            if not mm:
                                continue
                            if mm.group(2):
                                try:
                                    v = int(mm.group(2), 0)
                                except ValueError:
                                    v = None
                            else:
                                v = None if v is None else v + 1
                            if v is not None:
                                hdefs.setdefault(mm.group(1), str(v))
        hev = evaluator(hdefs)
        self.extra_consts = []
        for u in unknown:
            if u in CONST_STANDIN:
                note("Heart & Soul engine constants with a stand-in (TODO)", f"{u} -> {CONST_STANDIN[u]}")
                self.extra_consts.append((u, CONST_STANDIN[u]))
                continue
            v = hev(u)
            if v is None:
                note("constants Heart & Soul's scripts use that neither game defines (0 for now)", u)
                v = 0
            else:
                note("constants only Heart & Soul has (its value, in hns_compat.h)", f"{u} = {v}")
            self.extra_consts.append((u, v))

    # --- compat header ---------------------------------------------------------------------
    def gen_compat(self):
        out = [GEN_C, "#ifndef GUARD_CONSTANTS_HNS_COMPAT_H\n#define GUARD_CONSTANTS_HNS_COMPAT_H\n\n",
               "// Stand-ins for things Heart & Soul has and we don't have yet. TODO: replace each\n",
               "// with the real thing in a later stage (HGSS music, Johto sprites, Johto key items).\n\n",
               "// Music: Heart & Soul's HGSS songs, played as an existing song for now.\n"]
        for s in sorted(self.missing_songs):
            alt = MUSIC_STANDIN.get(s)
            if not alt:
                alt = "MUS_ROUTE101" if s.startswith("MUS_") else "SE_SELECT"
                note("songs with no hand-picked stand-in", s)
            note("music played as a stand-in song", f"{s} -> {alt}")
            out.append(f"#define {s:<34} {alt}\n")
        out.append("\n// Heart & Soul object sprites that are the same as one of ours (the others are\n"
                   "// Heart & Soul's own, in constants/event_objects.h; tools/pokemon_world/hns_sprites.py).\n")
        for g, ours in sorted(self.gfx_of.items()):
            if ours != "OBJ_EVENT_GFX_HNS_" + g[len("OBJ_EVENT_GFX_"):]:
                out.append(f"#define OBJ_EVENT_GFX_HNS_{g[len('OBJ_EVENT_GFX_'):]:<24} {ours}\n")
        out.append("\n// Items with no expansion counterpart (Johto key items etc.).\n")
        for it in sorted(self.missing_items):
            out.append(f"#define ITEM_HNS_{it[5:]:<24} ITEM_NONE\n")
        out.append("\n// Other constants Heart & Soul's scripts use that we don't have (Heart & Soul's values).\n")
        for u, v in self.extra_consts:
            out.append(f"#define {u:<40} {v}\n")
        out.append("\n#endif // GUARD_CONSTANTS_HNS_COMPAT_H\n")
        wr("include/constants/hns_compat.h", "".join(out))

    # --- trainer classes, pics and encounter music ---------------------------------------------
    def gen_trainer_classes(self, used):
        """HnS class -> ours. Same class here (by name, or FireRed's _FRLG one, or CLASS_MAP) is
        reused; Heart & Soul-only classes become TRAINER_CLASS_*_HNS with HnS's name and money."""
        H = self.H
        ours = set(re.findall(r"\b(TRAINER_CLASS_\w+)\b", rd("include/constants/trainers.h")))
        names = dict(re.findall(r"\[(TRAINER_CLASS_\w+)\]\s*=\s*_\(\"([^\"]*)\"\)", rd(f"{H}/src/data/text/trainer_class_names.h")))
        money = dict(re.findall(r"\{\s*(TRAINER_CLASS_\w+)\s*,\s*(\d+)\s*\}", rd(f"{H}/src/battle_main.c")))
        cls_of, new = {}, []
        for c in used:
            if c in CLASS_MAP:
                cls_of[c] = CLASS_MAP[c]
            elif c in ours and not c.endswith("_HNS"):
                cls_of[c] = c
            elif c + "_FRLG" in ours:
                cls_of[c] = c + "_FRLG"
            else:
                cls_of[c] = c + "_HNS"
                new.append(c)
                note("trainer classes brought over from Heart & Soul", f"{c}_HNS: \"{names.get(c, '?')}\", money {money.get(c, '5 (default)')}")
                continue
            if cls_of[c] != c:
                note("trainer classes reused from ours (Heart & Soul -> ours)", f"{c} -> {cls_of[c]}")
        enum = []
        table = [GEN_C, "// Heart & Soul's trainer classes (name, money as in Heart & Soul). Included inside gTrainerClasses.\n"]
        for c in new:
            enum.append(f"    {c}_HNS,\n")
            table.append(f'    [{c}_HNS] = {{ _("{names.get(c, c[14:].replace("_", " "))}"), {money.get(c, 5)} }},\n')
        # (in trainers.h itself: the assembler's preprocessor can't follow an #include inside an enum)
        splice("include/constants/trainers.h", "HNS_TRAINER_CLASSES", "".join(enum))
        wr("src/data/trainer_classes_hns.h", "".join(table))
        self.classes_added = new
        return cls_of

    def gen_trainer_pics(self, used):
        """HnS pic -> ours when the picture is the same (pixel for pixel, or PIC_MAP), otherwise
        Heart & Soul's front pic is copied to graphics/trainers/front_pics/hns as TRAINER_PIC_*_HNS."""
        H = self.H
        # ours: pic constant -> png
        gsrc = "".join(rd(p) for p in sorted(glob.glob("src/data/graphics/trainers*.h")) if not p.endswith("_hns.h")
                       and "_hns_" not in p)
        files = dict(re.findall(r"(gTrainerFrontPic_\w+)\[\]\s*=\s*INCGFX_U32\(\"([^\"]+)\"", gsrc))
        our_pic = {}
        for pc, sym in re.findall(r"\[(TRAINER_PIC_\w+)\]\s*=\s*\{\s*\.frontPic\s*=\s*TRAINER_FRONT_PIC\((gTrainerFrontPic_\w+)", gsrc):
            if sym in files:
                our_pic.setdefault(pc, files[sym])
        by_key = defaultdict(list)
        for pc, f in our_pic.items():
            k = png_key(f)
            if k:
                by_key[k].append(pc)
        # Heart & Soul: pic constant -> png
        hsrc = rd(f"{H}/src/data/graphics/trainers.h")
        hfiles = {re.sub(r"^gTrainerFrontPic_?", "", s): f.replace(".4bpp.lz", ".png")
                  for s, f in re.findall(r"(gTrainerFrontPic_?\w+)\[\]\s*=\s*INCBIN_U32\(\"([^\"]+)\"", hsrc)}
        htab = {"TRAINER_PIC_" + a: re.sub(r"^gTrainerFrontPic_?", "", b) for a, b in
                re.findall(r"TRAINER_SPRITE\((\w+),\s*(gTrainerFrontPic_?\w+)", rd(f"{H}/src/data/trainer_graphics/front_pic_tables.h"))}
        for old in self.state.get("trainer_pics", []):
            if os.path.exists(old):
                os.remove(old)
        pic_of, new = {}, []
        for p in used:
            if p in PIC_MAP:
                pic_of[p] = PIC_MAP[p]
                note("trainer pics reused from ours (same character, our drawing)", f"{p} -> {PIC_MAP[p]}")
                continue
            src = hfiles.get(htab.get(p, ""))
            if not src or not os.path.exists(f"{H}/{src}"):
                note("trainer pics not found in Heart & Soul (became TRAINER_PIC_YOUNGSTER)", p)
                pic_of[p] = "TRAINER_PIC_YOUNGSTER"
                continue
            same = by_key.get(png_key(f"{H}/{src}"), [])
            if same:
                best = sorted(same, key=lambda c: (c != p, c != p + "_FRLG", c))[0]
                pic_of[p] = best
                if best != p:
                    note("trainer pics reused from ours (identical picture)", f"{p} -> {best}")
                continue
            base = p[len("TRAINER_PIC_"):].lower()
            dst = f"graphics/trainers/front_pics/hns/{base}.png"
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy(f"{H}/{src}", dst)
            pic_of[p] = p + "_HNS"
            new.append((p, dst, "".join(w.title() for w in base.split("_"))))
            note("trainer pics brought over from Heart & Soul", f"{p}_HNS <- {src}")
        self.new_state["trainer_pics"] = [d for _, d, _ in new]
        enum = []
        gfx =[GEN_C, "// Heart & Soul's trainer front pics (graphics/trainers/front_pics/hns). Included from\n"
               "// data/graphics/trainers.h.\n"]
        tab = [GEN_C, "// Included inside gTrainerPicInfo.\n"]
        for p, dst, camel in new:
            enum.append(f"    {p}_HNS,\n")
            gfx.append(f'const u32 gTrainerFrontPic_{camel}_Hns[] = INCGFX_U32("{dst}", ".4bpp.smol");\n'
                       f'const u16 gTrainerPalette_{camel}_Hns[] = INCGFX_U16("{dst}", ".gbapal");\n')
            tab.append(f"    [{p}_HNS] =\n    {{\n        .frontPic = TRAINER_FRONT_PIC(gTrainerFrontPic_{camel}_Hns, "
                       f"gTrainerPalette_{camel}_Hns),\n    }},\n")
        splice("include/constants/trainers.h", "HNS_TRAINER_PICS", "".join(enum))
        wr("src/data/graphics/trainers_hns_gfx.h", "".join(gfx))
        wr("src/data/graphics/trainers_hns_table.h", "".join(tab))
        self.pics_added = new
        return pic_of

    def gen_trainer_music(self):
        out = [GEN_C, "#ifndef GUARD_CONSTANTS_TRAINER_MUSIC_HNS_H\n#define GUARD_CONSTANTS_TRAINER_MUSIC_HNS_H\n\n",
               "// Heart & Soul's trainer encounter music, as one of our encounter styles until the HGSS\n",
               "// music comes in (stage 4). The table is ENCOUNTER_MUSIC in tools/pokemon_world/import_hns.py:\n",
               "// to give these their own songs, add TRAINER_ENCOUNTER_MUSIC_* values (and their songs in\n",
               "// src/data/trainer_approach_music.h) and point these at them there.\n\n"]
        for h, (ours, why) in ENCOUNTER_MUSIC.items():
            line = f"#define TRAINER_ENCOUNTER_MUSIC_HNS_{h:<16} TRAINER_ENCOUNTER_MUSIC_{ours:<11}"
            out.append((f"{line} // {why}" if why else line.rstrip()) + "\n")
        out.append("\n#endif\n")
        wr("include/constants/trainer_music_hns.h", "".join(out))

    # --- wild encounters -------------------------------------------------------------------
    def gen_wild(self):
        """Heart & Soul's wild encounters for every imported map, into our wild_encounters.json.
        Heart & Soul keeps a day table and (for many maps) a night table right after it; these
        become gX_Hns and gX_Hns_Night (expansion's OW_TIME_OF_DAY_ENCOUNTERS; morning, day and
        evening fall back to the first). Heart & Soul's optional "modern spawns" tables (CDay /
        CNight) are not brought over."""
        h = json.load(open(f"{self.H}/src/data/wild_encounters.json"))
        hg = next(g for g in h["wild_encounter_groups"] if g.get("label") == "gWildMonHeaders")
        oj = json.load(open("src/data/wild_encounters.json"), object_pairs_hook=OrderedDict)
        og = next(g for g in oj["wild_encounter_groups"] if g.get("label") == "gWildMonHeaders")
        hf = {f["type"]: f for f in hg["fields"]}
        for f in og["fields"]:
            if f["type"] in hf and hf[f["type"]]["encounter_rates"] != f["encounter_rates"]:
                sys.exit(f"wild encounters: {f['type']} slot rates differ from Heart & Soul's")
        og["encounters"] = [e for e in og["encounters"] if not e.get("map", "").endswith("_HNS")]
        times = ("Morning", "Day", "Evening", "Night")
        tables, maps = 0, 0
        i, enc = 0, hg["encounters"]
        while i < len(enc):
            e = enc[i]
            m = e.get("map")
            if m not in self.map_const:
                i += 1
                continue
            group = [e]
            while i + len(group) < len(enc) and enc[i + len(group)].get("map") == m:
                group.append(enc[i + len(group)])
            i += len(group)
            # Heart & Soul: VAR_TIME_BASED_ENCOUNTER 1 = this map's first table, 2 = the one after it
            picks = [("", group[0])]
            if len(group) > 1 and group[1]["base_label"].endswith("Night"):
                picks.append(("_Night", group[1]))
            for g in group[len(picks):]:
                note("wild encounter tables not brought over (Heart & Soul's optional modern spawns etc.)", g["base_label"])
            base = re.sub(r"_(Night|Day)$", "", group[0]["base_label"]) + "_Hns"
            if any(t in base for t in times):
                base = base.replace("Day", "Dy").replace("Night", "Nt").replace("Morning", "Mrn").replace("Evening", "Evn")
            maps += 1
            for sfx, g in picks:
                o = OrderedDict([("map", self.map_const[m]), ("base_label", base + sfx)])
                for k in ("land_mons", "water_mons", "rock_smash_mons", "fishing_mons"):
                    if k not in g:
                        continue
                    mons = []
                    for mon in g[k]["mons"]:
                        sp = mon["species"]
                        if sp not in self.our_species:
                            note("wild species with no expansion counterpart (became SPECIES_RATTATA)", sp)
                            sp = "SPECIES_RATTATA"
                        mons.append(OrderedDict([("min_level", mon["min_level"]), ("max_level", mon["max_level"]),
                                                 ("species", sp)]))
                    o[k] = OrderedDict([("encounter_rate", g[k]["encounter_rate"]), ("mons", mons)])
                    tables += 1
                og["encounters"].append(o)
        wjson("src/data/wild_encounters.json", oj)
        self.wild_counts = (maps, tables)

    # --- trainers ------------------------------------------------------------------------
    def gen_trainers(self):
        H = self.H
        tsrc = rd(f"{H}/src/data/trainers.h")
        psrc = rd(f"{H}/src/data/trainer_parties.h")
        parties = {}
        for m in re.finditer(r"static const struct (TrainerMon\w+) (sParty_\w+)\[\]\s*=\s*\{(.*?)\n\};", psrc, re.S):
            mons = []
            for mon in re.finditer(r"\{([^{}]*(?:\{[^{}]*\}[^{}]*)?)\}", m.group(3)):
                b = mon.group(1)
                f = dict(re.findall(r"\.(\w+)\s*=\s*([^,\n{}]+)", b))
                mv = re.search(r"\.moves\s*=\s*\{([^}]*)\}", b)
                if "species" not in f:
                    continue
                mons.append((f.get("species").strip(), f.get("lvl", "5").strip(), f.get("iv", "0").strip(),
                             f.get("heldItem", "ITEM_NONE").strip(),
                             [x.strip() for x in mv.group(1).split(",") if x.strip() and x.strip() != "MOVE_NONE"] if mv else []))
            parties[m.group(2)] = mons
        trainers = {}
        for m in re.finditer(r"\[(TRAINER_\w+)\]\s*=\s*\{(.*?)\n    \},", tsrc, re.S):
            trainers[m.group(1)] = m.group(2)
        fields = {n: dict(re.findall(r"\.(\w+)\s*=\s*([^\n]+?),?\s*$", trainers[n], re.M))
                  for n in self.trainer_order if n in trainers}
        used_classes = sorted({f.get("trainerClass", "TRAINER_CLASS_PKMN_TRAINER_1").strip() for f in fields.values()})
        used_pics = sorted({f.get("trainerPic", "TRAINER_PIC_YOUNGSTER").strip() for f in fields.values()})
        cls_of = self.gen_trainer_classes(used_classes)
        pic_of = self.gen_trainer_pics(used_pics)
        self.gen_trainer_music()
        ai_ok = set(re.findall(r"#define\s+AI_FLAG_(\w+)\s+AI_FLAG\(", rd("include/constants/battle_ai.h")))
        out = [GEN_PARTY, "/* Heart & Soul's trainers (Johto and later Kanto). Parties, AI flags and items are\n",
               "   Heart & Soul's. Classes and pics are ours where we have the same one, otherwise\n",
               "   Heart & Soul's (TRAINER_CLASS_*_HNS, TRAINER_PIC_*_HNS). Music is Heart & Soul's\n",
               "   encounter style, TRAINER_ENCOUNTER_MUSIC_HNS_* (see constants/trainer_music_hns.h). */\n\n"]
        for n in self.trainer_order:
            out.append(f"=== {self.trainer_new[n]} ===\n")
            if n not in fields:
                note("trainers with no data in Heart & Soul (placeholder party)", n)
                out.append("Name: ???\nClass: TRAINER_CLASS_PKMN_TRAINER_1\nPic: TRAINER_PIC_YOUNGSTER\n"
                           "Gender: Male\nMusic: TRAINER_ENCOUNTER_MUSIC_MALE\nDouble Battle: No\nAI: Check Bad Move\n\n"
                           "SPECIES_RATTATA\nLevel: 5\n\n")
                continue
            f = fields[n]
            name = re.search(r'_\("([^"]*)"\)', f.get("trainerName", '_("???")'))
            name = name.group(1) if name else "???"
            if "{" in name:
                note("trainer names with a placeholder (Heart & Soul shows the rival's name; TODO)", f"{n}: {name}")
                name = re.sub(r"\{[^}]*\}", "SILVER", name)
            hcls = f.get("trainerClass", "TRAINER_CLASS_PKMN_TRAINER_1").strip()
            hpic = f.get("trainerPic", "TRAINER_PIC_YOUNGSTER").strip()
            cls, pic = cls_of[hcls], pic_of[hpic]
            mg = f.get("encounterMusic_gender", "TRAINER_ENCOUNTER_MUSIC_MALE")
            hmus = (re.search(r"TRAINER_ENCOUNTER_MUSIC_(\w+)", mg) or re.search(r"(MALE)", "MALE")).group(1)
            if hmus not in ENCOUNTER_MUSIC:
                note("encounter music Heart & Soul uses that the mapping table lacks (became MALE)", hmus)
                hmus = "MALE"
            female = ("F_TRAINER_FEMALE" in mg or hmus in ("FEMALE", "GIRL", "HG_GIRL_1", "HG_GIRL_2")
                      or hpic in FEMALE_PICS)
            dbl = "TRUE" in f.get("doubleBattle", "FALSE")
            ai = []
            for a in re.findall(r"AI_SCRIPT_(\w+)", f.get("aiFlags", "")):
                a = AI_RENAME.get(a, a)
                if a in ai_ok:
                    ai.append(a.replace("_", " ").title())
                else:
                    note("Heart & Soul AI flags with no expansion counterpart (dropped)", a)
            items = []
            for it in re.findall(r"\bITEM_\w+", f.get("items", "")):
                if it != "ITEM_NONE":
                    it = self.item(it)
                    if not it.startswith("ITEM_HNS_"):
                        items.append(it)
            pm = re.search(r"\((sParty_\w+)\)", f.get("party", ""))
            mons = parties.get(pm.group(1)) if pm else None
            out.append(f"Name: {name}\nClass: {cls}\nPic: {pic}\nGender: {'Female' if female else 'Male'}\n"
                       f"Music: TRAINER_ENCOUNTER_MUSIC_HNS_{hmus}\n")
            if items:
                out.append(f"Items: {' / '.join(items[:4])}\n")
            out.append(f"Double Battle: {'Yes' if dbl else 'No'}\n")
            if ai:
                out.append(f"AI: {' / '.join(ai)}\n")
            out.append("\n")
            if not mons:
                note("trainers whose party couldn't be read (placeholder party)", n)
                mons = [("SPECIES_RATTATA", "5", "0", "ITEM_NONE", [])]
            for sp, lvl, iv, held, moves in mons[:6]:
                if sp not in self.our_species:
                    note("species with no expansion counterpart", sp)
                    sp = "SPECIES_RATTATA"
                held = self.item(held) if held != "ITEM_NONE" else held
                line = sp + (f" @ {held}" if held not in ("ITEM_NONE",) and not held.startswith("ITEM_HNS_") else "")
                out.append(line + "\n")
                try:
                    ivn = min(31, int(iv, 0) * 31 // 255)
                except ValueError:
                    ivn = 0
                out.append(f"Level: {lvl}\nIVs: {ivn} HP / {ivn} Atk / {ivn} Def / {ivn} SpA / {ivn} SpD / {ivn} Spe\n")
                for mv in moves:
                    mv = MOVE_RENAME.get(mv, mv)
                    if mv in self.our_moves:
                        out.append(f"- {mv}\n")
                    else:
                        note("moves with no expansion counterpart", mv)
                out.append("\n")
        wr("src/data/trainers_hns.party", "".join(out))

    def run(self):
        self.load_maps()
        self.load_our_names()
        self.load_tilesets()
        self.behavior_map()
        self.import_tilesets()
        self.import_layouts()
        self.collect_used()
        self.heal_new = {}
        self.missing_items, self.missing_songs, self.used_metatiles = set(), set(), set()
        first = True
        while True:
            self.gen_flags_vars()
            if first:
                self.gen_mapsecs()
                self.gen_heal_locations()
                first = False
            self.build_label_table()
            self.convert_scripts()
            # the shared scripts brought over can use flags/vars of their own
            sh = rd("data/scripts/hns_shared.inc")
            nf = sorted(set(re.findall(r"\bFLAG_\w+", sh)) - set(self.used_flags) - set(self.rename.values())
                        - set(re.findall(r"#define\s+(FLAG_\w+)", rd("include/constants/flags.h") + rd("include/constants/flags_kanto.h"))))
            nv = sorted(set(re.findall(r"\bVAR_\w+", sh)) - set(self.used_vars) - set(self.rename.values())
                        - set(re.findall(r"#define\s+(VAR_\w+)", rd("include/constants/vars.h") + rd("include/constants/vars_kanto.h"))))
            nf = [f for f in nf if f not in ("FLAG_HNS_",)]
            if not nf and not nv:
                break
            self.used_flags = sorted(set(self.used_flags) | set(nf))
            self.used_vars = sorted(set(self.used_vars) | set(nv))
        self.convert_maps()
        self.gen_metatiles_doors()
        self.gen_trainers()
        self.gen_wild()
        self.gen_multichoices()
        self.find_unknown_constants()
        self.gfx_of = hns_sprites.gen_object_sprites(self.H, sorted(self.hns_gfx_used), self.state, self.new_state, note)
        self.gen_compat()
        json.dump(self.new_state, open(STATE, "w"), indent=1)
        open(STATE, "a").write("\n")
        print(f"Heart & Soul import: {len(self.maps)} maps, {len(self.layout_const)} layouts, "
              f"{len(self.ts_out)} tilesets ({len(self.ts_new) - len(self.ts_out)} reused)")
        print(f"Johto flags: 0x{self.flag_count:03X} of 0x500, vars: 0x{self.var_count:02X} of 0x100, "
              f"trainers: {len(self.trainer_order)}")
        print(f"Map sections: {len(self.mapsec_lists[0])} Johto, {len(self.mapsec_lists[1])} later Kanto; "
              f"heal locations: {len(self.heal_new)}")
        print(f"Trainer classes added: {len(self.classes_added)}, trainer pics added: {len(self.pics_added)}; "
              f"wild encounters: {self.wild_counts[0]} maps, {self.wild_counts[1]} tables")
        for k in sorted(REPORT):
            v = sorted(REPORT[k])
            print(f"\n== {k} ({len(v)})")
            print("   " + "\n   ".join(v))


# ---------------------------------------------------------------------------------
# Hand-made tables

JOHTO_WORDS = ["NEW_BARK", "CHERRYGROVE", "VIOLET", "AZALEA", "GOLDENROD", "ECRUTEAK", "OLIVINE", "CIANWOOD",
               "MAHOGANY", "BLACKTHORN", "SAFARI_ZONE", "DARK_CAVE", "UNION_CAVE", "ILEX", "NATIONAL_PARK",
               "WHIRL", "CLIFF", "MT_MORTAR", "LAKE_OF_RAGE", "ICE_PATH", "MT_SILVER", "TOHJO", "SPROUT",
               "SLOWPOKE", "BURNED", "TIN_TOWER", "DRAGONS_DEN", "RUINS_OF_ALPH", "SS_AQUA", "EMBEDDED",
               "LIGHTHOUSE", "ROUTE_26", "ROUTE_27"]
HOENN_SECS = {"MAPSEC_SOUTHERN_ISLAND", "MAPSEC_FARAWAY_ISLAND", "MAPSEC_BIRTH_ISLAND", "MAPSEC_BATTLE_FRONTIER",
              "MAPSEC_FORTREE_CITY", "MAPSEC_LILYCOVE_CITY", "MAPSEC_MOSSDEEP_CITY", "MAPSEC_SOOTOPOLIS_CITY",
              "MAPSEC_TRAINER_HILL"}


def mapsec_region(s):
    if s in HOENN_SECS or s in ("MAPSEC_NONE", "MAPSEC_DYNAMIC"):
        return "hoenn"
    m = re.fullmatch(r"MAPSEC_ROUTE_(\d+)", s)
    if m:
        return "johto" if 26 <= int(m.group(1)) <= 27 or int(m.group(1)) >= 29 else "kanto"
    return "johto" if any(w in s for w in JOHTO_WORDS) else "kanto"


SHARED_FLAGS = {"FLAG_SYS_B_DASH", "FLAG_SYS_CTRL_OBJ_DELETE", "FLAG_SYS_CYCLING_ROAD", "FLAG_SYS_NATIONAL_DEX",
                "FLAG_SYS_POKEDEX_GET", "FLAG_SYS_POKEMON_GET", "FLAG_SYS_POKENAV_GET", "FLAG_PENDING_DAYCARE_EGG",
                "FLAG_SET_WALL_CLOCK", "FLAG_TEMP_HIDE_FOLLOWER"}
SHARED_VARS = {"VAR_ICE_STEP_COUNT", "VAR_SAFARI_ZONE_STATE", "VAR_TRAINER_HILL_IS_ACTIVE",
               "VAR_POKELOT_PRIZE_ITEM", "VAR_POKELOT_PRIZE_PLACE"}

# Heart & Soul specials for Johto features we don't have, ported as Hns_<name> (src/hns_specials.c,
# src/hns_bug_contest.c, src/hns_sliding_puzzle.c); the scripts call Hns_<name>.
HNS_SPECIALS = ["CheckAerodactyl", "CheckCelebi", "CheckHooh", "CheckKabuto", "CheckOmanyte", "CheckTogepi",
                "DoSlidingPuzzle", "GetBattleFrontierTutorMoveIndex", "BufferBattleFrontierTutorMoveName",
                "GetDaycareCost", "GetMaxPartySize", "HaircutBrother1", "NameRival", "SwitchMonAbility",
                "EnterBugContestMode", "ExitBugContestMode", "JudgeBugContestMon", "TransferBugContestMon",
                "RemoveSafariBalls", "ShowBugContestChosenMon",
                "SetHpIvs", "SetAtkIvs", "SetDefIvs", "SetSpdIvs", "SetSpAtkIvs", "SetSpDefIvs",
                "SetHpEvs", "SetAtkEvs", "SetDefEvs", "SetSpdEvs", "SetSpAtkEvs", "SetSpDefEvs"]
# Heart & Soul specials / natives / scripts that only serve its own options: left out.
DROPPED_SPECIALS = {"ToggleShinyColors": "Heart & Soul's shiny-colour option isn't brought over"}
DROPPED_NATIVES = {"SetTimeBasedEncounters": "day/night tables are expansion's time-of-day encounters"}
DROPPED_SCRIPTS = {"GoldenrodCity_NameRatersHouse_EventScript_ToggleShinies":
                   "Heart & Soul's shiny-colour option isn't brought over"}
# Heart & Soul scripts our C code starts (the Bug-Catching Contest's timer and whiteout).
C_SCRIPTS = ["BugContest_EventScript_TimesUp", "BugContest_EventScript_WhiteOut",
             "EventScript_Headbutt"]   # Headbutt trees (MB_HEADBUTT_TREE, field_control_avatar.c)
# Heart & Soul engine flags our ported C code uses (given Johto flags like the scripts' ones).
ENGINE_FLAGS = ["FLAG_SYS_BUG_CONTEST_MODE"]

# Tileset animations ported to src/data/tilesets/tileset_anims_hns.h
ANIM_PORTED = {"InitTilesetAnim_JohtoGeneral": "InitTilesetAnim_JohtoGeneral_Hns"}

# Trainer classes: Heart & Soul's -> ours, where the name differs but it's the same class.
# (Same-named classes and FireRed's _FRLG ones are found by name; anything else becomes _HNS.)
CLASS_MAP = {
    "TRAINER_CLASS_TEAM_ROCKET": "TRAINER_CLASS_TEAM_ROCKET_FRLG",
    "TRAINER_CLASS_PSYCHIC_M": "TRAINER_CLASS_PSYCHIC",       # both "PSYCHIC"
    "TRAINER_CLASS_POLICEMAN": "TRAINER_CLASS_POLICEMAN",     # HnS calls it OFFICER; same class (Sinnoh's)
}
# Trainer pics that are the same character as one of ours but drawn differently: use ours.
# (Pixel-identical pics are found automatically.)
PIC_MAP = {
    "TRAINER_PIC_POLICEMAN": "TRAINER_PIC_SINNOH_POLICEMAN",  # HGSS Officer = DPPt Policeman
}
# Heart & Soul pics of women/girls (for the trainer's gender; Heart & Soul rarely sets it).
FEMALE_PICS = {
    "TRAINER_PIC_POKEMON_BREEDER_F", "TRAINER_PIC_ROCKET_GRUNT_F", "TRAINER_PIC_COOLTRAINER_F", "TRAINER_PIC_LADY",
    "TRAINER_PIC_BEAUTY", "TRAINER_PIC_EXPERT_F", "TRAINER_PIC_ARIANA", "TRAINER_PIC_PICNICKER", "TRAINER_PIC_PSYCHIC_F",
    "TRAINER_PIC_ELITE_FOUR_KAREN", "TRAINER_PIC_LEADER_WHITNEY", "TRAINER_PIC_LEADER_JASMINE", "TRAINER_PIC_LEADER_CLAIR",
    "TRAINER_PIC_SCHOOL_KID_F", "TRAINER_PIC_POKEFAN_F", "TRAINER_PIC_KIMONO_GIRL", "TRAINER_PIC_SWIMMING_TRIATHLETE_F",
    "TRAINER_PIC_DRAGON_TAMER", "TRAINER_PIC_BATTLE_GIRL", "TRAINER_PIC_PARASOL_LADY", "TRAINER_PIC_SWIMMER_F",
    "TRAINER_PIC_TWINS", "TRAINER_PIC_MAY", "TRAINER_PIC_POKEMON_RANGER_F", "TRAINER_PIC_LASS",
    "TRAINER_PIC_LEADER_MISTY", "TRAINER_PIC_LEADER_ERIKA", "TRAINER_PIC_LEADER_JANINE", "TRAINER_PIC_LEADER_SABRINA",
    "TRAINER_PIC_LEAF", "TRAINER_PIC_RS_MAY", "TRAINER_PIC_NURSE", "TRAINER_PIC_AROMA_LADY", "TRAINER_PIC_TUBER_F",
}
# Heart & Soul's AI_SCRIPT_* -> expansion's AI_FLAG_* (same name unless listed here).
AI_RENAME = {"SETUP_FIRST_TURN": "FORCE_SETUP_FIRST_TURN"}
# Heart & Soul's trainer encounter music -> one of our encounter styles, until HGSS music (stage 4).
# Written to include/constants/trainer_music_hns.h as TRAINER_ENCOUNTER_MUSIC_HNS_<key>.
ENCOUNTER_MUSIC = OrderedDict([
    ("MALE", ("MALE", "Heart & Soul: standard male")),
    ("FEMALE", ("FEMALE", "Kimono Girls")),
    ("GIRL", ("GIRL", "")),
    ("SUSPICIOUS", ("SUSPICIOUS", "")),
    ("INTENSE", ("INTENSE", "")),
    ("COOL", ("COOL", "")),
    ("ROCKET", ("DANGEROUS", "Team Rocket: FireRed's Rocket theme (HGSS: MUS_HG_ENCOUNTER_ROCKET)")),
    ("MAGMA", ("MAGMA", "")),
    ("SWIMMER", ("SWIMMER", "")),
    ("TWINS", ("TWINS", "")),
    ("ELITE_FOUR", ("ELITE_FOUR", "Elite Four")),
    ("HIKER", ("HIKER", "")),
    ("SILVER", ("INTENSE", "Silver (HGSS: MUS_HG_ENCOUNTER_RIVAL)")),
    ("RICH", ("RICH", "")),
    ("HG_CHAMPION", ("ELITE_FOUR", "Lance, Red (HGSS: MUS_HG_VS_CHAMPION)")),
    ("HG_BOY_1", ("SERIOUS", "Youngsters, Bug Catchers, Leaders... (HGSS: MUS_HG_ENCOUNTER_BOY_1)")),
    ("HG_BOY_2", ("SERIOUS", "Bikers, Swimmers, Officers (HGSS: MUS_HG_ENCOUNTER_BOY_2)")),
    ("HG_GIRL_1", ("CUTE", "Lasses, Picnickers, Twins (HGSS: MUS_HG_ENCOUNTER_GIRL_1)")),
    ("HG_GIRL_2", ("FEMALE", "Beauties, female Cooltrainers, Swimmers (HGSS: MUS_HG_ENCOUNTER_GIRL_2)")),
    ("HG_SUSPICIOUS_1", ("DANGEROUS", "Super Nerds, Jugglers (HGSS: MUS_HG_ENCOUNTER_SUSPICIOUS_1)")),
    ("HG_SUSPICIOUS_2", ("HIKER", "Hikers, Fishermen, Black Belts, Sailors (HGSS: MUS_HG_ENCOUNTER_SUSPICIOUS_2)")),
    ("HG_SAGE", ("SUSPICIOUS", "Sages, Mediums (HGSS: MUS_HG_ENCOUNTER_SAGE)")),
])
# Heal locations with no Pokémon Center at the door: (respawn map, healer local id, x, y), or another
# heal location whose respawn to use. Local id 1 in a house = Mom (EventScript_AfterWhiteOutMomHeal).
RESPAWN_FIX = {
    "HEAL_LOCATION_NEW_BARK_TOWN": ("MAP_NEW_BARK_TOWN_PLAYERS_HOUSE_1F", "1", 2, 3),
    "HEAL_LOCATION_NEW_BARK_TOWN_PLAYERS_HOUSE_2F": ("MAP_NEW_BARK_TOWN_PLAYERS_HOUSE_1F", "1", 2, 3),
    "HEAL_LOCATION_PALLET_TOWN": ("MAP_PALLET_TOWN_REDS_HOUSE_1F", "1", 7, 5),
    "HEAL_LOCATION_LAKE_OF_RAGE": "HEAL_LOCATION_MAHOGANYTOWN",   # nearest Pokémon Center
    "HEAL_LOCATION_ROUTE26NORTH": "HEAL_LOCATION_VIRIDIAN_CITY",  # through the gate and Route 22
    "HEAL_LOCATION_NEW_MAP1": "HEAL_LOCATION_NEW_BARK_TOWN",      # unused test map, no warps
}

ITEM_RENAME = {"ITEM_HM_FLASH": "ITEM_TM_FLASH", "ITEM_EXP_SHARE_SMALL": "ITEM_EXP_SHARE"}
MOVE_RENAME = {"MOVE_SMELLING_SALT": "MOVE_SMELLING_SALTS"}
# Heart & Soul engine values whose number means something else here.
CONST_STANDIN = {"MOVEMENT_TYPE_TOWER_BEAM": "MOVEMENT_TYPE_NONE",
                 "SPECIAL_BATTLE_LANCE": "SPECIAL_BATTLE_MULTI",
                 "TUTOR_MOVE_HEADBUTT": "0",
                 "SCROLL_MULTI_BF_MOVE_TUTOR_3": "SCROLL_MULTI_BF_MOVE_TUTOR_1"}
# Heart & Soul's raw trainerbattle types -> our trainerbattle_single (expansion pairs up
# two approaching trainers by itself, so SET_TRAINER_A/B become ordinary battles).
OLD_TRAINERBATTLE = {"TRAINER_BATTLE_CONTINUE_SCRIPT", "TRAINER_BATTLE_CONTINUE_SCRIPT_NO_MUSIC",
                     "TRAINER_BATTLE_SET_TRAINER_A", "TRAINER_BATTLE_SET_TRAINER_B", "TRAINER_BATTLE_SINGLE"}

# Heart & Soul's overworld sprites (OBJ_EVENT_GFX_HNS_*): see hns_sprites.py.
import hns_sprites  # noqa: E402  (next to this script)

# HGSS songs -> closest song we have, until the HGSS music is imported.
MUSIC_STANDIN = {
    "MUS_HG_PALLET": "MUS_RG_PALLET", "MUS_HG_ROUTE1": "MUS_RG_ROUTE1", "MUS_HG_PEWTER": "MUS_RG_PEWTER",
    "MUS_HG_CERULEAN": "MUS_RG_ROUTE24", "MUS_HG_VERMILION": "MUS_RG_VERMILLION", "MUS_HG_LAVENDER": "MUS_RG_LAVENDER",
    "MUS_HG_CELADON": "MUS_RG_CELADON", "MUS_HG_CINNABAR": "MUS_RG_CINNABAR", "MUS_HG_ROUTE3": "MUS_RG_ROUTE3",
    "MUS_HG_ROUTE11": "MUS_RG_ROUTE11", "MUS_HG_ROUTE24": "MUS_RG_ROUTE24", "MUS_HG_ROCK_TUNNEL": "MUS_RG_MT_MOON",
    "MUS_HG_MT_MOON_SQUARE": "MUS_RG_MT_MOON", "MUS_HG_VIRIDIAN_FOREST": "MUS_RG_VIRIDIAN_FOREST",
    "MUS_HG_OAK": "MUS_RG_OAK", "MUS_HG_VICTORY_ROAD": "MUS_RG_VICTORY_ROAD", "MUS_HG_POKEMON_LEAGUE": "MUS_EVER_GRANDE",
    "MUS_HG_GAME_CORNER": "MUS_RG_GAME_CORNER", "MUS_HG_ENCOUNTER_RIVAL": "MUS_RG_ENCOUNTER_RIVAL",
    "MUS_HG_RIVAL_EXIT": "MUS_RG_RIVAL_EXIT", "MUS_HG_FOLLOW_ME_1": "MUS_RG_FOLLOW_ME",
    "MUS_HG_POKE_CENTER": "MUS_RG_POKE_CENTER", "MUS_HG_POKE_MART": "MUS_POKE_MART", "MUS_HG_GYM": "MUS_RG_GYM",
    "MUS_HG_TEAM_ROCKET_HQ": "MUS_RG_ROCKET_HIDEOUT", "MUS_HG_ROCKET_TAKEOVER": "MUS_RG_SILPH",
    "MUS_HG_RADIO_ROCKET": "MUS_RG_SILPH", "MUS_HG_RADIO_POKE_FLUTE": "MUS_RG_POKE_FLUTE",
    "MUS_HG_SS_AQUA": "MUS_RG_SS_ANNE", "MUS_HG_SAFARI_ZONE": "MUS_SAFARI_ZONE", "MUS_HG_SAFARI_ZONE_GATE": "MUS_SAFARI_ZONE",
    "MUS_HG_NEW_BARK": "MUS_LITTLEROOT", "MUS_HG_ELM_LAB": "MUS_BIRCH_LAB", "MUS_HG_CHERRYGROVE": "MUS_OLDALE",
    "MUS_HG_VIOLET": "MUS_PETALBURG", "MUS_HG_AZALEA": "MUS_VERDANTURF", "MUS_HG_GOLDENROD": "MUS_RUSTBORO",
    "MUS_HG_ECRUTEAK": "MUS_FORTREE", "MUS_HG_CIANWOOD": "MUS_DEWFORD", "MUS_HG_ROUTE29": "MUS_ROUTE101",
    "MUS_HG_ROUTE30": "MUS_ROUTE104", "MUS_HG_ROUTE34": "MUS_ROUTE110", "MUS_HG_ROUTE38": "MUS_GSC_ROUTE38",
    "MUS_HG_ROUTE42": "MUS_ROUTE119", "MUS_HG_ROUTE47": "MUS_ROUTE120", "MUS_HG_ROUTE26": "MUS_ROUTE113",
    "MUS_HG_UNION_CAVE": "MUS_RG_SEVII_CAVE", "MUS_HG_ICE_PATH": "MUS_RG_SEVII_CAVE", "MUS_HG_DRAGONS_DEN": "MUS_SEALED_CHAMBER",
    "MUS_HG_RUINS_OF_ALPH": "MUS_SEALED_CHAMBER", "MUS_HG_SPROUT_TOWER": "MUS_MT_PYRE", "MUS_HG_BELL_TOWER": "MUS_MT_PYRE",
    "MUS_HG_BURNED_TOWER": "MUS_MT_PYRE", "MUS_HG_LIGHTHOUSE": "MUS_RG_POKE_TOWER", "MUS_HG_NATIONAL_PARK": "MUS_PETALBURG_WOODS",
    "MUS_HG_DANCE_THEATER": "MUS_CONTEST_LOBBY", "MUS_HG_KIMONO_GIRL": "MUS_CONTEST_LOBBY",
    "MUS_HG_KIMONO_GIRL_DANCE": "MUS_CONTEST", "MUS_HG_EUSINE": "MUS_ENCOUNTER_SUSPICIOUS",
    "MUS_HG_POKEGEAR_REGISTERED": "MUS_REGISTER_MATCH_CALL", "MUS_HG_VS_HO_OH": "MUS_RG_VS_LEGEND",
    "MUS_HG_VS_LUGIA": "MUS_RG_VS_LEGEND", "MUS_PL_FIGHT_AREA_DAY": "MUS_DPP_LEAGUE_OUTSIDE",
    "MUS_WEATHER_KYOGRE": "MUS_ABNORMAL_WEATHER",
}


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--hns", default=os.path.expanduser("~/ref/pokemonHnS"))
    a = ap.parse_args()
    if not os.path.exists("data/maps/map_groups.json"):
        sys.exit("run from the repo root")
    Importer(a.hns).run()
