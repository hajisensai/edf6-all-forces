"""Read-only EDF6 native audit for the M163 firing/pose/render contract.

Usage: python tests/vulcan_rotor_native_audit.py [EDF.dll]
The native game image is read as data; no game process is opened or modified.
"""
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'pylib'))
import gamedir


def require(ok: bool, why: str) -> None:
    if not ok:
        raise ValueError(why)


def call(data: bytes, address: int, target: int, opcode: int=0xE8) -> None:
    require(data[address]==opcode,f'{address:#x}: native call opcode differs')
    require(address+5+struct.unpack_from('<i',data,address+1)[0]==target,f'{address:#x}: native target differs')


def main() -> int:
    path=Path(sys.argv[1]) if len(sys.argv)>1 else Path(gamedir.find_or_dev())/'EDF.dll'
    if not path.is_file():
        print('SKIP M163 native audit: EDF.dll is unavailable')
        return 77
    import pefile
    pe=pefile.PE(str(path),fast_load=True)
    require(pe.FILE_HEADER.TimeDateStamp==0x678CCB46,'unsupported EDF6 build')
    data=pe.get_memory_mapped_image();base=pe.OPTIONAL_HEADER.ImageBase
    slot=lambda vt,i: struct.unpack_from('<Q',data,vt+i*8)[0]-base
    require(slot(0x17DC620,45)==0x621510,'603 native pose virtual changed')
    require(data[0x621649:0x621659].hex()=='488d5360488d8be00e0000e837f5ad00','603 final world-build boundary changed')
    for at,target in ((0x6215D6,0x6EDCA0),(0x6215F4,0x661C00),(0x62161C,0x661810),(0x621644,0x661810),(0x621654,0x1100B90)):
        call(data,at,target)
    call(data,0x62168C,0x6ED800,0xE9)
    require(slot(0x17DC620,3)==0x680130,'603 native rendering virtual changed')
    call(data,0x680178,0x674A10,0xE9);call(data,0x674A1C,0x630220)
    call(data,0x630230,0x6C06A0);call(data,0x6C06D7,0x1100F10)
    require(data[0x1100F10:0x1100F22].hex()=='33d24c8bc1483951207652448bca448bd249','native all-bone draw-palette loop differs')
    for at,wanted in ((0x6982A5,'41c787d40b000001000000'),(0x693A9B,'c786d40b000002000000'),
                      (0x693ABC,'4489aed40b0000'),(0x690540,'89ab44150000'),(0x69056D,'0fb640082401'),
                      (0x690611,'ff83d00b0000'),(0x6947AD,'8b83d00b0000488bcb898344150000')):
        raw=bytes.fromhex(wanted)
        require(data[at:at+len(raw)]==raw,f'{at:#x}: native fire/state/replay field differs')
    # Wheel drawing occurs after slot45 pose/world build. H must follow the
    # final G world writer, never the earlier local-pose boundary.
    require(slot(0x17DC620,5)==0x621C30,'603 render preparation virtual changed')
    call(data,0x621C39,0x680180);call(data,0x6801A5,0x674B00,0xE9)
    call(data,0x674B6B,0x630250)
    require(data[0x63028C:0x630292].hex()=='ff9068010000','native wheel phase must follow virtual slot45 pose')
    require(data[0x674B76:0x674B7C].hex()=='ff90c0010000','native wheel-cache slot56 differs')
    require(data[0x674B7C:0x674B84].hex()=='488bcfe8aca8ffff','final wheel call signature differs')
    call(data,0x674B7F,0x66F430)
    require(data[0x66FC96:0x66FCA6].hex()=='450f118fb0000000410f11a7c0000000','native wheel writer must write final bone worlds')
    # No CAS name-table change is needed: the real stock CAS skeleton table
    # is empty while this vehicle already displays its 40 MDB bones.
    import rootcpk,cas_legacy
    game=rootcpk.default()
    if Path(game.root,'Root.cpk').is_file():
        stock=game.read('OBJECT','V603_FLAK.CAS')
        require(len(cas_legacy.cas_layout(stock).tree.bones)==0,'stock CAS bone-table premise differs')
        import tracked_combat_model as tracked
        md,_archive=tracked.stock(game)
        for side in 'lr':
            require(md.bones[md.bone_index(f'tire_moveG_{side}')].parent==md.bones[md.bone_index(f'tire_moveH_{side}')].parent,
                    'half-idler native sibling parent differs')
    print('M163 native final pose, full draw palette and actual/remote firing contracts passed')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
