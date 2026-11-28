"""The new classes' patch (src/soldier_classes.cpp) against the game's EDF.dll and CONFIG.SGO, and its copies read
with pylib/sgo.py.

  python tests/soldier_classes_native_audit.py PATH/TO/soldier_classes_test.exe

Always: a stock-like CONFIG.SGO written with pylib/sgo.py goes through the plugin's extension (soldier_classes_test
--extend); pylib/sgo.py reads the result back and it is the input's classes 0..3 and categories, unchanged, plus the
plan's classes 4 / 5 and five categories (docs/plan-2026-11-28-new-classes-impl.md 1), written out here independently.
With the game (read only; skipped without it):
- every byte sequence soldier_classes.cpp checks is EDF.dll's, the redirected calls call 0x528A0 / 0xE34D0 and
  0xE34D0 is `[[rcx+0x20]+4]`;
- the game's own CONFIG.SGO (its values, little-endian as the game holds them after loading) is extended the same way:
  the stock layout is the one the extension accepts."""
from __future__ import annotations

import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import gamedir  # noqa: E402
import sgo  # noqa: E402

checks = 0
failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


# soldier_classes.cpp's checks: address -> bytes.
BYTES = {
    0xE2C0D: '488d158c56680149' '8bcce884fcf6ff48' '894620488d159156' '6801498bcce871fc' 'f6ff488bf8',
    0xDCD63: 'e8686700' '0085c0',
    0xDDB25: 'e8a65900' '00443be8',
    0x893066: 'e8650485' 'ff448be0',
    0x70FC2D: '8b9411906e000085d2',
    0x70FC36: '7436',
    0x70FD82: 'ffc3',
}
CALLS = {0xE2C17: 0x528A0, 0xE2C2A: 0x528A0, 0xDCD63: 0xE34D0, 0xDDB25: 0xE34D0, 0x893066: 0xE34D0}

CATEGORIES = [[400, 'WeaponCategory_ZwVehicle'], [401, 'WeaponCategory_ZwPistol'], [500, 'WeaponCategory_ZwMelee'],
              [501, 'WeaponCategory_ZwRanged'], [502, 'WeaponCategory_ZwSupport']]


def new_class(t: int, key: str, stem: str, slots: list) -> list:
    return [t, f'SoldierType_{key}', [f'app:/object/{stem}_0{n}.sgo' for n in range(1, 5)], [200.0, 0.56], slots,
            [f'App:/DefaultPackage/CustomColor{key}.sgo', [f'APP:/MENUOBJECT/{stem}_0{n}.SGO' for n in range(1, 5)],
             'App:/UI/TypeSelectImage.rab', [f'SoldierType_{key}_0{n}.dds' for n in range(1, 5)]]]


NEW = [
    new_class(4, 'ZwVehicler', 'zw_vehicler', [['ZW_SlotVehicle', 'ZW_VehicleNone', [400], 0, 1],
                                               ['ZW_SlotPistol', 'ZW_Gyrojet01', [401], 0, 0]]),
    new_class(5, 'ZwRuneBlade', 'zw_runeblade', [['ZW_SlotMelee', 'ZW_MeleeNone', [500], 0, 0],
                                                 ['ZW_SlotRanged', 'ZW_RuneCarbine01', [501], 0, 0],
                                                 ['ZW_SlotSupport', 'ZW_SupportNone', [502], 0, 1]]),
]


def fixture() -> bytes:
    def element(t: int, name: str, slots: int) -> list:
        return [t, f'SoldierType_{name}', [f'app:/object/{name}{n}.sgo' for n in range(1, 5)],
                [sgo.Float(struct.pack('<f', 150.0 + t)), sgo.Float(struct.pack('<f', 0.5))],
                [[f'Weapon_Slot{s}', f'e{name}{s}', [t * 10, t * 10 + s], 0, int(s == slots - 1)] for s in range(slots)],
                [f'App:/DefaultPackage/CustomColor{name}.sgo', [f'APP:/MENUOBJECT/{name}{n}.SGO' for n in range(1, 5)],
                 'App:/UI/TypeSelectImage.rab', [f'SoldierType_{name}_0{n}.dds' for n in range(1, 6 - t % 2)]]]
    members = {
        'SoldierInit': [element(0, 'Ranger', 4), element(1, 'WingDiver', 4), element(2, 'AirRaider', 5), element(3, 'Fencer', 6)],
        'SoldierWeaponCategory': [[i, f'Weapon_{i}'] for i in (0, 1, 2, 334)] + [[335, '']],
        'Other': 7,
    }
    return sgo.write(1, members)


def extended(exe: str, config: bytes, label: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        src, out = os.path.join(tmp, 'in.sgo'), os.path.join(tmp, 'out.sgo')
        with open(src, 'wb') as f:
            f.write(config)
        run = subprocess.run([exe, '--extend', src, out], capture_output=True, text=True)
        check(run.returncode == 0, f'{label}: extended ({run.stdout.strip()})')
        if run.returncode:
            return
        with open(out, 'rb') as f:
            _, got = sgo.read(f.read())
    _, stock = sgo.read(config)
    init = sgo.plain(got['SoldierInit'])
    cats = sgo.plain(got['SoldierWeaponCategory'])
    stock_init = sgo.plain(stock['SoldierInit'])
    stock_cats = sgo.plain(stock['SoldierWeaponCategory'])
    check(len(init) == 6 and [e[0] for e in init] == list(range(6)), f'{label}: six classes 0..5')
    check(init[:4] == stock_init, f'{label}: the stock four unchanged')
    check(_same(init[4:], NEW), f'{label}: classes 4 / 5 are the plan\'s')
    check(cats[:len(stock_cats)] == stock_cats and cats[len(stock_cats):] == CATEGORIES, f'{label}: the five categories appended')


def _same(a, b) -> bool:
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(_same(x, y) for x, y in zip(a, b))
    if isinstance(b, float):
        return isinstance(a, float) and abs(a - b) < 1e-6
    return a == b


def source_has(hexbytes: str) -> bool:
    with open(os.path.join(ROOT, 'src', 'soldier_classes.cpp'), encoding='utf-8') as f:
        text = f.read()
    listed = ''.join(m.lower() for m in re.findall(r'0x([0-9A-Fa-f]{2})\b', text))
    return hexbytes in listed


def audit_code(img: bytes) -> None:
    for site, hexbytes in BYTES.items():
        check(img[site:site + len(hexbytes) // 2].hex() == hexbytes, f'EDF+{site:#x} bytes')
        check(source_has(hexbytes), f'soldier_classes.cpp checks the bytes at EDF+{site:#x}')
    for site, target in CALLS.items():
        rel = struct.unpack_from('<i', img, site + 1)[0]
        check(img[site] == 0xE8 and site + 5 + rel == target, f'EDF+{site:#x} calls {target:#x}')
    check(img[0xE34D0:0xE34D8].hex() == '488b41208b4004c3', 'EDF+0xe34d0 is mov rax,[rcx+0x20]; mov eax,[rax+4]; ret')


def main() -> int:
    exe = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else ''
    if not exe or not os.path.isfile(exe):
        print('usage: soldier_classes_native_audit.py PATH/TO/soldier_classes_test.exe')
        return 2
    extended(exe, fixture(), 'fixture')
    game_dir = gamedir.find()
    dll = os.path.join(game_dir, 'EDF.dll') if game_dir else ''
    if dll and os.path.isfile(dll):
        import room_preview_sigs
        import rootcpk
        _, img = room_preview_sigs.image(dll)
        audit_code(img)
        # The file is big-endian ('\0OGS'); the game swaps it on load (0xE34D0 reads the count without a bswap), so
        # the extension sees it little-endian: the same values, written little-endian here.
        version, members = sgo.read(rootcpk.Game(game_dir).read('DEFAULTPACKAGE', 'CONFIG.SGO'))
        extended(exe, sgo.write(version, members), 'CONFIG.SGO')
    else:
        print('soldier_classes_native_audit: no game; EDF.dll / CONFIG.SGO checks skipped')
    print(f'soldier_classes_native_audit: {checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
