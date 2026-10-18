"""Read-only audit of the real native 408 single-bone gun path. No process/game writes.

Usage: python tests/edf41bike_native_audit.py [EDF.dll] [EDF41.exe]
Absent local games skip their corresponding audit; source files are never loaded as code.
"""
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib')]
import gamedir


def require(ok: bool, why: str) -> None:
    if not ok:
        raise ValueError(why)


def relative(data: bytes, at: int, opcode: int = 0xE8) -> int:
    require(data[at] == opcode, f'{at:#x}: not expected branch opcode')
    return at + 5 + struct.unpack_from('<i', data, at + 1)[0]


def native(path: Path) -> int:
    import pefile
    pe = pefile.PE(str(path), fast_load=True)
    require(pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, 'EDF6 build is not the supported Steam profile')
    data = pe.get_memory_mapped_image()
    source = (ROOT / 'src' / 'edf41bike.cpp').read_text(encoding='utf-8')
    constants = {name: int(value,16) for name,value in re.findall(r'\b(k\w+)=0x([0-9A-Fa-f]+)',source)}
    audited = 0
    for name, body in re.findall(r'const unsigned char (k\w+)Code\[\]=\{([^}]+)\};', source):
        require(name in constants, f'{name}: signature has no named address')
        wanted = bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]+)',body))
        address = constants[name]
        require(data[address:address+len(wanted)] == wanted, f'{name}: native signature differs')
        audited += 1
    require(audited >= 7, 'native runtime signatures were not audited')
    base = pe.OPTIONAL_HEADER.ImageBase
    table = lambda rva,index: struct.unpack_from('<Q',data,rva+index*8)[0]-base
    require(table(0x17E01B0,4)==0x65A850 and table(0x17E01B0,45)==0x65A690,'Car virtual frame/pose contract')
    for vt, step in ((0x17D8A68,0x5FBDA0),(0x17D8A90,0x5FCD80)):
        require(table(vt,2)==step and table(vt,3)==0x5FACD0,'native Aim methods differ')
    require(relative(data,0x65B37E)==0x633330,'Car gun construction no longer calls SetWeaponObject')
    require(data[0x65B366:0x65B374].hex()=='498b97080600004881c2e0000000','Car construction must expose the seat0 Aim to the wrapper')
    require(data[0x5FAD1C:0x5FAD25].hex()=='493bd00f85da000000','native Aim same-matrix branch differs')
    require(relative(data,0x65A6A4)==0x6EDCA0,'native pose physics-to-bones call differs')
    require(relative(data,0x65A704)==0x1100B90,'native pose world-build call differs')
    require(relative(data,0x65A73B,0xE9)==0x6ED800,'native pose bones-to-physics tail call differs')
    require(data[0x5FBDA9:0x5FBDB5].hex()=='80b9c000000000488bf17547','native remote Aim interpolation flag differs')
    require(data[0x5FB810:0x5FB81C].hex()=='f20f1002f20f118190000000','native Aim speed must come from the source weapon setup')
    require(table(0x17E0458,7)==0x676B40 and table(0x17E0458,8)==0x66BA60,'Car native network methods differ')
    require(data[0x677065:0x67706D].hex()=='440fb6af18060000','native network send must read seat count')
    require(relative(data,0x6770B9)==0x5FB7E0,'native aim mapping admission check differs')
    require(data[0x5FB7F0:0x5FB7F5].hex()=='4883783800','native map count no longer gates outgoing aim')
    require(relative(data,0x66BC33)==0x5FAAE0,'native corresponding-seat angle receive differs')
    require(data[0x66BC1F:0x66BC30].hex()=='488b8b080600004881c1e00000004803ce','native receive must add the selected seat offset')
    require(relative(data,0x629C47)==0x62B430,'VehicleBase must build its native seats before derived setup')
    require(relative(data,0x6598DF)==0x65C540,'Car constructor base order differs')
    require(relative(data,0x65C58C)==0x629450,'GenericCarBase must initialize its VehicleBase')
    require(data[0x65C587:0x65C58C].hex()=='458d442401','proxy default mode must be animation-driven 1')
    require(relative(data,0x62990D)==0x6EB4C0,'VehicleBase must pass its default mode to native ragdoll loading')
    require(data[0x6598E5:0x6598E8].hex()=='488d05','final Car vtable assignment differs')
    require(0x6598EC+struct.unpack_from('<i',data,0x6598E8)[0]==0x17E01B0,'derived setup must use final Car vtable')
    print(f'EDF6 original 408 adapter: {audited} runtime signatures and native Car/Aim/physics contracts passed')
    return audited


def original(path: Path) -> None:
    import pefile
    pe = pefile.PE(str(path),fast_load=True)
    require(pe.FILE_HEADER.TimeDateStamp == 0x57AAB039,'EDF4.1 source version differs from the audited 408 algorithm')
    data = pe.get_memory_mapped_image()
    require(data[0x2D6C22:0x2D6C27].hex()=='493bd07550','source 408 single-gun matrix branch differs')
    require(relative(data,0x2CA5F3)==0x334410,'source dynamic physics-to-bones call differs')
    require(relative(data,0x2CA680)==0x50AED0,'source bone-world build differs')
    require(relative(data,0x2CA68C)==0x334580,'source animation-to-physical-body sync differs')
    print('EDF4.1 original 408 single-bone pose and real physics synchronization passed')


def main() -> int:
    ran = False
    dll = Path(sys.argv[1]) if len(sys.argv)>1 else Path(gamedir.find_or_dev())/'EDF.dll'
    if dll.is_file():
        native(dll)
        ran = True
    else:
        print('SKIP native 408 audit: EDF.dll not present')
    old = Path(sys.argv[2]) if len(sys.argv)>2 else Path(r'D:\Steam\steamapps\common\Earth Defense Force 4.1\EDF41.exe')
    if old.is_file():
        original(old)
        ran = True
    else:
        print('SKIP source 408 audit: EDF41.exe not present')
    return 0 if ran else 77


if __name__=='__main__':
    raise SystemExit(main())
