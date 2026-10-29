"""Research helper for finding engine code (not shipped, not needed to build).

Maps tlou-i.exe like the loader does and offers:
  strings <regex>          list matching strings with their RVAs
  xrefs <text>             code that references a string (lea reg,[rip+x])
  dis <rva> [count]        disassemble at an RVA
  refs <rva>               rip-relative instructions that reference an RVA
Requires: pefile, capstone, numpy (pip install --target <dir> ...; set PYTHONPATH).
"""
import re
import struct
import sys

import capstone
import pefile

EXE = r"C:\Program Files (x86)\Steam\steamapps\common\The Last of Us Part I\tlou-i.exe"


class Image:
    def __init__(self, path=EXE):
        pe = pefile.PE(path, fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.mem = bytearray(pe.OPTIONAL_HEADER.SizeOfImage)
        raw = open(path, "rb").read()
        self.mem[: pe.OPTIONAL_HEADER.SizeOfHeaders] = raw[: pe.OPTIONAL_HEADER.SizeOfHeaders]
        self.text = None
        for s in pe.sections:
            n = min(s.SizeOfRawData, s.Misc_VirtualSize)
            self.mem[s.VirtualAddress : s.VirtualAddress + n] = raw[s.PointerToRawData : s.PointerToRawData + n]
            if self.text is None and s.Characteristics & 0x20000000:
                self.text = (s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = False

    def strings(self, pattern, minlen=4):
        rx = re.compile(pattern.encode() if isinstance(pattern, str) else pattern)
        for m in re.finditer(rb"[\x20-\x7e]{%d,}\x00" % minlen, self.mem):
            s = m.group(0)[:-1]
            if rx.search(s):
                yield m.start(), s.decode()

    def find_string(self, text):
        needle = text.encode() + b"\x00"
        out, i = [], self.mem.find(needle)
        while i >= 0:
            if i == 0 or self.mem[i - 1] == 0:
                out.append(i)
            i = self.mem.find(needle, i + 1)
        return out

    def _disp_views(self):
        # int32 at every byte offset of .text, as four aligned views (cached).
        if not hasattr(self, "_views"):
            import numpy as np
            lo, hi = self.text
            self._views = []
            for a in range(4):
                n = (hi - lo - a) // 4
                v = np.frombuffer(bytes(self.mem[lo + a : lo + a + 4 * n]), dtype="<i4").astype(np.int64)
                pos = lo + a + 4 * np.arange(n, dtype=np.int64)
                self._views.append((v + pos, pos))
        return self._views

    def refs(self, target):
        """rip-relative instructions in .text whose operand resolves to target."""
        import numpy as np
        found = set()
        for k in (4, 5, 8):  # bytes from disp32 to instruction end: none / imm8 / imm32
            for v, pos in self._disp_views():
                for i in pos[np.nonzero(v + k == target)[0]]:
                    i = int(i)
                    for start in range(i - 4, i - 1):
                        ins = next(self.md.disasm(bytes(self.mem[start : start + 16]), start), None)
                        if ins and ins.address + ins.size == i + k and "rip" in ins.op_str:
                            found.add((start, f"{ins.mnemonic} {ins.op_str}"))
                            break
        return sorted(found)

    def refs_range(self, lo_t, hi_t):
        """rip-relative instructions in .text whose operand lies in [lo_t, hi_t)."""
        import numpy as np
        found = set()
        for k in (4, 5, 8):
            for v, pos in self._disp_views():
                t = v + k
                for i in pos[np.nonzero((t >= lo_t) & (t < hi_t))[0]]:
                    i = int(i)
                    for start in range(i - 4, i - 1):
                        ins = next(self.md.disasm(bytes(self.mem[start : start + 16]), start), None)
                        if ins and ins.address + ins.size == i + k and "rip" in ins.op_str:
                            d = int.from_bytes(self.mem[i : i + 4], "little", signed=True)
                            found.add((start, i + k + d, f"{ins.mnemonic} {ins.op_str}"))
                            break
        return sorted(found)

    def callers(self, target):
        """call rel32 / jmp rel32 instructions in .text that land on target."""
        import numpy as np
        out = []
        for v, pos in self._disp_views():
            for i in pos[np.nonzero(v + 4 == target)[0]]:
                i = int(i)
                if self.mem[i - 1] in (0xE8, 0xE9):
                    out.append(i - 1)
        return sorted(out)

    def dmenu(self):
        """Every debug-menu item: (label, item ctor, value/callback RVA, call site).

        The retail exe still builds Naughty Dog's developer menu: each item is
        `lea r8/r9,[value]; ...; lea rdx,[label]; mov rcx,..; call Item*`.
        """
        import numpy as np
        lo, hi = self.text
        out = []
        mem = self.mem
        # every `lea rdx,[rip+d]` (48 8D 15 d32)
        start = 0
        while True:
            i = mem.find(bytes([0x48, 0x8D, 0x15]), start, hi)
            if i < 0:
                break
            start = i + 1
            if i < lo:
                continue
            label = self.cstr(i + 7 + int.from_bytes(mem[i + 3 : i + 7], "little", signed=True), 120)
            if not label:
                continue
            # the call right after (within 24 bytes)
            ctor = None
            for ins in self.md.disasm(bytes(mem[i + 7 : i + 40]), i + 7):
                if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
                    ctor = int(ins.op_str, 16)
                    break
            if ctor is None:
                continue
            # value / callback: lea r8|r9,[rip+x] in the 48 bytes before
            vals = []
            for back in range(48, 0, -1):
                j = i - back
                if mem[j] == 0x4C and mem[j + 1] == 0x8D and mem[j + 2] in (0x05, 0x0D):
                    t = j + 7 + int.from_bytes(mem[j + 3 : j + 7], "little", signed=True)
                    vals.append(("r8" if mem[j + 2] == 0x05 else "r9", t))
            out.append((label, ctor, vals, i))
        return out

    def dis(self, rva, count=30):
        out = []
        for ins in self.md.disasm(bytes(self.mem[rva : rva + count * 15]), rva):
            extra = ""
            m = re.search(r"\[rip ([+-]) 0x([0-9a-f]+)\]", ins.op_str)
            if m:
                d = int(m.group(2), 16) * (1 if m.group(1) == "+" else -1)
                t = ins.address + ins.size + d
                extra = f"   ; -> 0x{t:X}"
                s = self.cstr(t)
                if s:
                    extra += f' "{s}"'
            out.append(f"0x{ins.address:X}: {ins.bytes.hex(' '):<45} {ins.mnemonic} {ins.op_str}{extra}")
            if len(out) >= count:
                break
        return "\n".join(out)

    def around(self, rva, before=60, count=40):
        """Disassembly that starts before rva at a point decoding cleanly through rva."""
        best = rva
        for back in range(before, 0, -1):
            start = rva - back
            addrs = [i.address for i in self.md.disasm(bytes(self.mem[start : rva + 16]), start)]
            if rva in addrs and len(addrs) > 3:
                best = start
                break
        return self.dis(best, count)

    def cstr(self, rva, maxlen=80):
        if rva < 0 or rva >= len(self.mem):
            return None
        end = self.mem.find(b"\x00", rva, rva + maxlen)
        if end <= rva:
            return None
        s = self.mem[rva:end]
        if all(0x20 <= c < 0x7F for c in s) and len(s) >= 3:
            return s.decode()
        return None


def main():
    img = Image()
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "strings":
        for rva, s in img.strings(args[0]):
            print(f"0x{rva:X}  {s}")
    elif cmd == "xrefs":
        for rva in img.find_string(args[0]):
            print(f"string 0x{rva:X}")
            for at, txt in img.refs(rva):
                print(f"  0x{at:X}: {txt}")
    elif cmd == "dis":
        print(img.dis(int(args[0], 16), int(args[1]) if len(args) > 1 else 30))
    elif cmd == "around":
        print(img.around(int(args[0], 16), 60, int(args[1]) if len(args) > 1 else 40))
    elif cmd == "dmenu":
        for label, ctor, vals, at in img.dmenu():
            v = " ".join(f"{r}=0x{t:X}" for r, t in vals[-2:])
            print(f"0x{at:X}  ctor=0x{ctor:X}  {v:40s}  {label}")
    elif cmd == "refs":
        for at, txt in img.refs(int(args[0], 16)):
            print(f"0x{at:X}: {txt}")


if __name__ == "__main__":
    main()
