#!/usr/bin/env python3
"""Pokémon World web build: turn an assembled GBA data object into WebAssembly assembly.

The game's maps, scripts, battle scripts and songs are written in GNU assembler.
Browsers can't use that directly, so the web build assembles them with GNU as
(32-bit little-endian, like the GBA) and this tool rewrites the resulting ELF
object as LLVM WebAssembly assembly: the same bytes, the same symbol names, and
every 32-bit address in the data turned back into a relocation, so the
WebAssembly linker fills in the real addresses.

Pointers to C functions (the specials table, for example) become function table
entries; their signatures come from the compiled C objects (see --functypes,
written by wasm_functypes.py).

usage: elf2wasm.py input.o output.s [--functypes file]
"""
import struct
import sys

SHT_PROGBITS, SHT_SYMTAB, SHT_NOBITS, SHT_REL, SHT_RELA = 1, 2, 8, 9, 4
SHF_ALLOC = 0x2
SHN_UNDEF, SHN_ABS, SHN_COMMON = 0, 0xFFF1, 0xFFF2
STB_LOCAL, STB_GLOBAL, STB_WEAK = 0, 1, 2
STT_SECTION, STT_FILE = 3, 4
# 32-bit absolute relocations: R_386_32 (x86 as --32), R_ARM_ABS32 (arm-none-eabi-as)
ABS32 = {(3, 1), (40, 2)}


class Elf:
    def __init__(self, data):
        self.d = data
        assert data[:4] == b"\x7fELF" and data[4] == 1 and data[5] == 1, "need a 32-bit little-endian ELF"
        (self.type, self.machine, _, _, _, shoff, _, _, _, _, shentsize, shnum, shstrndx) = \
            struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        self.sections = []
        for i in range(shnum):
            f = struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize)
            self.sections.append(dict(name_off=f[0], type=f[1], flags=f[2], offset=f[4], size=f[5],
                                      link=f[6], info=f[7], align=f[8], entsize=f[9], index=i))
        strtab = self.sections[shstrndx]
        for s in self.sections:
            s["name"] = self.cstr(strtab["offset"] + s["name_off"])
        self.symbols = []
        for s in self.sections:
            if s["type"] == SHT_SYMTAB:
                names = self.sections[s["link"]]
                for j in range(s["size"] // 16):
                    name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", data, s["offset"] + j * 16)
                    self.symbols.append(dict(name=self.cstr(names["offset"] + name), value=value, size=size,
                                             bind=info >> 4, type=info & 0xF, shndx=shndx, index=j))

    def cstr(self, off):
        end = self.d.index(b"\0", off)
        return self.d[off:end].decode("utf-8", "replace")

    def relocs(self, sec_index):
        out = {}
        for s in self.sections:
            if s["type"] in (SHT_REL, SHT_RELA) and s["info"] == sec_index:
                ent = 8 if s["type"] == SHT_REL else 12
                for j in range(s["size"] // ent):
                    if ent == 8:
                        off, info = struct.unpack_from("<II", self.d, s["offset"] + j * ent)
                        addend = None
                    else:
                        off, info, addend = struct.unpack_from("<IIi", self.d, s["offset"] + j * ent)
                    rtype, sym = info & 0xFF, info >> 8
                    if (self.machine, rtype) not in ABS32:
                        sys.exit(f"elf2wasm: unsupported relocation type {rtype} at {off:#x}")
                    out[off] = (sym, addend)
        return out


def load_functypes(path):
    types = {}
    if path:
        for line in open(path):
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 3:
                types[parts[0]] = (parts[1], parts[2])
    return types


def asm_name(name):
    # Symbol names from the decomp are plain C identifiers; quote anything else.
    return name if name.replace("_", "a").replace(".", "a").replace("$", "a").isalnum() else f'"{name}"'


def convert(path_in, path_out, functypes):
    elf = Elf(open(path_in, "rb").read())
    out = []
    used_funcs = set()
    tag = "".join(c if c.isalnum() else "_" for c in path_in)

    def sec_label(i):
        return f"__pw_sec_{tag}_{i}"

    for sec in elf.sections:
        if not (sec["flags"] & SHF_ALLOC) or sec["type"] not in (SHT_PROGBITS, SHT_NOBITS) or sec["size"] == 0:
            continue
        i = sec["index"]
        nobits = sec["type"] == SHT_NOBITS
        kind = ".bss" if nobits else ".data"
        out.append(f'\t.section {kind}.pw_{tag}_{i},"",@')
        align = max(sec["align"], 1)
        out.append(f"\t.p2align {align.bit_length() - 1}")
        # Every symbol defined in this section, by offset.
        syms = [s for s in elf.symbols if s["shndx"] == i and s["type"] not in (STT_SECTION, STT_FILE) and s["name"]]
        offsets = sorted({s["value"] for s in syms} | {0, sec["size"]})
        size_at = {o: offsets[k + 1] - o for k, o in enumerate(offsets[:-1])}
        by_off = {}
        for s in syms:
            by_off.setdefault(s["value"], []).append(s)
        out.append(f"\t.type {sec_label(i)},@object")
        out.append(f"\t.size {sec_label(i)}, {sec['size']}")
        out.append(f"{sec_label(i)}:")
        relocs = {} if nobits else elf.relocs(i)
        data = b"" if nobits else elf.d[sec["offset"]:sec["offset"] + sec["size"]]
        pos = 0
        pending = []

        def flush():
            if pending:
                for k in range(0, len(pending), 32):
                    out.append("\t.byte " + ",".join(str(b) for b in pending[k:k + 32]))
                pending.clear()

        while pos < sec["size"]:
            if pos in by_off:
                flush()
                for s in by_off[pos]:
                    n = asm_name(s["name"])
                    if s["bind"] in (STB_GLOBAL, STB_WEAK):
                        out.append(f"\t.globl {n}" if s["bind"] == STB_GLOBAL else f"\t.weak {n}")
                    out.append(f"\t.type {n},@object")
                    out.append(f"\t.size {n}, {size_at.get(pos, 0)}")
                    out.append(f"{n}:")
            if nobits:
                nxt = min([o for o in offsets if o > pos] + [sec["size"]])
                out.append(f"\t.skip {nxt - pos}")
                pos = nxt
                continue
            if pos in relocs:
                flush()
                sym_index, addend = relocs[pos]
                if addend is None:
                    addend = struct.unpack_from("<i", data, pos)[0]
                target = elf.symbols[sym_index]
                if target["type"] == STT_SECTION:
                    expr = sec_label(target["shndx"])
                    expr += f"+{addend}" if addend else ""
                elif target["shndx"] == SHN_UNDEF and target["name"] in functypes:
                    if addend:
                        sys.exit(f"elf2wasm: function pointer {target['name']}+{addend} in {path_in}")
                    used_funcs.add(target["name"])
                    expr = asm_name(target["name"])
                else:
                    expr = asm_name(target["name"]) + (f"+{addend}" if addend > 0 else f"{addend}" if addend < 0 else "")
                out.append(f"\t.int32 {expr}")
                pos += 4
                continue
            pending.append(data[pos])
            pos += 1
        flush()
        out.append("")
    header = [f"# Generated by tools/web/elf2wasm.py from {path_in}"]
    for f in sorted(used_funcs):
        params, results = functypes[f]
        header.append(f"\t.functype {asm_name(f)} ({params}) -> ({results})")
    open(path_out, "w").write("\n".join(header + out) + "\n")


if __name__ == "__main__":
    args = sys.argv[1:]
    ft = None
    if "--functypes" in args:
        k = args.index("--functypes")
        ft = args[k + 1]
        del args[k:k + 2]
    convert(args[0], args[1], load_functypes(ft))
