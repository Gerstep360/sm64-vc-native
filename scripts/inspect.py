"""Read-only PE/ROM identity checks; no guessed offsets or asset extraction."""
import argparse, hashlib, json, struct
from pathlib import Path

def inspect(exe, rom):
    data = Path(exe).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe+4] == b'PE\0\0'
    machine, sections = struct.unpack_from('<HH', data, pe+4)
    size = struct.unpack_from('<H', data, pe+20)[0]
    opt = pe+24
    base = struct.unpack_from('<I', data, opt+28)[0]
    def at(va, count):
        rva = va-base
        for i in range(sections):
            s = opt+size+40*i
            vs, addr, rawsize, raw = struct.unpack_from('<IIII', data, s+8)
            if addr <= rva and rva+count <= addr+rawsize:
                return data[raw+rva-addr:raw+rva-addr+count]
        raise ValueError(f'VA {va:x} not file backed')
    # Exact detector from pinned Plugin-SDK shared/GameVersion.cpp, GTAVC branch.
    detected = 'UNKNOWN'
    for va, name in [(0x667bf0,'GAME_10EN'),(0x667c40,'GAME_11EN'),(0x666ba0,'GAME_STEAM')]:
        if at(va,4) == bytes.fromhex('5589e553'):
            detected = name
            break
    hooks = {hex(va): at(va,5).hex() for va in [0x4a5da0,0x4a604a,0x4a5b8e,
        0x4a46f1,0x50dbfb,0x4a64d0,0x4a613d,0x65cdfb,0x65d1d8,0x65d856,
        0x65e2a3,0x65e680,0x65ed10,0x65ef1d]}
    hooks['water_5c2c80'] = at(0x5c2c80,8).hex()
    for va in (0x525b20,0x4fd9f0,0x4bbee0,0x534890,0x5345a0):hooks[f'function_{va:x}']=at(va,12).hex()
    r = Path(rom).read_bytes()
    result = dict(exe=str(exe), exe_sha256=hashlib.sha256(data).hexdigest(),
        machine=hex(machine), image_base=hex(base), game_version=detected,
        hook_bytes=hooks, rom=str(rom), rom_size=len(r), rom_sha1=hashlib.sha1(r).hexdigest())
    result['accepted'] = (machine == 0x14c and base == 0x400000 and detected == 'GAME_10EN'
        and result['exe_sha256'] == '04e4db72629eaa786fdd182ac224f4fd6d68806f5f4fe5c1fb5f756dc4da11d7'
        and len(r) == 8388608 and result['rom_sha1'] == '9bef1128717f958171a4afac3ed78ee2bb4e86ce')
    return result

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('exe'); p.add_argument('rom'); p.add_argument('--output')
    a = p.parse_args()
    result = inspect(a.exe,a.rom)
    out = json.dumps(result,indent=2)
    print(out)
    if a.output: Path(a.output).write_text(out+'\n',encoding='utf-8')
    raise SystemExit(0 if result['accepted'] else 1)
