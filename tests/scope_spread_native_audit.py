"""Read-only audit of the native sites the scoped weapons' spread stands on (src/scope_spread.cpp,
docs/scope-accuracy-re.md). No game process, no game writes; EDF.dll is only read as a file.

Usage: python tests/scope_spread_native_audit.py <EDF.dll>
Without an existing supported EDF.dll (the build's EDF6_NATIVE_DLL unset): SKIP (77).

Pinned:
  - the player creation's weapon call (0x5A4479..0x5A4488): the bytes scope_spread.cpp checks, the call to 0x58F360; the
    row in esi from 0x5A4330 (GetRow 0x5A434A gets it in r8d) and esi / rsi not written up to the call; the weapon table
    as GameStatus (0x20B2890) +0x130; 0x58F360's two callers (0x5A4484 the player's, 0x5944A9 the NPCs');
  - 0x58F360 reads the old weapon count (+0x1960) first: the appended weapon sits at that index;
  - GetRow 0xE1CF0 reads the table's rows at +0x188 and fills the row's category (+0x10);
  - the round's cone: 0x691560's only caller is the shot entry 0x696FD0 (0x6976CA); 0x691AFA..0x691B10 E14 x FireAccuracy
    as the upper bound (xmm3) of 0x4E820, the lower bound xmm2 zeroed at 0x691ACC;
  - the shot entry's callers and their replay argument ([rsp+20h]): the local shot 0x690D47 0, the replays 1;
    its prologue the one heli.cpp hooks;
  - FireAccuracy's online-mode reader 0x767150 runs only from 0x691BC0, whose only caller is the weapon's setup 0x68EBDE
    (never inside a round: a round's write cannot change it);
  - the scope: the soldier's frame reads +0x690 == 1 and +0x780 at 0x59AE7C / 0x59AE81, the second fire input d72 at
    0x59AE42; the toggle 0x696310 reads +0x780 at 0x696322.
"""
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
if not __debug__:
    raise RuntimeError('Run without -O: contract assertions must execute')

dll = Path(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1] else None
if dll is None or not dll.is_file():
    print('SKIP: pass an existing supported EDF.dll (EDF6_NATIVE_DLL)')
    raise SystemExit(77)

import capstone  # noqa: E402
import pefile  # noqa: E402

pe = pefile.PE(str(dll), fast_load=True)
if pe.FILE_HEADER.TimeDateStamp != 0x678CCB46:
    print(f'SKIP: {dll} is not the supported Steam EDF.dll')
    raise SystemExit(77)
img = pe.get_memory_mapped_image()
text = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
TVA, TSZ = text.VirtualAddress, text.Misc_VirtualSize
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
checks = 0


def ok(cond: bool, what: str) -> None:
    global checks
    assert cond, what
    checks += 1
    print('PASS', what)


def ins(at: int):
    return next(md.disasm(img[at:at + 16], at))


def text_of(at: int) -> str:
    i = ins(at)
    return f'{i.mnemonic} {i.op_str}'.strip()


def walk(start: int, end: int):
    at = start
    while at < end:
        i = ins(at)
        yield i
        at += i.size


_calls = None


def callers(target: int) -> list[int]:
    """Direct call / jmp rel32 sites to target in .text."""
    global _calls
    if _calls is None:
        _calls = {}
        body = img[TVA:TVA + TSZ]
        for m in re.finditer(b'[\xe8\xe9]', body):
            p = m.start()
            if p + 5 > len(body):
                break
            rel = struct.unpack_from('<i', body, p + 1)[0]
            _calls.setdefault(TVA + p + 5 + rel, []).append(TVA + p)
    return sorted(_calls.get(target, []))


def call_target(at: int) -> int:
    i = ins(at)
    assert i.mnemonic == 'call' and i.op_str.startswith('0x'), (hex(at), i.mnemonic, i.op_str)
    return int(i.op_str, 16)


def src_bytes(name: str) -> bytes:
    src = (ROOT / 'src' / 'scope_spread.cpp').read_text(encoding='utf-8')
    m = re.search(name + r'\[\]=\{([^}]*)\}', src)
    assert m, name
    return bytes(int(x, 16) for x in m.group(1).split(','))


def src_const(name: str) -> int:
    src = (ROOT / 'src' / 'scope_spread.cpp').read_text(encoding='utf-8')
    m = re.search(r'\b' + name + r'=0x([0-9A-Fa-f]+)', src)
    assert m, name
    return int(m.group(1), 16)


# --- the player's weapon creation
make_ctx, make_site, make_weapon = src_const('kMakeCtx'), src_const('kMakeSite'), src_const('kMakeWeapon')
ok((make_ctx, make_site, make_weapon) == (0x5A4479, 0x5A4484, 0x58F360), 'scope_spread.cpp names the creation call')
code = src_bytes('kMakeCode')
ok(img[make_ctx:make_ctx + len(code)] == code, 'the creation call bytes are the ones scope_spread.cpp checks')
ok(call_target(make_site) == make_weapon, '0x5A4484 calls 0x58F360')
ok(callers(make_weapon) == [0x5944A9, 0x5A4484], "0x58F360's callers: the NPCs' 0x5944A9 and the player's 0x5A4484 only")
ok(text_of(0x5A4330) == 'mov esi, dword ptr [r13 + rbx*4]', '0x5A4330 takes the loadout row into esi')
ok(text_of(0x5A4343) == 'mov r8d, esi' and call_target(0x5A434A) == 0xE1CF0, 'GetRow gets that row (r8d = esi)')
gs = ins(0x5A4335)
ok(gs.mnemonic == 'mov' and 'rip' in gs.op_str and gs.address + gs.size + gs.disp == 0x20B2890 and
   text_of(0x5A433C) == 'add rcx, 0x130', 'the table is *(EDF+0x20B2890) + 0x130')
ok(src_const('kGameStatus') == 0x20B2890 and src_const('kWeaponTable') == 0x130, 'scope_spread.cpp reads that table')
written = []
for i in walk(0x5A4335, make_site + 5):
    _, w = i.regs_access()
    names = {i.reg_name(r) for r in w}
    if names & {'esi', 'rsi', 'si', 'sil'}:
        written.append(hex(i.address))
ok(not written, f'esi / rsi not written between 0x5A4335 and the call ({written})')
ok(text_of(0x58F392) == 'mov rsi, rcx' and text_of(0x58F3CA) == 'mov r12, qword ptr [rsi + 0x1960]',
   '0x58F360 keeps the old weapon count (+0x1960) of the human (rcx)')

# --- GetRow / RowCount
ok(src_const('kGetRow') == 0xE1CF0 and src_const('kRowCount') == 0xE23F0 and src_const('kTableRows') == 0x188 and
   src_const('kRowCategory') == 0x10, 'scope_spread.cpp: GetRow, RowCount, rows +0x188, category +0x10')
ok(text_of(0xE1D12) == 'movups xmm0, xmmword ptr [rcx + 0x188]', 'GetRow reads the rows at table +0x188')
ok(text_of(0xE1D4F) == 'mov dword ptr [rdx + 0x10], r12d', "GetRow fills the out row's +0x10 (the category)")
ok(callers(0xE23F0) != [], 'RowCount 0xE23F0 is called by the game')

# --- the round's cone
cone = src_bytes('kConeCode')
ok(src_const('kConeRead') == 0x691AFA and img[0x691AFA:0x691AFA + len(cone)] == cone, 'the cone read bytes (E14 x +0x378)')
ok(text_of(0x691AFA) == 'movss xmm3, dword ptr [rdi + 0xe14]' and text_of(0x691B02) == 'mulss xmm3, dword ptr [rdi + 0x378]',
   '0x691AFA: xmm3 = weapon+0xE14 x weapon+0x378')
ok(call_target(0x691B10) == 0x4E820 and text_of(0x691ACC) == 'xorps xmm2, xmm2', 'the cone is the upper bound of 0x4E820 from 0')
ok(callers(0x691560) == [0x6976CA], "0x691560's only caller is 0x6976CA")
ok(0x696FD0 < 0x6976CA < 0x698000 and text_of(0x6976C7) == 'mov rcx, r15', 'inside the shot entry 0x696FD0, the weapon in rcx')

# --- the shot entry and its replay argument
heli = (ROOT / 'src' / 'heli.cpp').read_text(encoding='utf-8')
sig = re.search(r'kMedicShotSig\[\]=\{([^}]*)\}', heli)
sig = bytes(int(x, 16) for x in sig.group(1).split(','))
ok(img[0x696FD0:0x696FD0 + len(sig)] == sig, "the shot entry's prologue is the one heli.cpp hooks")
want = {0x6904F7: 1, 0x690603: 1, 0x690D54: 0, 0x692A24: 1, 0x694894: 1}
ok(callers(0x696FD0) == sorted(want), f"the shot entry's callers: {[hex(x) for x in callers(0x696FD0)]}")
for site, replay in want.items():
    found = None
    for i in walk(site - 0x14, site):
        if i.mnemonic == 'mov' and i.op_str.startswith('byte ptr [rsp + 0x20], '):
            found = int(i.op_str.rsplit(', ', 1)[1], 0)
    ok(found == replay, f'{site:#x}: replay {replay}')
ok(text_of(0x690D47) == 'mov byte ptr [rsp + 0x20], 0', 'the local shot 0x690D47 passes replay 0')
mh = re.search(r'void __fastcall MedicShotHook\(.*?\n\}', heli, re.S).group(0)
order = [mh.find(s) for s in ('ScopeSpreadShot(weapon,replay,&scope)', 'medicShotNext(weapon,muzzle,overrideParam,counter,replay)',
                              'ScopeSpreadRestore(scope)')]
ok(all(x >= 0 for x in order) and order == sorted(order), 'MedicShotHook: scope write, the stock shot, the restore')

# --- FireAccuracy's other reader
ok(callers(0x767150) == [0x691CEB], "0x767150's only caller is 0x691CEB")
ok(text_of(0x7672CD) == 'movss xmm1, dword ptr [rsi + 0x378]', '0x767150 reads FireAccuracy (the online mode)')
ok(0x691BC0 < 0x691CEB < 0x691E00 and callers(0x691BC0) == [0x68EBDE], '0x691BC0 (its caller) runs only from the setup 0x68EBDE')

# --- the scope
ok(text_of(0x59AE42) == 'cmp byte ptr [r14 + rbx + 0xd72], 0', 'the second fire (scope) input d72')
ok(text_of(0x59AE7C) == 'cmp edx, 1' and text_of(0x59AE81) == 'cmp byte ptr [r8 + 0x780], 0',
   'the soldier frame: SecondaryFire_Type 1 and the scope open +0x780')
ok('+ 0x780]' in text_of(0x696322), 'the toggle 0x696310 reads +0x780')
ok(src_const('kWeaponScope') == 0x690 and src_const('kWeaponZoomed') == 0x780 and src_const('kWeaponCone') == 0xE14 and
   src_const('kWeaponAccuracy') == 0x378 and src_const('kWeaponOwner') == 0x120, 'scope_spread.cpp: the weapon fields')

print(f'scope_spread_native: {checks} checks passed')
