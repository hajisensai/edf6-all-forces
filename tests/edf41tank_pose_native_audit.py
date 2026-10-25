"""EDF.dll facts pylib/edf41tank.py CLASS_POSED relies on (the grey map in EDF4.1's first mission, 2026-10-25).

    python tests/edf41tank_pose_native_audit.py <EDF.dll>     (exit 77: no supported EDF.dll given)

Static only (pefile + capstone over the file; V601_TANK.CAS read from the game's archives when they sit next to it):
  * Vehicle601_Tank's slot 45 (vtable 0x17DC250) is the pose 0x620830, which reads the physics into the bones
    (0x6EDCA0), poses the turret (0x661C00) and then calls 0x661810 for the gun;
  * 0x661810 takes the gun bone's record (0x1100280), reads its local at +0x70 (lea rdi,[rdx+0x70]; the model's bind
    when the vehicle has no animation player), turns it by the pitch it is given (0x296F90, the angle in xmm2) and
    writes the turned local back to +0x70 (movups [rdi], 0x661BCC .. 0x661BEF): an in-place turn, so the bone's local
    must be rewritten from its CAS track every frame;
  * the turret pose 0x661C00 reads its bone's +0x70 but never writes it (only the gun is turned in place);
  * EDF6's own V601_TANK.CAS has a track for 'cannon' in every clip (what V301's converted CAS must keep).
"""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: a supported EDF.dll is required (EDF6_NATIVE_DLL)')
    raise SystemExit(77)
if not __debug__:
    raise RuntimeError('assertions must be enabled')
import capstone
import pefile

pe = pefile.PE(sys.argv[1], fast_load=True)
assert pe.FILE_HEADER.TimeDateStamp == 0x678CCB46, hex(pe.FILE_HEADER.TimeDateStamp)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

TANK_VTABLE, POSE_SLOT, POSE = 0x17DC250, 45, 0x620830
GUN_POSE, TURRET_POSE, PHYSICS_TO_BONES, BONE_RECORD, ROTATE_X = 0x661810, 0x661C00, 0x6EDCA0, 0x1100280, 0x296F90


def body(start: int, limit: int = 0x600) -> list:
    out = []
    for i in md.disasm(img[start:start + limit], start):
        out.append(i)
        if i.mnemonic == 'ret' or i.mnemonic == 'jmp' and i.op_str == hex(0x6ED800):
            break
    return out


def calls(ins: list) -> list[int]:
    return [int(i.op_str, 16) for i in ins if i.mnemonic == 'call' and i.op_str.startswith('0x')]


def check() -> None:
    slot = struct.unpack_from('<Q', img, TANK_VTABLE + 8 * POSE_SLOT)[0] - base
    assert slot == POSE, hex(slot)
    pose = body(POSE)
    assert [c for c in calls(pose) if c in (PHYSICS_TO_BONES, TURRET_POSE, GUN_POSE)] == \
        [PHYSICS_TO_BONES, TURRET_POSE, GUN_POSE], [hex(c) for c in calls(pose)]

    gun = body(GUN_POSE)
    text = [(i.address, i.mnemonic, i.op_str) for i in gun]
    assert calls(gun)[:2] == [BONE_RECORD, BONE_RECORD], [hex(c) for c in calls(gun)]
    assert ROTATE_X in calls(gun), 'the gun pose no longer turns by the pitch'
    local = [a for a, m, o in text if m == 'lea' and o == 'rdi, [rdx + 0x70]']
    assert local == [0x661886], local
    turn = next(a for a, m, o in text if m == 'call' and o == hex(ROTATE_X))
    back = [a for a, m, o in text if a > turn and m == 'movups' and re.fullmatch(r'xmmword ptr \[rdi(?: \+ 0x([0-9a-f]+))?\], xmm\d+', o)]
    assert back == [0x661BCC, 0x661BD5, 0x661BE8, 0x661BEF], [hex(a) for a in back]
    assert not any(m == 'mov' and o.startswith('rdi,') for a, m, o in text if 0x661886 < a < 0x661BCC), \
        'rdi (the bone local) changes before the write-back'

    turret = [(i.mnemonic, i.op_str) for i in body(TURRET_POSE, 0x500)]
    assert ('lea', 'rcx, [rdi + 0x70]') in turret
    assert not any(m.startswith('mov') and re.match(r'xmmword ptr \[rdi \+ 0x7[0-9a-f]\]|xmmword ptr \[rdi \+ 0x(?:8|9|a)[0-9a-f]\]', o)
                   for m, o in turret), 'the turret pose writes its local'


def v601_cas() -> None:
    game = Path(sys.argv[1]).resolve().parent
    if not (game / 'Root.cpk').is_file():
        print('no Root.cpk next to EDF.dll: V601_TANK.CAS not checked')
        return
    sys.path.insert(0, str(ROOT / 'pylib'))
    import cas_pose
    import rootcpk
    data = None
    for name in ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk', 'DX11.cpk'):
        if not (game / name).is_file():
            continue
        try:
            data = rootcpk.Game(str(game), name).read('OBJECT', 'V601_TANK.CAS')
            break
        except KeyError:
            continue
    assert data is not None, 'V601_TANK.CAS'
    pose = cas_pose.CasPose(data)
    for clip in pose.clips:
        assert 'cannon' in {t.name for t in clip.tracks}, clip.name


check()
v601_cas()
print('edf41tank pose native audit: ok')
