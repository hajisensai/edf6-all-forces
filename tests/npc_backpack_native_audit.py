"""Optional check of the EDF.dll code an NPC's backpack weapon rests on (src/npc_backpack.cpp, docs/npc-backpack-re.md).

    python tests/npc_backpack_native_audit.py <EDF.dll>

Pass an installed EDF.dll (TimeDateStamp 0x678CCB46); without one (or off Windows) it exits 77 (skipped). Static only:
it reads the DLL's bytes (pefile), never loads or runs it. What it holds:
  - src/npc_backpack_signatures.inc is exactly what tools/npc_backpack_sigs.py renders from this DLL (no hand edit);
  - the root cause: the backpack input 0x5A1D70 returns for a soldier without a pad (cmp [rcx+0x340],0; je);
  - the use the plugin repeats: WeaponSet 0 +0x140 = the sub weapon, 0x5A0A60(+0x110, 0x5A28A0, 0), +0x1A20 = 0, both in
    the input's tap (0x5A1FDB..) and in the receive of message 0x10 on every copy (0x59E7C1..), the jump table's entry 16
    pointing there; the message (0x10, slot) sent through NetworkObject slot 14 (+0x70);
  - the long press's reload (+0x690 -> +0x13B), the record offsets (+0x1A00 / +0x1A10, stride 0x20, +0 index), the gates
    (+0x5D0 bit 2, +0x39C & ~2, +0x158C 0x2000 / 0x4000, +0x1A28 / +0x1A30, rounds +0xBE8);
  - the two type descriptors are WeaponBase's and Weapon_Sub's, the cast __RTDynamicCast's (0x12DA7AA);
  - the template setup: soldier_sub_weapon_slot read into +0x19F8, soldier_load_weapon weapons made with eight 5 stars;
  - the plugin's constants (src/npc_backpack.cpp) are these addresses.
"""
from __future__ import annotations

import os
import re
import struct
import sys
from pathlib import Path

if sys.platform != 'win32' or len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass a supported EDF.dll path')
    raise SystemExit(77)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import npc_backpack_sigs as sigs  # noqa: E402

checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


_, img = sigs.image(sys.argv[1])
with open(sigs.INC, encoding='utf-8') as f:
    check(f.read() == sigs.render(img), 'src/npc_backpack_signatures.inc is tools/npc_backpack_sigs.py\'s rendering of this DLL')
covered = [(rva, rva + size) for rva, size, _ in sigs.SIGNATURES]


def held(rva: int, size: int) -> bool:
    return any(a <= rva and rva + size <= b for a, b in covered)


def at(rva: int, hexbytes: str) -> bool:
    data = bytes.fromhex(hexbytes)
    return img[rva:rva + len(data)] == data and held(rva, len(data))


def rel32(rva: int, size: int) -> int:
    """The target of the rel32 ending instruction rva..rva+size."""
    return rva + size + struct.unpack_from('<i', img, rva + size - 4)[0]


# The root cause: no pad, no backpack.
check(at(0x5A1DA4, '4883b94003000000' '0f84'), '0x5A1DA4: cmp qword [rcx+0x340],0; je (the pad test)')
check(rel32(0x5A1DAC, 6) == 0x5A2147, 'its je leaves the input')
check(img[0x59AC65] == 0xE8 and rel32(0x59AC65, 5) == 0x5A1D70, 'SoldierBase slot 5 calls the input for every soldier (0x59AC65)')
# The gates.
check(at(0x5A1DD3, 'f683d005000004'), '+0x5D0 bit 2')
check(at(0x5A1DDC, 'f7839c030000fdffffff'), '+0x39C & ~2')
check(at(0x5A1DEE, 'b800400000' '4585ff' '410f44c5' '85838c150000'), 'the mask +0x158C: 0x4000, 0x2000 (r13d) for slot 0')
check(at(0x5A1E10, '488b8b001a0000'), 'records at +0x1A00')
check(at(0x5A1E0C, '49c1e605'), 'stride 0x20')
check(at(0x5A1E5A, '4c3983101a0000'), 'their count at +0x1A10')
check(at(0x5A1E68, '488b83001a0000' '49630c06' '3b8b60190000'), 'a record\'s +0 index, below +0x1960')
check(at(0x5A1E89, '488b8350190000'), 'into the list at +0x1950')
check(rel32(0x5A1E99, 7) == 0x20476E8 and rel32(0x5A1EA0, 7) == 0x2045768 and rel32(0x5A1EAC, 5) == 0x12DA7AA,
      'the cast: WeaponBase -> Weapon_Sub through 0x12DA7AA')
check(img[0x2045778:0x2045789] == b'.?AVWeaponBase@@\0' and img[0x20476F8:0x2047709] == b'.?AVWeapon_Sub@@\0',
      'the type descriptors\' names')
check(at(0x5A1EBD, '488b8b70190000' '4883b94001000000'), 'WeaponSet 0 (+0x1970) +0x140 in use')
check(at(0x5A1ECE, '80bb201a000000'), '... unless +0x1A20')
check(at(0x5A1EE8, '4883bb281a000000') and at(0x5A1EF2, '83bb301a000001'), '+0x1A28 with +0x1A30 >= 1')
check(at(0x5A1F75, '83bfe80b000000'), 'rounds +0xBE8')
# The use, in the input's tap and in every copy's receive.
check(at(0x5A1FC1, '488b4740') and at(0x5A1FD2, '66c780c40e00000000'), 'the set\'s weapon +0xEC4 = 0')
check(at(0x5A1FDB, '4889b740010000'), 'WeaponSet 0 +0x140 = the sub weapon')
check(at(0x5A1FE2, '488d8f10010000') and rel32(0x5A1FEC, 7) == 0x5A28A0 and rel32(0x5A1FF3, 5) == 0x5A0A60,
      '0x5A0A60(+0x110, 0x5A28A0, 0)')
check(at(0x5A1FF8, 'c683201a000000'), '+0x1A20 = 0')
check(struct.unpack_from('<i', img, 0x59E880)[0] == 0x59E6FD and held(0x59E880, 4), 'message 0x10: the jump table\'s entry 16')
check(at(0x59E24B, '440fbec0') and at(0x59E24F, '4183c0f4'), 'the receive switches on the type - 0xC')
check(at(0x59E7C1, '4889bb40010000') and at(0x59E7C8, '488d8b10010000') and rel32(0x59E7D2, 7) == 0x5A28A0 and
      rel32(0x59E7D9, 5) == 0x5A0A60 and at(0x59E7DE, '4488ae001900'), 'every copy: the same use (rsi = +0x120)')
check(at(0x59E708, '483986f0180000'), 'every copy: the slot below +0x1A10 (+0x18F0 from +0x120)')
# The message the input sends: (0x10, slot) through NetworkObject +0x120 slot 14, never from a copy (+0x128 bit 0).
check(at(0x5A2013, 'f6832801000001'), 'a copy sends nothing')
check(at(0x5A2020, 'ba40000000') and rel32(0x5A202A, 5) == 0x79A460, 'the writer (capacity 0x40)')
check(at(0x5A2030, 'b210') and rel32(0x5A2037, 5) == 0x12B5790 and rel32(0x5A2045, 5) == 0x12B5790, 'type 0x10, then the slot')
check(at(0x5A204A, '488b8320010000' '4533c0') and at(0x5A2059, '488d8b20010000' 'ff5070'), 'sent through +0x120\'s slot 14 (+0x70)')
check(rel32(0x5A2075, 5) == 0x760180, 'the writer destroyed')
# The long press's reload.
check(at(0x5A208B, '83be9006000000') and at(0x5A2094, 'c6863b01000001'), '+0x690 set: +0x13B = 1')
# The templates' setup.
check(at(0x593C49, '498db5f8190000'), 'soldier_sub_weapon_slot into +0x19F8')
check(at(0x593F24, '498b8d001a0000'), 'a record index written at +0')
check(at(0x59439B, 'b205') and rel32(0x5943A4, 5) == 0x12DA786 and rel32(0x594393, 5) == 0x12DA786,
      'soldier_load_weapon weapons: their eight stars set to 5')
check(img[0x5944A9] == 0xE8 and rel32(0x5944A9, 5) == 0x58F360, 'made by 0x58F360(human, path, stars)')
# The plugin's constants.
code = open(os.path.join(ROOT, 'src', 'npc_backpack.cpp'), encoding='utf-8').read()
for name, value in (('kMachineGo', 0x5A0A60), ('kSubState', 0x5A28A0), ('kDynamicCast', 0x12DA7AA), ('kTypeWeaponBase', 0x2045768),
                    ('kTypeWeaponSub', 0x20476E8), ('kConstruct', 0x79A460), ('kDestroy', 0x760180), ('kWriteSmall', 0x12B5790),
                    ('kSubs', 0x1A00), ('kSubCount', 0x1A10), ('kSubStride', 0x20), ('kSets', 0x1970), ('kSetSub', 0x140),
                    ('kSetWeapon', 0x40), ('kSetMachine', 0x110), ('kMainBusy', 0xEC4), ('kSubChain', 0x1A20),
                    ('kSubLock', 0x1A28), ('kSubLockCount', 0x1A30), ('kStateBits', 0x5D0), ('kHumanState', 0x39C),
                    ('kControlMask', 0x158C), ('kReloadType', 0x690), ('kReloadPress', 0x13B), ('kNet', 0x120)):
    m = re.search(rf'\b{name}=(0x[0-9A-Fa-f]+)', code)
    check(m is not None and int(m.group(1), 16) == value, f'npc_backpack.cpp {name} = {value:#x}')
check(re.search(r'kSlotSend=14;', code) is not None and re.search(r'kMessageSub=0x10;', code) is not None, 'slot 14, message 0x10')
print(f'npc_backpack_native_audit: {checks} checks passed')
