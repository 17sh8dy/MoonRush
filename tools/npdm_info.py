"""Summarise a main.npdm: title ID, allowed syscalls, services, fs permissions. Usage: npdm_info.py a.npdm [b.npdm ...]"""
import struct, sys

def parse(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'META', path
    aci_off, aci_size, acid_off, acid_size = struct.unpack_from('<IIII', d, 0x70)
    a = d[aci_off:aci_off + aci_size]
    assert a[:4] == b'ACI0'
    tid = struct.unpack_from('<Q', a, 0x10)[0]
    fah_o, fah_s, sac_o, sac_s, kc_o, kc_s = struct.unpack_from('<IIIIII', a, 0x20)
    kc = a[kc_o:kc_o + kc_s]
    svcs = set()
    for i in range(0, len(kc), 4):
        v = struct.unpack_from('<I', kc, i)[0]
        if v & 0x1f == 0x0f:
            idx = v >> 29; mask = (v >> 5) & 0xffffff
            for b in range(24):
                if mask >> b & 1: svcs.add(idx * 24 + b)
    sac = a[sac_o:sac_o + sac_s]; services = []; i = 0
    while i < len(sac):
        c = sac[i]; n = (c & 7) + 1
        services.append(sac[i + 1:i + 1 + n].decode()); i += 1 + n
    fah_perm = struct.unpack_from('<Q', a, fah_o + 4)[0] if fah_s >= 12 else None
    return {'title': f'{tid:016x}', 'svcs': svcs, 'services': sorted(services), 'fs': fah_perm, 'raw': d}

if __name__ == '__main__':
    infos = [(p, parse(p)) for p in sys.argv[1:]]
    for p, i in infos:
        print(f'{p}\n  title {i["title"]}  svcs {len(i["svcs"])}  services {len(i["services"])}  fs 0x{(i["fs"] or 0):x}')
    if len(infos) > 1:
        base = infos[0][1]
        for p, i in infos[1:]:
            print(f'vs first: +svcs {sorted(hex(s) for s in i["svcs"] - base["svcs"])}  -svcs {sorted(hex(s) for s in base["svcs"] - i["svcs"])}')
            print(f'          +services {sorted(set(i["services"]) - set(base["services"]))}  -services {sorted(set(base["services"]) - set(i["services"]))}')
