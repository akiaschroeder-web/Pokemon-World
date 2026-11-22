#!/usr/bin/env python3
"""Pokémon World web build, debugging aid: show the WebAssembly code around a crash.

Browsers report crashes as "wasm-function[N]:0xOFFSET". This prints the function's
name and the instructions just before that offset, with the names of called functions
(the build links with --profiling-funcs so names are kept).

usage: wasm_where.py index.wasm 0xOFFSET [count]
       wasm_where.py index.wasm --table N...   (name the functions behind function pointers)
"""
import struct
import sys


def uleb(b, p):
    r = s = 0
    while True:
        c = b[p]
        p += 1
        r |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return r, p


def sleb(b, p):
    r = s = 0
    while True:
        c = b[p]
        p += 1
        r |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            if c & 0x40:
                r -= 1 << s
            return r, p


def main():
    b = open(sys.argv[1], "rb").read()
    target = int(sys.argv[2], 0) if sys.argv[2] != '--table' else 0
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    p = 8
    names, imports, code, elems = {}, 0, None, None
    while p < len(b):
        sid = b[p]
        p += 1
        size, p = uleb(b, p)
        start = p
        if sid == 2:
            q = start
            n, q = uleb(b, q)
            for _ in range(n):
                l, q = uleb(b, q); mod = b[q:q + l]; q += l
                l, q = uleb(b, q); fld = b[q:q + l].decode(); q += l
                kind = b[q]; q += 1
                if kind == 0:
                    _, q = uleb(b, q)
                    names[imports] = "import:" + fld
                    imports += 1
                elif kind == 1:
                    q += 1; fl, q = uleb(b, q); _, q = uleb(b, q)
                    if fl & 1: _, q = uleb(b, q)
                elif kind == 2:
                    fl, q = uleb(b, q); _, q = uleb(b, q)
                    if fl & 1: _, q = uleb(b, q)
                elif kind == 3:
                    q += 2
        elif sid == 10:
            code = start
        elif sid == 9:
            elems = start
        elif sid == 0:
            l, q = uleb(b, start)
            if b[q:q + l] == b"name":
                q += l
                while q < start + size:
                    sub = b[q]; q += 1
                    ssize, q = uleb(b, q)
                    nxt = q + ssize
                    if sub == 1:
                        n, q = uleb(b, q)
                        for _ in range(n):
                            idx, q = uleb(b, q)
                            l, q = uleb(b, q)
                            names[idx] = b[q:q + l].decode("utf-8", "replace")
                            q += l
                    q = nxt
        p = start + size
    if sys.argv[2] == "--table":
        # Function pointers (table indices) -> function names. Emscripten emits one active
        # element segment starting at table index 1.
        q = elems
        n, q = uleb(b, q)
        table = {}
        for _ in range(n):
            flags, q = uleb(b, q)
            assert flags == 0, flags
            assert b[q] == 0x41
            off, q = sleb(b, q + 1)
            q += 1  # end
            cnt, q = uleb(b, q)
            for k in range(cnt):
                f, q = uleb(b, q)
                table[off + k] = f
        for a in sys.argv[3:]:
            f = table.get(int(a))
            print(f"{a}: {names.get(f, f)}")
        return
    # find the function containing target
    q = code
    n, q = uleb(b, q)
    for i in range(n):
        fsize, q = uleb(b, q)
        fstart, fend = q, q + fsize
        if fstart <= target < fend:
            fidx = imports + i
            print(f"function {fidx}: {names.get(fidx, '?')}  ({fstart:#x}-{fend:#x})")
            decode(b, fstart, fend, target, count, names)
            return
        q = fend
    print("offset not in code")


MEM = set(range(0x28, 0x3F))  # loads/stores with memarg


def decode(b, p, end, target, count, names):
    nloc, p = uleb(b, p)
    for _ in range(nloc):
        _, p = uleb(b, p)
        p += 1
    out = []
    while p < end:
        at = p
        op = b[p]
        p += 1
        txt = f"{op:#04x}"
        if op in (0x02, 0x03, 0x04):
            bt = b[p]
            if bt == 0x40 or bt >= 0x6F:
                p += 1
            else:
                _, p = sleb(b, p)
            txt = {0x02: "block", 0x03: "loop", 0x04: "if"}[op]
        elif op in (0x0C, 0x0D):
            v, p = uleb(b, p); txt = ("br " if op == 0x0C else "br_if ") + str(v)
        elif op == 0x0E:
            n, p = uleb(b, p)
            for _ in range(n + 1):
                _, p = uleb(b, p)
            txt = "br_table"
        elif op == 0x10:
            v, p = uleb(b, p); txt = f"call {names.get(v, v)}"
        elif op == 0x11:
            t, p = uleb(b, p); _, p = uleb(b, p); txt = f"call_indirect (type {t})"
        elif op in (0x20, 0x21, 0x22, 0x23, 0x24):
            v, p = uleb(b, p); txt = ["local.get", "local.set", "local.tee", "global.get", "global.set"][op - 0x20] + f" {v}"
        elif op in MEM:
            a, p = uleb(b, p); o, p = uleb(b, p); txt = f"mem{op:#x} offset={o}"
        elif op in (0x3F, 0x40):
            p += 1; txt = "memory.size/grow"
        elif op == 0x41:
            v, p = sleb(b, p); txt = f"i32.const {v}"
        elif op == 0x42:
            v, p = sleb(b, p); txt = f"i64.const {v}"
        elif op == 0x43:
            p += 4; txt = "f32.const"
        elif op == 0x44:
            p += 8; txt = "f64.const"
        elif op == 0x1C:
            n, p = uleb(b, p); p += n; txt = "select t"
        elif op == 0xFC:
            sub, p = uleb(b, p)
            if sub in (8,):
                _, p = uleb(b, p); p += 1
            elif sub in (9,):
                _, p = uleb(b, p)
            elif sub in (10,):
                p += 2
            elif sub in (11,):
                p += 1
            elif sub in (12, 14):
                _, p = uleb(b, p); _, p = uleb(b, p)
            elif sub in (13, 15, 16, 17):
                _, p = uleb(b, p)
            txt = f"0xfc {sub}"
        elif op == 0x05:
            txt = "else"
        elif op == 0x0B:
            txt = "end"
        elif op == 0x0F:
            txt = "return"
        elif op == 0x00:
            txt = "unreachable"
        out.append((at, txt))
        if at >= target:
            break
    for at, txt in out[-count:]:
        mark = " <==" if at >= target else ""
        print(f"  {at:#x}: {txt}{mark}")


if __name__ == "__main__":
    main()
