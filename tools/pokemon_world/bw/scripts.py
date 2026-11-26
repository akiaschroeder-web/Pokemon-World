"""Pokémon World: reads Pokémon Black's event scripts (a/0/5/7) and text (a/0/0/3).

Script file: s32 offsets (each relative to the end of its own field), ended by u16 0xFD13; script id n
is entry n-1. An instruction is a u16 opcode and its parameters, unpadded, in the order of
bw_script_commands.json (PokeScriptSDK5's BW command list, via CTRMapV). Jumps/calls are s32 relative
to the end of the instruction.

Text: the converter's decode_text(); control codes 0xF000 <code> <n> <args...>, 0xFFFE line break.

For now this pulls what an NPC, sign or object says out of its script: the messages along the
script's main path, in order. Full script conversion (flags, choices, battles, movement) comes later.
"""

import json
import os
import struct

_CMDS = None
SIZES = {'byte': 1, 'ushort': 2, 'const ushort': 2, 'ref ushort': 2, 'fx16': 2, 'int': 4, 'fx32': 4}
MSG_ZONE_FILE = 1024      # textFile argument meaning "this zone's text file"
# message commands: opcode -> (index of the text file param or None, index of the message id param)
MESSAGES = {0x3D: (0, 1), 0x3C: (0, 1), 0x48: (0, 1), 0x49: (0, 1), 0x34: (None, 0), 0x35: (None, 0),
            0x38: (None, 0), 0x43: (None, 0), 0x4A: (None, 0)}
YESNO = 0x47


def commands():
    global _CMDS
    if _CMDS is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bw_script_commands.json')
        _CMDS = {int(k): [p['Type'] for p in v['params']] for k, v in json.load(open(path)).items()}
    return _CMDS


def entries(d):
    out, pos = [], 0
    while pos + 2 <= len(d) and struct.unpack_from('<H', d, pos)[0] != 0xFD13:
        out.append(pos + 4 + struct.unpack_from('<i', d, pos)[0])
        pos += 4
    return out


def walk(d, start, limit=400):
    """The instructions along a script's main path from start: [(opcode, args)]. Follows unconditional
    jumps and local calls; stops at the end, a return or anything it can't read."""
    cmds = commands()
    out, pc, seen, stack = [], start, set(), []
    while 0 <= pc < len(d) - 1 and len(out) < limit:
        if pc in seen:
            break
        seen.add(pc)
        op = struct.unpack_from('<H', d, pc)[0]
        if op not in cmds:
            break
        p = pc + 2
        args = []
        for t in cmds[op]:
            n = SIZES[t]
            if p + n > len(d):
                return out
            args.append(d[p] if n == 1 else struct.unpack_from('<H' if t != 'fx16' else '<h', d, p)[0] if n == 2
                        else struct.unpack_from('<i', d, p)[0])
            p += n
        out.append((op, args))
        if op in (0x02, 0x1D):
            break
        if op == 0x05:
            if not stack:
                break
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
    return out


def script_messages(rom, zone, scrid, female=False):
    """[message id, ...] said along the main path of zone-local script scrid (1-based)."""
    files = rom.narc('a/0/5/7')
    if zone.scripts >= len(files) or scrid <= 0:
        return []
    d = files[zone.scripts]
    es = entries(d)
    if scrid > len(es):
        return []
    out = []
    for op, args in walk(d, es[scrid - 1]):
        if op in MESSAGES:
            fi, mi = MESSAGES[op]
            if fi is not None and args[fi] != MSG_ZONE_FILE:
                continue
            msg = args[mi + 1] if op == 0x48 and female else args[mi]
            if msg < 0x4000:
                out.append(msg)
        if op == YESNO:
            break            # what follows depends on the answer: stop at the question for now
    return out


def flags_cleared(rom):
    """Flags any BW script clears (FlagReset with a constant flag)."""
    out = set()
    for d in rom.narc('a/0/5/7'):
        try:
            es = entries(d)
        except Exception:
            continue
        if len(es) > 3000:
            continue
        for e in es:
            for op, args in walk(d, e):
                if op == 0x24 and args[0] < 0x4000:
                    out.add(args[0])
    return out


def actor_new_objs(rom):
    """objCodes BW scripts bring in with ActorNew (0x69: x, z, dir, actor, objCode, moveCode)."""
    out = set()
    for d in rom.narc('a/0/5/7'):
        try:
            es = entries(d)
        except Exception:
            continue
        if len(es) > 3000:
            continue
        for e in es:
            for op, args in walk(d, e):
                if op == 0x69 and args[4] < 0x4000:
                    out.add(args[4])
    return out


# --- text -> our script strings ---

ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,!?'\"-:;/()&%+=~<>$#*@"
              "ÀÁÂÄÇÈÉÊËÌÍÎÏÑÒÓÔÖÙÚÛÜßàáâäçèéêëìíîïñòóôöùúûüŒœ‘’“”…♀♂×¡¿")


def to_vm_string(codes):
    """Like to_script_string, for text the BW script interpreter shows: BW's word buffers (filled by
    its WordSet* commands, e.g. the player's name) become {STR_VAR_1..3}."""
    return to_script_string(codes, buffers=True)


def to_script_string(codes, buffers=False):
    """BW text (decoded code units) -> a pokeemerald .string body, pages and lines like BW's."""
    out, line, i = '', 0, 0
    quote_open = False
    while i < len(codes):
        c = codes[i]
        if c == 0xFFFF:
            break
        if c == 0xFFFE:
            if not out.endswith('\\p'):        # BW puts a line break after a page break; ours doesn't need one
                out += '\\n' if line == 0 else '\\l'
                line += 1
        elif c == 0xF000 and i + 2 < len(codes):
            code, n = codes[i + 1], codes[i + 2]
            if code == 0xBE01:                  # clear the box: new page
                if out.endswith('\\n') or out.endswith('\\l'):
                    out = out[:-2]
                out += '\\p'
                line = 0
            elif code == 0xBE00:                # scroll a line
                if out.endswith('\\n') or out.endswith('\\l'):
                    out = out[:-2]
                out += '\\l'
            elif buffers and 0x0100 <= code < 0x0300 and n:
                out += '{STR_VAR_%d}' % min(3, codes[i + 3] + 1)   # word buffer (name, item, number...)
            elif code in (0x0100, 0x0101):      # trainer (player) name
                out += '{PLAYER}'
            i += 2 + n
        elif 0xD800 <= c <= 0xDFFF:
            pass
        else:
            ch = chr(c)
            if ch == '’':
                ch = "'"
            elif ch == '‘':
                ch = "'"
            elif ch == '"':
                quote_open = not quote_open        # the text tools can't take straight double quotes
                ch = '“' if quote_open else '”'
            elif ch == '…':
                ch = '...'
            elif ch == '　':
                ch = ' '
            if len(ch) == 1 and ch not in ALLOWED:
                ch = '?'
            out += ch
        i += 1
    while out.endswith('\\p') or out.endswith('\\n') or out.endswith('\\l'):
        out = out[:-2]
    return out
