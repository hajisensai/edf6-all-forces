"""The 整备 weapons page against the game's own files (read only): src/weapon_equip_signatures.inc is EDF.dll's code,
the 3D preview's OnMessage sends message 4 where docs/weapon-select-re.md says, and the generated weapon data
(src/weapon_series.inc, tools/gen_weapon_series.py) is the game's: SoldierInit's slot rules, the stock rows' names and
order (WEAPONTABLE), their Chinese names (WEAPONTEXT.SC / .CN into docs/weapon-names-zh.csv) and their levels (col4 with
GetRow's formula). Without the game: the source checks only."""
from __future__ import annotations

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import gamedir  # noqa: E402

checks = 0
failed = 0


def check(ok: bool, what: str) -> None:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
        print('FAIL', what)


def audit_source() -> None:
    """weapon_equip.cpp's constants are the ones the signatures pin."""
    import weapon_equip_sigs as sigs
    with open(os.path.join(ROOT, 'src', 'weapon_equip.cpp'), encoding='utf-8') as f:
        text = f.read()
    rvas = {rva for rva, _, _ in sigs.SIGNATURES}
    for name in ('kRowCount', 'kGetRow', 'kFindRow', 'kLevelCap'):
        m = re.search(rf'{name}=0x([0-9A-F]+)', text)
        check(bool(m) and int(m.group(1), 16) in rvas, f'weapon_equip.cpp {name} is a signed function')
    for const in ('kGsCapOn=0x14FC8', 'kGsEquipped=0x6E98', 'kGsPlayerStride=0x3E60', 'kGsClassStride=0x18',
                  'kGsClass=0x6E90', 'kRowLevel=0x20'):
        check(const in text, f'weapon_equip.cpp {const}')


def audit_code(img: bytes) -> None:
    import struct

    import weapon_equip_sigs as sigs
    with open(sigs.INC, encoding='utf-8') as f:
        check(f.read() == sigs.render(img), 'weapon_equip_signatures.inc is EDF.dll\'s code (rerun tools/weapon_equip_sigs.py)')

    def rel(at: int, size: int) -> int:   # the target of a call / jmp rel32 (size: the instruction's length)
        return at + size + struct.unpack_from('<i', img, at + size - 4)[0]
    check(rel(0x8BB9EC, 5) == 0x119D1B0 and img[0x8BB9E4:0x8BB9E9] == b'\xBA\x04\x00\x00\x00',
          'msg 4 sender: 0x119D1B0(system, 4, payload)')
    check(rel(0x8BB972, 7) == 0x20B2960, 'msg 4 sender: the UI system pointer at EDF+0x20B2960')
    check(rel(0x8BDA4D, 5) == 0x8BB920, 'confirm calls the msg 4 sender')
    check(rel(0x8BC86A, 5) == 0xD8850 and rel(0x8BC85A, 7) == 0x20B2890, 'cap predicate: 0xD8850(GS)')
    check(struct.unpack_from('<f', img, 0x1765A14)[0] == 0.25 and struct.unpack_from('<f', img, 0x1765A68)[0] == 100.0,
          'GetRow level constants 0.25f, 100.0f')
    check(rel(0xE20BF, 8) == 0x1765A14 and rel(0xE20CB, 8) == 0x1765A68, 'GetRow multiplies by those constants')
    # The 3D preview's OnMessage jump table (0x93B320): id 4 has a case of its own (docs/room-3d-preview-re.md).
    table = 0x93C2E4
    cases = [struct.unpack_from('<I', img, table + 4 * i)[0] for i in range(8)]
    check(bytes.fromhex('8D42FD83F807') in img[0x93B320:0x93B380] and 0x93B320 < cases[1] < table and
          cases.count(cases[1]) == 1, 'preview OnMessage: message 4 (index 1) has a case of its own')


def audit_data(game) -> None:
    import dsgo
    import gen_weapon_series as gen
    import sgo
    config = sgo.load(data=game.read('DEFAULTPACKAGE', 'CONFIG.SGO'))
    have = tuple(tuple((s[0], tuple(s[2]), bool(s[3])) for s in c[4]) for c in config['SoldierInit'])
    check(have == gen.SLOTS, 'gen_weapon_series SLOTS is CONFIG.SGO SoldierInit')
    table = dsgo.parse(game.read('WEAPON', 'WEAPONTABLE.SGO')).root.get('table').items
    stock = gen.stock_rows()
    check(len(table) == len(stock) == gen.STOCK_ROWS, 'stock row count')
    names = [r.items[0] for r in table]
    check([r['id'] for r in stock] == names, 'weapons.csv rows are WEAPONTABLE\'s, in order')
    levels = [r.items[4] for r in table]
    check(all(gen.shown_level(float(lv)) == gen.shown_level(r['raw']) for lv, r in zip(levels, stock)),
          'stock levels are col4 through GetRow\'s formula')
    for lang, k in (('SC', 1), ('CN', 2)):
        text = [r.items[0] for r in dsgo.parse(game.read('WEAPON', f'WEAPONTEXT.{lang}.SGO')).root.get('text_table').items]
        check(text == [r['names'][k] for r in stock], f'weapon-names-zh.csv {lang} is WEAPONTEXT.{lang}')


def main() -> int:
    audit_source()
    game_dir = gamedir.find()
    dll = os.path.join(game_dir, 'EDF.dll') if game_dir else ''
    if dll and os.path.isfile(dll):
        import room_preview_sigs
        import rootcpk
        _, img = room_preview_sigs.image(dll)
        audit_code(img)
        audit_data(rootcpk.Game(game_dir))
    else:
        print('weapon_equip_native_audit: no game; EDF.dll / Root.cpk checks skipped')
    print(f'weapon_equip_native_audit: {checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
