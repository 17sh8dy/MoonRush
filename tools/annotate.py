"""Disassemble a function from SMO 1.0.0 and name every `bl` target (demangled) — for reverse-engineering notes.
Usage: python annotate.py <symbol-substring> [maxlines]"""
import re, subprocess, sys
from smo_disasm import symbols, disasm
CXXFILT = r'D:\devkitPro\devkitA64\bin\aarch64-none-elf-c++filt.exe'

def demangle(names):
    r = subprocess.run([CXXFILT], input='\n'.join(names), capture_output=True, text=True)
    return r.stdout.splitlines()

def main():
    syms = symbols()
    by_off = {}
    for k, (off, size, st) in syms.items():
        by_off.setdefault(off, k)
    offs = sorted(by_off)
    import bisect
    names = list(by_off.values())
    dem = dict(zip(names, demangle(names)))
    q = sys.argv[1]
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 10**9
    hits = [(k, v) for k, v in syms.items() if q in k]
    for k, (off, size, st) in hits[:1]:
        print(f'== {dem.get(k, k)} @0x{off:x} size {size} {st}')
        for line in disasm(off, size).splitlines()[:limit]:
            m = re.search(r'\sbl?\s+0x([0-9a-f]+)', line)
            note = ''
            if m and re.search(r'\sbl\s', line):
                t = int(m.group(1), 16)
                n = by_off.get(t)
                note = '   ; ' + (dem.get(n, n)[:110] if n else f'sub_{t:x}')
            print(line.rstrip() + note)
main()
