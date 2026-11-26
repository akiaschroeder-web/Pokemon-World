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


def every_instruction(d, starts):
    """All instructions reachable from the start offsets, following every branch: [(opcode, args)]."""
    cmds = commands()
    out, todo, seen = [], list(starts), set()
    while todo:
        pc = todo.pop()
        while 0 <= pc < len(d) - 1 and pc not in seen:
            seen.add(pc)
            op = struct.unpack_from('<H', d, pc)[0]
            if op not in cmds:
                break
            p, args = pc + 2, []
            for t in cmds[op]:
                n = SIZES[t]
                if p + n > len(d):
                    break
                args.append(d[p] if n == 1 else struct.unpack_from('<H' if t != 'fx16' else '<h', d, p)[0] if n == 2
                            else struct.unpack_from('<i', d, p)[0])
                p += n
            if len(args) != len(cmds[op]):
                break
            out.append((op, args))
            if op in (0x02, 0x05, 0x1D):
                break
            if op == 0x1E:
                pc = p + args[-1]
                continue
            if op in (0x04, 0x1F, 0x20):
                todo.append(p + args[-1])
            pc = p
    return out


def flags_cleared(rom):
    """Flags any BW script clears (FlagReset with a constant flag), on any branch."""
    out = set()
    for d in rom.narc('a/0/5/7'):
        try:
            es = entries(d)
        except Exception:
            continue
        if len(es) > 3000:
            es = es[:3000]
        for op, args in every_instruction(d, es):
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
    scrolled = False      # the last thing was BW scroll code (its line break follows)
    while i < len(codes):
        c = codes[i]
        if c == 0xFFFF:
            break
        if c == 0xFFFE:
            if scrolled:                       # BW's scroll code comes just before its line break: one \l
                scrolled = False
            elif not out.endswith('\\p'):      # BW puts a line break after a page break; ours doesn't need one
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
                line += 1
                scrolled = True
            elif buffers and 0x0100 <= code < 0x0300 and n:
                out += '{STR_VAR_%d}' % min(3, codes[i + 3] + 1)   # word buffer (name, item, number...)
            elif code in (0x0100, 0x0101):      # trainer (player) name
                out += '{PLAYER}'
            i += 2 + n
        elif 0xD800 <= c <= 0xDFFF:
            pass
        else:
            scrolled = False
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
    return fit_text(out)


# --- fitting BW's lines into our text boxes ---
# BW's lines are laid out for the DS's wider box. A page with a line wider than ours (field message
# box and battle box: FONT_NORMAL, 208 pixels to leave room for the "more" arrow) is wrapped again:
# first line break \n, then \l (scroll), like ours.

TEXT_WIDTH = 208
TOKEN_WIDTH = {'PLAYER': 42, 'STR_VAR_1': 60, 'STR_VAR_2': 60, 'STR_VAR_3': 60}
_widths = None


def _char_widths():
    """{character: pixel width} from charmap.txt and FONT_NORMAL's widths (src/fonts.c)."""
    global _widths
    if _widths is None:
        import re as _re
        here = os.path.dirname(os.path.abspath(__file__))
        root = os.path.normpath(os.path.join(here, '..', '..', '..'))
        table = _re.search(r'gFontNormalLatinGlyphWidths\[\] = \{(.*?)\};',
                           open(os.path.join(root, 'src/fonts.c'), encoding='utf-8').read(), _re.S).group(1)
        widths = [int(x) for x in _re.findall(r'\b\d+\b', table)]
        _widths = {}
        for line in open(os.path.join(root, 'charmap.txt'), encoding='utf-8'):
            m = _re.match(r"^'(.+)'\s*=\s*([0-9A-Fa-f]{2})\s*$", line.strip())
            if m and len(m.group(1)) == 1 and m.group(1) not in _widths:
                code = int(m.group(2), 16)
                if code < len(widths):
                    _widths[m.group(1)] = widths[code]
    return _widths


def text_width(line):
    w, i, cw = 0, 0, _char_widths()
    while i < len(line):
        if line[i] == '{':
            j = line.index('}', i)
            w += TOKEN_WIDTH.get(line[i + 1:j], 0)
            i = j + 1
            continue
        w += cw.get(line[i], 6)
        i += 1
    return w


def fit_text(s):
    pages = s.split('\\p')
    for k, page in enumerate(pages):
        lines = page.replace('\\l', '\\n').split('\\n')
        if all(text_width(l) <= TEXT_WIDTH for l in lines):
            continue
        out, cur = [], ''
        for word in ' '.join(lines).split():
            trial = word if not cur else cur + ' ' + word
            if cur and text_width(trial) > TEXT_WIDTH:
                out.append(cur)
                cur = word
            else:
                cur = trial
        out.append(cur)
        pages[k] = out[0] + ''.join(('\\n' if n == 1 else '\\l') + l for n, l in enumerate(out[1:], 1))
    return '\\p'.join(pages)
