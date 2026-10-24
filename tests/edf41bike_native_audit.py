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
    # The derived CarBase setup resets every proxy to dynamic mode 4 after
    # VehicleBase's initial mode 1. An omitted gun row therefore stays dynamic.
    require(relative(data,0x6640A9)==0x6E40B0,'CarBase all-proxy reset differs')
    require(data[0x6E40F0:0x6E40F5].hex()=='ba04000000','CarBase reset must select dynamic mode 4')
    require(relative(data,0x6E40F8)==0x6ED480,'CarBase reset no longer sets each proxy mode')
    require(data[0x664279:0x66427E].hex()=='83fb017529','simulation row value-1 branch differs')
    require(data[0x66428F:0x664292].hex()=='8d5301','simulation row 1 must select native mode 2')
    require(relative(data,0x664292)==0x6ED480,'explicit kinematic mode write differs')
    require(relative(data,0x6642A2)==0x6E94E0,'simulation row 0 restore-default path differs')
    require(data[0x6E972A:0x6E9730].hex()=='8b8ec8000000','row 0 must restore constructor default')
    # The hover (2026-10-21): CarBase adds every proxy constraint once all are dynamic (0x6E4126 -> 0x6E4A70 ->
    # 0x11C11A0). Row 0 removes the proxy's joint to its parent (0x6E9725 -> 0x6EC840 -> 0x11C1180), recursively over
    # its children (r8b 1 at 0x664299); row 1 only makes it keyframed (0x6ED50E motion type 1) and keeps the joint,
    # which then holds the dynamic body to a body of infinite mass (pylib/edf41bike.py pinned).
    require(relative(data,0x6E4126)==0x6E4A70 and relative(data,0x6E4B1C)==0x11C11A0,'CarBase must add every proxy constraint')
    require(data[0x664299:0x66429C].hex()=='41b001','row 0 must restore the whole subtree')
    require(relative(data,0x6E9725)==0x6EC840 and relative(data,0x6EC85D)==0x11C1180,'row 0 must remove the joint to the parent')
    require(data[0x6ED50E:0x6ED513].hex()=='ba01000000' and relative(data,0x6ED513)==0x11B1960,'mode 2 must be keyframed')
    # The gun gate (2026-10-21): Car setup builds guns only when seat 0 owns one (0x65B14B loads the seat array,
    # 0x65B152 seat 0's holder count +0xD8, 0x65B15A jbe past the loop to 0x65B3EF); the loop's only SetWeaponObject is
    # 0x65B37E with seat 0's Aim. Both the script / placement path (0x633280) and the mission path (0x926A7) reach
    # it through slot 46, so a seat-1 gun was never built anywhere.
    require(0x65B15A+6+struct.unpack_from('<i',data,0x65B15C)[0]==0x65B3EF,'the gate skips the whole gun loop')
    require(data[0x65B366:0x65B374].hex()=='498b97080600004881c2e0000000' and relative(data,0x65B37E)==0x633330,
            'the gated loop builds with seat 0 Aim')
    # In the loop (over vehicle_weapon_setting, 0x65B1B2 its name) a row's seat [1] in ebx is tested at 0x65B31B: another
    # seat jumps to the next row (0x65B3A6) before the build; seat 0 builds at 0x65B37E.
    require(data[0x65B1B9+0x118286F:0x65B1B9+0x118286F+44].decode('utf-16le')=='vehicle_weapon_setting','the loop walks vehicle_weapon_setting')
    require(data[0x65B31B:0x65B323].hex()=='85db0f8583000000' and 0x65B323+0x83==0x65B3A6,'a row of another seat is skipped')
    require(table(0x17E01B0,46)==0x65A910 and data[0x6332BE:0x6332C4].hex()=='ff9070010000'
            and data[0x926B8:0x926BE].hex()=='ff9070010000','every spawn path runs Car setup slot 46')
    # car_base_fall_out's seats index the seat array unchecked (0x673F41: seat * 0x340 + [v+0x608], then +0x268).
    require(data[0x673F41:0x673F55].hex()=='496316488bcf4869f2400300004803b708060000'
            and data[0x673F5A:0x673F61].hex()=='488b9668020000','fall-out seats are not bounds-checked')
    # car_base_wheel's driven / steered columns (2026-10-25, pylib/edf41bike.py wheel_flags): [10] != 0 is the wheel's
    # +0x64, [11] != -1 its +0x65, and [12] is read (into +0x70[[11]]) only when +0x65 is set.
    require(data[0x679CFC:0x679D06].hex()=='c785d00000000a000000' and data[0x679D6F:0x679D77].hex()=='85c00f95c0884664',
            'wheel [10] must be the +0x64 flag')
    require(data[0x679DB6:0x679DC0].hex()=='c785e00000000b000000' and data[0x679E29:0x679E32].hex()=='83f8ff0f95c0884665',
            'wheel [11] != -1 must be the +0x65 flag')
    require(data[0x679E85:0x679E8F].hex()=='807e65000f84a3010000' and data[0x679F74:0x679F7E].hex()=='c785000100000c000000'
            and data[0x67A004:0x67A00A].hex()=='f30f11448670','wheel [12] is read only for a steered wheel')
    # Vehicle_Car requests BodyRecoil variant 0, unlike the old bike AimRecoil.
    # There is no null guard between that lookup and the recoil payload call.
    require(relative(data,0x65A750)==0x62EA10,'Car base FireRecoil path differs')
    require(data[0x65A76B:0x65A76D].hex()=='33d2','Car callback must request BodyRecoil variant 0')
    require(relative(data,0x65A76D)==0x5FA150,'Car recoil variant getter differs')
    require(data[0x5FA150:0x5FA161].hex()=='83fa02740939117505488b4110c333c0c3',
            'wrong recoil variant must return null')
    require(relative(data,0x65A83A)==0x5F9FB0,'Car recoil payload consumer differs')
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
    # VehicleBike's vehicle_tire row (2026-10-25, wheel_flags): [8] -> +0x64 (preset 1 for the first two tires),
    # [9] -> +0xA8 (default 1), [10] -> +0x68 (default 0.25).
    require(data[0x2CBDA7:0x2CBDAE].hex()=='c681a800000001' and data[0x2CBDF8:0x2CBE03].hex()=='c6416401c741680000803e',
            'source tire record defaults differ')
    require(data[0x2CBF24:0x2CBF31].hex()=='4883f9027307c644106401eb05','source preset of the first two tires differs')
    require(data[0x2CC7B1:0x2CC7BD].hex()=='44396408680f95c088443764' and
            data[0x2CC7D5:0x2CC7E4].hex()=='44396408740f95c0888437a8000000' and
            data[0x2CC7FC:0x2CC807].hex()=='8b8c0880000000894c3768','source tire [8] [9] [10] reads differ')
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
