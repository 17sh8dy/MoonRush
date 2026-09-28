"""Look up SMO 1.0.0 functions by symbol and disassemble them from the real game code.

Usage: python smo_disasm.py <symbol-substring> [...]      (reads ../../_reference)
Needs: _reference/smo100/exefs/main (extracted with hactool), the OdysseyDecomp clone,
       and devkitA64's objdump.
"""
import os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REF = os.path.normpath(os.path.join(HERE, '..', '..', '_reference'))
MAIN_NSO = os.path.join(REF, 'smo100', 'exefs', 'main')
FLAT = os.path.join(REF, 'smo100', 'main.flat')
FILE_LIST = os.path.join(REF, 'OdysseyDecomp', 'data', 'file_list.yml')
OBJDUMP = r'D:\devkitPro\devkitA64\bin\aarch64-none-elf-objdump.exe'
BUILD_ID_100 = '3ca12dfaaf9c82da064d1698df79cda1'


def lz4_block(src):
    dst = bytearray(); i = 0
    while i < len(src):
        tok = src[i]; i += 1
        n = tok >> 4
        if n == 15:
            while True:
                b = src[i]; i += 1; n += b
                if b != 255: break
        dst += src[i:i + n]; i += n
        if i >= len(src): break
        off = src[i] | (src[i + 1] << 8); i += 2
        m = tok & 15
        if m == 15:
            while True:
                b = src[i]; i += 1; m += b
                if b != 255: break
        m += 4
        s = len(dst) - off
        for k in range(m): dst.append(dst[s + k])
    return bytes(dst)


def flat_image():
    """main NSO -> flat memory image (segments at their load offsets)."""
    if os.path.exists(FLAT):
        return open(FLAT, 'rb').read()
    d = open(MAIN_NSO, 'rb').read()
    assert d[:4] == b'NSO0'
    assert d[0x40:0x50].hex() == BUILD_ID_100, 'not SMO 1.0.0'
    flags = struct.unpack_from('<I', d, 0xC)[0]
    img = bytearray()
    for n in range(3):
        foff, moff, dsize = struct.unpack_from('<III', d, 0x10 + n * 0x10)
        csize = struct.unpack_from('<I', d, 0x60 + n * 4)[0]
        raw = d[foff:foff + csize]
        seg = lz4_block(raw) if flags & (1 << n) else raw
        assert len(seg) == dsize
        if len(img) < moff: img += bytes(moff - len(img))
        img[moff:moff + dsize] = seg
    open(FLAT, 'wb').write(img)
    return bytes(img)


def symbols():
    """{mangled label: (offset, size, status)} from the decomp's file list."""
    out = {}
    cur = {}
    in_labels = False
    for line in open(FILE_LIST, encoding='utf-8'):
        s = line.strip()
        if s.startswith('- offset:'):
            cur = {'offset': int(s.split(':')[1], 16), 'labels': []}; in_labels = False
        elif s.startswith('size:'):
            cur['size'] = int(s.split(':')[1])
        elif s.startswith('label:'):
            v = s[len('label:'):].strip()
            if v: cur['labels'].append(v)
            else: in_labels = True
        elif in_labels and s.startswith('- '):
            cur['labels'].append(s[2:].strip())
        elif s.startswith('status:'):
            in_labels = False
            for l in cur.get('labels', []):
                out[l] = (cur['offset'], cur.get('size', 0), s.split(':')[1].strip())
    return out


def disasm(off, size):
    flat_image()
    r = subprocess.run([OBJDUMP, '-D', '-b', 'binary', '-m', 'aarch64',
                        f'--start-address=0x{off:x}', f'--stop-address=0x{off + size:x}', FLAT],
                       capture_output=True, text=True)
    return '\n'.join(l for l in r.stdout.splitlines() if re.match(r'\s+[0-9a-f]+:', l))


if __name__ == '__main__':
    syms = symbols()
    for q in sys.argv[1:]:
        limit = None
        if '@' in q:
            q, limit = q.split('@'); limit = int(limit, 0)
        hits = [(k, v) for k, v in syms.items() if q in k]
        for k, (off, size, st) in hits[:6]:
            print(f'== {k}  @0x{off:x}  size {size}  {st}')
            print(disasm(off, min(size, limit or size)))
        if not hits:
            print('no match:', q)
