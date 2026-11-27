"""Pokémon World: BW's animated buildings ("static props") - doors, the Pokémon Center's door, the PC,
fountains, flowing water, TVs, warp pads, the smoke in the player's room after the first battle...

A building pack (a/2/2/9 outside, a/2/3/0 inside) is a GF container: n resource headers, then n
models. A header (format after CTRMapV's AreaBuildingResource):
  s16 uid, u16 type (0 plain, 1 door, 2 Pokémon Center door, 7 PC, 10 warp pad, 11 event...),
  s16 door uid, s16 door x/y/z (where the building's door goes, in its own space), s16, s16,
  then at 16: u8 animation kind (1 always playing, 2 played by the game/scripts, 3 several),
  u8, s8 animation sets, s8 animation count, s32 x 4 offsets (from 16) of its animation files
  (NSBCA joints, NSBTA texture scrolling, NSBTP texture patterns, NSBMA material colours).

Such a building isn't merged into its chunk's mesh: it's written once per pack as its own model,
one baked clip per animation (bw/demos.py's write_model), and each chunk lists where its
animated buildings stand (import_bw.py's Converter.chunk: "props"). A building with a door gets
its door building placed at the door position; the page (unova3d.js) draws them, plays the
always-on ones and the ones the scripts ask for (src/bw_script.c: StaticProp / BM* commands).

Output: graphics/unova/props/<pack>_<uid>.json {kind, type, clips: [model info + frames]} and the
clips' .bin / .anim / tex/*.png.
"""

import json
import os
import struct

import demos
import export3d
import nitro

OUT = 'graphics/unova/props'


def s16(b, o): return struct.unpack_from('<h', b, o)[0]
def u16(b, o): return struct.unpack_from('<H', b, o)[0]


def header(h):
    uid, typ, door, dx, dy, dz = struct.unpack_from('<hHhhhh', h, 0)
    kind, _, sets, count = struct.unpack_from('<BBbb', h, 16)
    anims = []
    for k in range(4):
        off = struct.unpack_from('<i', h, 20 + 4 * k)[0]
        if off == -1 or off < 0 or 16 + off + 12 > len(h):
            continue
        size = struct.unpack_from('<I', h, 16 + off + 8)[0]
        anims.append(bytes(h[16 + off:16 + off + size]))
    return {'uid': uid, 'type': typ, 'door': door, 'door_pos': (dx, dy, dz), 'kind': kind, 'anims': anims}


def frame_count(magic, buf):
    blocks = nitro.blocks(buf)
    tag = {b'BCA0': b'JNT0', b'BTA0': b'SRT0', b'BTP0': b'PAT0', b'BMA0': b'MAT0'}.get(magic)
    b = blocks.get(tag)
    if b is None:
        return 0
    _, it = nitro.read_dict(buf, b + 8)[0]
    return u16(buf, b + struct.unpack_from('<I', buf, it)[0] + 4)


class Props:
    def __init__(self):
        self.done = {}

    def export(self, key, info, bmd, tex_btx):
        """Writes building `key` (pack_uid) once; returns its file name (or None if it can't be read)."""
        if key in self.done:
            return self.done[key]
        os.makedirs(OUT + '/tex', exist_ok=True)
        try:
            md = nitro.load_bmd(bmd)[0]
        except Exception:
            self.done[key] = None
            return None
        clips = []
        anims = info['anims'] or [None]
        for k, a in enumerate(anims):
            magic = a[:4] if a else None
            curves, pats, srt, frames = {}, {}, {}, 0
            if magic == b'BCA0':
                curves, frames = demos.load_bca(a)
            elif magic == b'BTA0':
                srt = demos.load_bta(a)
                frames = frame_count(magic, a)
            elif magic == b'BTP0':
                pats = demos.load_btp(a)
                frames = frame_count(magic, a)
            mesh = demos.run_sbc_bones(md)
            # only joint animations need their bones baked per frame (texture ones: one pose)
            baked = max(frames, 1) if magic == b'BCA0' else 1
            mi = demos.write_model(None, None, md, mesh, curves, (0, 0, None), pats, srt, baked, bmd,
                                   name='%s_%d' % (key, k), out=OUT, texset=export3d.TextureSet(tex_btx))
            mi['frames'] = frames
            mi['magic'] = magic.decode('latin1') if magic else ''
            if clips and len(mi['textures']) == len(clips[0]['textures']):
                # the same model's textures: keep the first clip's files
                for t in mi['textures']:
                    try:
                        os.remove('%s/tex/%s' % (OUT, t))
                    except OSError:
                        pass
                mi['textures'] = clips[0]['textures']
            clips.append(mi)
        with open('%s/%s.json' % (OUT, key), 'w') as f:
            json.dump({'kind': info['kind'], 'type': info['type'], 'clips': clips}, f, separators=(',', ':'))
        self.done[key] = key
        return key
