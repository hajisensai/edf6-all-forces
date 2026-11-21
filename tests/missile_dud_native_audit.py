"""Optional check of what an unarmed plugin missile's dud rests on (src/fuze.h, src/missile.cpp; docs/feedback-2026-11-21-
missile-dud.md): a MissileBullet01 hit shows its damage, its sound and its fireball, each keyed on a field of the round
the plugin writes before the stock step.

    python tests/missile_dud_native_audit.py <EDF.dll>

Pass an installed EDF.dll (TimeDateStamp 0x678CCB46); without one it exits 77 (skipped). Static only: reads the DLL's
bytes (pefile), never loads or runs it. What it holds:
  - the fireball: the hit records itself (0x230CA0: push_back on the list at core+0x6C0, count core+0x6D8, the hit point
    and normal at +0x10 / +0x20 of the 0x50-byte record), and the class's update (0x26A880), with a record there, draws
    EffectGenUtil::Explosion (0x2E5ED0) at the first record's point and normal at size [B+0xB60] (core+0xA20) / 5, then
    ends the round (0x2318E0). core+0xA20 is read nowhere else after the init in the bullet core or the class;
  - the burst radius is core+0x788 (copied from core+0xA20 by the init, 0x2320A9 / 0x2320BD; the hit's area damage
    0x542670 takes GDI+0x58 with GDI = core+0x730);
  - the sound: the init sets 0x600 on every round (0x231D3D), the hit plays AmmoHitSe (core+0xA70) through 0x7B4510 only
    with 0x400 (0x2325CD bt eax,0xA); no other bt/test of that bit in the bullet core or the class;
  - the damage: the hit takes the area damage only with 0x10 (0x232616);
  - the plugin checks the same bytes before it hooks (kDudSites) and reads / writes the same fields.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

if sys.platform != 'win32' or len(sys.argv) != 2 or not Path(sys.argv[1]).is_file():
    print('SKIP: pass a supported EDF.dll path')
    raise SystemExit(77)
import pefile  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
pe = pefile.PE(sys.argv[1], fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: EDF.dll TimeDateStamp {pe.FILE_HEADER.TimeDateStamp:#x}')
    raise SystemExit(77)
img = pe.get_memory_mapped_image()
checks = 0


def check(ok: bool, why: str) -> None:
    global checks
    checks += 1
    if not ok:
        print('FAIL', why)
        sys.exit(1)


def at(rva: int, hexbytes: str, why: str) -> None:
    want = bytes.fromhex(hexbytes)
    check(img[rva:rva + len(want)] == want, f'{rva:#x}: {why}')


def call(rva: int, target: int, why: str) -> None:
    check(img[rva] == 0xE8 and rva + 5 + struct.unpack_from('<i', img, rva + 1)[0] == target, f'{rva:#x}: {why}')


def f32(rva: int) -> float:
    return struct.unpack_from('<f', img, rva)[0]


# The fireball: MissileBullet01's update (vtable 0x17A1C10 slot 5).
check(struct.unpack_from('<Q', img, 0x17A1C10 + 5 * 8)[0] - pe.OPTIONAL_HEADER.ImageBase == 0x26A880,
      'MissileBullet01 slot 5 is 0x26A880')
at(0x26A8AC, '4883b91808000000', 'cmp qword [rcx+0x818],0: a hit recorded (core+0x6D8)')
at(0x26A8B4, '7464', 'je: none, no fireball')
at(0x26A8C1, 'f30f1099600b0000', 'movss xmm3,[rcx+0xB60]: its size core+0xA20')
at(0x26A8C9, 'f30f5e1d', 'divss xmm3,[rip+..]')
check(f32(0x26A8D1 + struct.unpack_from('<i', img, 0x26A8CD)[0]) == 5.0, 'drawn at a fifth of it')
at(0x26A8D1, '488b8108080000', 'mov rax,[rcx+0x808]: the records (core+0x6C8)')
at(0x26A8D8, '4c8d4020', 'lea r8,[rax+0x20]: the first one\'s normal')
at(0x26A8DC, '488d5010', 'lea rdx,[rax+0x10]: its point')
call(0x26A904, 0x2E5ED0, 'EffectGenUtil::Explosion')
call(0x26A913, 0x2318E0, 'then the round ends')
at(0x2318EA, '83c803', 'or eax,3: dead')
# The record: pushed by the hit (0x230CA0, from the hit handler at 0x23238F / 0x23256D).
call(0x23238F, 0x230CA0, 'a body hit records itself')
call(0x23256D, 0x230CA0, 'a ground hit records itself')
at(0x230F98, '488dbbc0060000', 'lea rdi,[rbx+0x6C0]: the list')
at(0x230F9F, '488b4718', 'mov rax,[rdi+0x18]: its count (core+0x6D8)')
at(0x230FCF, '48894718', 'mov [rdi+0x18],rax: one more')
at(0x230FDE, '0f114910', 'the hit point at +0x10')
at(0x230FE6, '0f114120', 'its normal at +0x20')
# The burst radius: core+0x788, copied from core+0xA20 once.
at(0x2320A9, 'f30f1083200a0000', 'movss xmm0,[rbx+0xA20]')
at(0x2320BD, 'f30f118388070000', 'movss [rbx+0x788],xmm0')
at(0x232682, '488d8f30070000', 'lea rcx,[rdi+0x730]: the GDI (+0x58 = core+0x788)')
call(0x232689, 0x542670, 'the hit\'s area damage, radius GDI+0x58')
# The sound and the damage.
at(0x231D3D, '4181c900060000', 'or r9d,0x600: every round starts with 0x400')
at(0x2325CD, '0fbae00a7320', 'bt eax,0xA / jae: the hit\'s sound only with 0x400')
at(0x2325DD, '488d8f700a0000', 'lea rcx,[rdi+0xA70]: AmmoHitSe')
call(0x2325EE, 0x7B4510, 'played')
at(0x232616, 'a8100f84', 'test al,0x10 / je: the area damage only with 0x10')

# Nothing else in the bullet core (0x22E000..0x240000) or the class (0x266000..0x26B000) names core+0xA20 ([B+0xB60] in
# the class) after the init, or tests 0x400 of the flags: the displacement's bytes appear only in the one instruction
# each (any form, read or write), the bit test only at the hit's sound.
text = bytes(img)
for lo, hi, disp, only in ((0x22E000, 0x240000, 0xA20, 0x2320A9 + 4), (0x266000, 0x26B000, 0xB60, 0x26A8C1 + 4)):
    seen = [lo + m.start() for m in re.finditer(re.escape(struct.pack('<I', disp)), text[lo:hi])]
    check(seen == [only], f'{lo:#x}..{hi:#x}: +{disp:#x} named at {[hex(x) for x in seen]}')
bits = [lo + m.start() for lo, hi in ((0x22E000, 0x240000), (0x264000, 0x26B000))
        for m in re.finditer(rb'\x0f\xba[\xe0-\xe7]\x0a', text[lo:hi])]
check(bits == [0x2325CD], f'bt r32,0xA in the bullet code: {[hex(x) for x in bits]}')

# The plugin: the same bytes checked before the hook, the same fields.
missile = (ROOT / 'src/missile.cpp').read_text(encoding='utf-8')
fuze = (ROOT / 'src/fuze.h').read_text(encoding='utf-8')
for rva in (0x26A8AC, 0x26A8C1, 0x2325CD, 0x232616, 0x2320BD):
    check(f'{{{rva:#X},'.replace('0X', '0x') in missile, f'src/missile.cpp kDudSites checks {rva:#x}')
check('kBlast=kCore+0x788;' in missile, 'src/missile.cpp: the burst radius is core+0x788')
check('kFireball=kCore+0xA20,kHits=kCore+0x6D8;' in missile, 'src/missile.cpp: the fireball size and the hit count')
check('Put<float>(b,kFireball,fuze::HitFireball(blast,explosive,armed));' in missile, 'src/missile.cpp writes the fireball')
check('if(At<std::size_t>(b,kHits)!=0)return;' in missile, 'src/missile.cpp: a struck round is left as it struck')
check('constexpr std::uint32_t kHitSound=0x400;' in fuze and 'constexpr std::uint32_t kAreaOnHit=0x10;' in fuze,
      'src/fuze.h: the bits')
print(f'missile dud native: {checks} checks passed')
