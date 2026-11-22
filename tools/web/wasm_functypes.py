#!/usr/bin/env python3
"""Pokémon World web build: list the signature of every function defined in some
WebAssembly object files, for elf2wasm.py (data tables that point at C functions).

usage: wasm_functypes.py out.txt file.o...
Each line: name<TAB>param types<TAB>result types (comma separated, e.g. "i32,i32").
"""
import sys

VALTYPES = {0x7F: "i32", 0x7E: "i64", 0x7D: "f32", 0x7C: "f64", 0x7B: "v128", 0x70: "funcref", 0x6F: "externref"}


class R:
    def __init__(self, b, pos=0, end=None):
        self.b, self.p, self.end = b, pos, len(b) if end is None else end

    def u8(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def uleb(self):
        r = s = 0
        while True:
            c = self.u8()
            r |= (c & 0x7F) << s
            s += 7
            if c < 0x80:
                return r

    def name(self):
        n = self.uleb()
        v = self.b[self.p:self.p + n].decode("utf-8", "replace")
        self.p += n
        return v


def functypes(path):
    b = open(path, "rb").read()
    if b[:4] != b"\0asm":
        return {}
    r = R(b, 8)
    types, func_types, imported = [], [], 0
    linking = None
    while r.p < len(b):
        sid = r.u8()
        size = r.uleb()
        start = r.p
        s = R(b, start, start + size)
        if sid == 1:
            for _ in range(s.uleb()):
                s.u8()  # 0x60
                params = [VALTYPES[s.u8()] for _ in range(s.uleb())]
                results = [VALTYPES[s.u8()] for _ in range(s.uleb())]
                types.append((params, results))
        elif sid == 2:
            for _ in range(s.uleb()):
                s.name(); s.name()
                kind = s.u8()
                if kind == 0:
                    func_types.append(s.uleb())
                    imported += 1
                elif kind == 1:
                    s.u8(); flags = s.uleb(); s.uleb()
                    if flags & 1: s.uleb()
                elif kind == 2:
                    flags = s.uleb(); s.uleb()
                    if flags & 1: s.uleb()
                elif kind == 3:
                    s.u8(); s.u8()
                elif kind == 4:
                    s.u8(); s.uleb()
        elif sid == 3:
            for _ in range(s.uleb()):
                func_types.append(s.uleb())
        elif sid == 0:
            if s.name() == "linking":
                linking = (s.p, start + size)
        r.p = start + size
    out = {}
    if linking is None:
        return out
    s = R(b, linking[0], linking[1])
    s.uleb()  # version
    while s.p < s.end:
        kind = s.u8()
        size = s.uleb()
        nxt = s.p + size
        if kind == 8:  # symbol table
            for _ in range(s.uleb()):
                skind = s.u8()
                flags = s.uleb()
                undefined = flags & 0x10
                if skind in (0, 2, 4, 5):
                    index = s.uleb()
                    if not undefined or flags & 0x40:
                        nm = s.name()
                        if skind == 0 and not undefined:
                            params, results = types[func_types[index]]
                            out[nm] = (",".join(params), ",".join(results))
                elif skind == 1:
                    s.name()
                    if not undefined:
                        s.uleb(); s.uleb(); s.uleb()
                elif skind == 3:
                    s.uleb()
        s.p = nxt
    return out


if __name__ == "__main__":
    allt = {}
    for f in sys.argv[2:]:
        allt.update(functypes(f))
    with open(sys.argv[1], "w") as fh:
        for k in sorted(allt):
            fh.write(f"{k}\t{allt[k][0]}\t{allt[k][1]}\n")
