"""Pokémon World: checks every BW scene's choreography against Unova's maps, before anything is built.

For each zone script it follows the main path (scripts.walk), keeping every actor's tile:
  NPCs start where the zone's entities put them; ActorNew / ActorSetGPos / ActorJumpToGPos place them;
  movement lists (ActorCmdExec) move them by bw/movement.py's table - the same one the game uses.
The player is only tracked in scripts started by a step trigger (they stand on the trigger tile).
A step onto a blocked tile, or out of the zone, is reported (a few are fine: BW lets actors walk
through furniture on purpose, e.g. off-screen exits). Writes tools/pokemon_world/bw_scene_report.txt.
"""

import collections
import struct

import movement
import scripts

PLAYER = 255


def check(rom, zones, blocked, npc_starts, trigger_scripts, names):
    """blocked(zone, x, z) -> True/False/None (None: not in this zone).
    npc_starts[zone] = {actor uid: (x, z)}; trigger_scripts[zone] = {script id: (x, z)}."""
    files = rom.narc('a/0/5/7')
    problems = []
    stats = collections.Counter()
    for z in zones:
        d = files[z.scripts]
        try:
            es = scripts.entries(d)
        except Exception:
            continue
        for sid in range(1, len(es) + 1):
            pos = dict(npc_starts.get(z.id, {}))
            if sid in trigger_scripts.get(z.id, {}):
                pos[PLAYER] = trigger_scripts[z.id][sid]
            pc = es[sid - 1]
            moved = False
            for op, args in walk_with_lists(d, pc):
                if op == 0x69:                       # ActorNew x, z, dir, actor, obj, move
                    if args[0] < 0x4000 and args[1] < 0x4000 and args[3] < 0x4000:
                        pos[args[3]] = (args[0], args[1])
                elif op == 0x241 and PLAYER in pos:  # companions (src/bw_script.c SpawnCompanion)
                    px, pz = (788, 737) if z.id == 389 else pos[PLAYER]
                    pos[args[0]] = (px - 1, pz - 1) if args[0] == 250 else (px + 1, pz)
                elif op in (0x6D, 0x7B):             # ActorSetGPos / JumpToGPos: actor, x, y, z
                    if args[0] < 0x4000 and args[1] < 0x4000 and args[3] < 0x4000:
                        pos[args[0]] = (args[1], args[3])
                elif op == 0x64:
                    actor, entries = args
                    if actor >= 0x4000 or actor not in pos:
                        continue
                    moved = True
                    p, facing = pos[actor], 1
                    for code, count in entries:
                        visited, p, facing = movement.step(code, count, p, facing)
                        for t in visited:
                            stats['steps'] += 1
                            b = blocked(z.id, t[0], t[1])
                            if b is None or b:
                                stats['blocked' if b else 'outside'] += 1
                                problems.append((z.id, sid, actor, t, 'outside the zone' if b is None else 'blocked tile'))
                    pos[actor] = p
            if moved:
                stats['scenes'] += 1
    with open('tools/pokemon_world/bw_scene_report.txt', 'w', encoding='utf-8') as f:
        f.write('BW scene check (tools/pokemon_world/bw/scenes.py): %(scenes)d scenes with movement, '
                '%(steps)d steps, %(blocked)d onto blocked tiles, %(outside)d outside the zone\n\n' % stats)
        by_scene = collections.defaultdict(list)
        for zid, sid, actor, t, why in problems:
            by_scene[(zid, sid)].append('actor %d at %s (%s)' % (actor, t, why))
        for (zid, sid), lines in sorted(by_scene.items()):
            f.write('zone %d %s, script %d: %s\n' % (zid, names.get(zid, ''), sid, '; '.join(lines[:6])))
    print('scene check: %(scenes)d scenes, %(steps)d steps, %(blocked)d onto blocked tiles, %(outside)d outside' % stats)
    return stats


def walk_with_lists(d, start):
    """scripts.walk, with ActorCmdExec (0x64 actor, list offset) resolved to its [(code, count)]."""
    cmds = scripts.commands()
    pc, seen, stack, n = start, set(), [], 0
    while 0 <= pc < len(d) - 1 and n < 600:
        if pc in seen:
            return
        seen.add(pc)
        n += 1
        op = struct.unpack_from('<H', d, pc)[0]
        if op not in cmds:
            return
        p, args = pc + 2, []
        for t in cmds[op]:
            k = scripts.SIZES[t]
            if p + k > len(d):
                return
            args.append(d[p] if k == 1 else struct.unpack_from('<H' if t != 'fx16' else '<h', d, p)[0] if k == 2
                        else struct.unpack_from('<i', d, p)[0])
            p += k
        if op == 0x64:
            t, entries = p + args[-1], []
            while 0 <= t <= len(d) - 4 and len(entries) < 64:
                code, count = struct.unpack_from('<HH', d, t)
                t += 4
                if code == 0xFE:
                    break
                entries.append((code, count))
            yield op, (args[0], entries)
        else:
            yield op, args
        if op in (0x02, 0x1D, 0x47):     # end, or a question (what follows depends on the answer)
            return
        if op == 0x05:
            if not stack:
                return
            pc = stack.pop()
            continue
        if op == 0x1E:
            pc = p + args[-1]
            continue
        if op == 0x04:
            stack.append(p)
            pc = p + args[-1]
            continue
        pc = p


if __name__ == '__main__':
    # quick re-check from the importer's saved inputs: python3 bw/scenes.py <Pokemon Black .nds>
    import os
    import pickle
    import sys
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    import import_bw
    data = pickle.load(open(os.path.expanduser('~/.cache_bw_scenes.pickle'), 'rb'))
    Z = collections.namedtuple('Z', 'id scripts')
    grid = data['grid']
    check(import_bw.Rom(sys.argv[1]), [Z(*z) for z in data['zones']], lambda zid, x, z: grid.get((zid, x, z)),
          data['starts'], data['triggers'], data['names'])
