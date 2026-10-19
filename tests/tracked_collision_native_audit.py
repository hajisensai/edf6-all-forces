"""Read-only native guard for tracked collision modes, pose owners and contact filtering.

Usage: python tests/tracked_collision_native_audit.py [EDF.dll]
No process is opened and no game file is written. Missing game returns skip 77.
"""
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'pylib'))
import gamedir


def require(ok,message):
    if not ok:raise ValueError(message)


def main():
    path=Path(sys.argv[1]) if len(sys.argv)>1 else Path(gamedir.find_or_dev())/'EDF.dll'
    if not path.is_file():
        print('SKIP tracked collision native audit: EDF.dll unavailable')
        return 77
    import pefile
    pe=pefile.PE(str(path),fast_load=True)
    require(pe.FILE_HEADER.TimeDateStamp==0x678CCB46,'unsupported EDF6 build')
    data=pe.get_memory_mapped_image()
    def code(at,wanted):
        expected=bytes.fromhex(wanted)
        require(data[at:at+len(expected)]==expected,f'{at:#x}: native collision contract differs')
    def call(at,target,opcode=0xe8):
        require(data[at]==opcode and at+5+struct.unpack_from('<i',data,at+1)[0]==target,
                f'{at:#x}: native call target differs')
    # CarBase unconditionally resets all proxies to 4, then applies the named
    # simulation table. Value 0 restores component+C8 (default 1); value 1 is 2.
    call(0x6640A9,0x6E40B0)
    code(0x6E40F0,'ba04000000488bcbe883930000')
    code(0x664275,'85db742083fb017529')
    code(0x66428F,'8d5301');call(0x664292,0x6ED480)
    call(0x6642A2,0x6E94E0)
    code(0x6E972A,'8b8ec800000085c9741683e901740a83f90175168d5101eb09ba01000000eb0233d2')
    call(0x6E974F,0x6ED480)
    # Wheel row[8] is the contact/rolling radius, not a guessed donor value.
    code(0x679B80,'c785b000000008000000')
    code(0x679BFA,'f30f11465c')
    code(0x679E53,'f30f104e5c410f28c3f30f5ec1f30f1186d4000000')
    code(0x6787BC,'f3430f1054025c')
    code(0x67A45B,'f30f105e5c');call(0x67A468,0x11A6570)
    # AFR establishes the proxy pointer/index in each model-bone name record;
    # removing this unique owner makes simulation_node lookup return -1.
    code(0x6E7E40,'488b433048894808488b43308b4c243c894810')
    code(0x6EA644,'488b43308b4010')
    # Live mode 1 does not write AFR. Only mode 4 reaches the physics-to-bone loop.
    code(0x6EDD3D,'83bb9c000000040f85f7010000')
    call(0x6215D6,0x6EDCA0);call(0x621654,0x1100B90)
    call(0x62168C,0x6ED800,0xE9)
    # 603's extra pose writers target main/right/left cannon indices, not
    # the two radar bones; fixing their private CAS references is sufficient.
    for at,name,store,wanted in ((0x621019,'body',0x62103A,'8986cc2a0000'),
                                  (0x621099,'cannon_main',0x6210BA,'8986d02a0000'),
                                  (0x621119,'cannon_r',0x62113A,'8986d82a0000'),
                                  (0x621199,'cannon_l',0x6211BA,'8986d42a0000')):
        require(data[at:at+3]==bytes.fromhex('488d15'),'native bone-name lookup differs')
        target=at+7+struct.unpack_from('<i',data,at+3)[0]
        require(data[target:target+2*(len(name)+1)]==name.encode('utf-16le')+bytes(2),'native pose writer names another bone')
        code(store,wanted)
    # The real wheel writer runs later than pose45. It changes world records
    # directly, so the live H half must copy G.world after this call.
    call(0x674B7F,0x66F430)
    code(0x66FC96,'450f118fb0000000410f11a7c0000000')
    code(0x670259,'410f1187e000000041c787ec0000000000803f')
    # Same group only rejects direct subsystem/dont pairs. Sibling guns/idlers
    # retain contact and therefore require physically separated positive shapes.
    base=pe.OPTIONAL_HEADER.ImageBase
    require(struct.unpack_from('<Q',data,0x18BBFC0+3*8)[0]-base==0xD93250,'group filter vtable differs')
    code(0xD932A3,'8bc833caf7c10080ff7f7527f7c20080ff7f741f')
    code(0xD932B7,'8bc8c1e90533caf7c1e00300007433c1e805c1ea0a83e01f83e21f2bc2eb15')
    code(0xD932EB,'85c0740a498b0b49890a4983c208')
    print('Tracked collision native mode reset, AFR ownership, mode4 pose and sibling-contact contracts passed')
    return 0


if __name__=='__main__':raise SystemExit(main())
