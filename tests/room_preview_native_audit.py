"""The room / HQ 3D preview against the game's own files (read only): src/room_preview_signatures.inc is EDF.dll's code,
the vtable slots the hooks chain hold the functions the docs name, the OnMessage jump table sends 3 / 5 / 8 / 9 where
room_preview.cpp expects, and src/room_preview_logic.h's tables are the game data's (the AI templates' soldier_color,
the WeaponTable names, CONFIG.SGO's menu paths, tools/menu_preview.py's generated names). Without the game: skipped."""
from __future__ import annotations

import os
import re
import struct
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


def logic_text() -> str:
    with open(os.path.join(ROOT, 'src', 'room_preview_logic.h'), encoding='utf-8') as f:
        return f.read()


def audit_code(img: bytes, base: int) -> None:
    import room_preview_sigs as sigs
    with open(sigs.INC, encoding='utf-8') as f:
        check(f.read() == sigs.render(img), 'room_preview_signatures.inc is EDF.dll\'s code (rerun tools/room_preview_sigs.py)')
    for vt, slot, fn in sigs.SLOTS:
        held = struct.unpack_from('<Q', img, vt + slot * 8)[0] - base
        check(held == fn, f'vtable {vt:#x} slot {slot} holds {fn:#x} (holds {held:#x})')
    table = 0x93C2E4
    want = {3: 0x93B376, 5: 0x93C202, 6: 0x93BCA5, 8: 0x93C24B, 9: 0x93C255}
    for msg, at in want.items():
        check(struct.unpack_from('<i', img, table + (msg - 3) * 4)[0] == at, f'OnMessage id {msg} -> {at:#x}')
    for site, vt in ((0x93B682, 0x180FDB0), (0x93BF8B, 0x180FE20)):
        disp = struct.unpack_from('<i', img, site + 3)[0]
        check(site + 7 + disp == vt, f'the job at {site:#x} is the std::function {vt:#x}')
    check(struct.unpack_from('<i', img, 0x938A77 + 3)[0] + 0x938A77 + 7 == 0x20B2890, 'GameStatus global')


def audit_tables(game) -> None:
    import dsgo
    import menu_preview
    import sgo
    import support_loadout
    text = logic_text()
    rows = [r[0] for r in dsgo.to_py(dsgo.parse(game.read('WEAPON', 'WEAPONTABLE.SGO')).root.get('table'))]
    for name in set(re.findall(r'L"((?:[ap]Weapon|h|RocketLauncher|LAZR)[A-Za-z0-9]*)"', text)):
        check(name in rows, f'WeaponTable has {name}')
    _, config = sgo.read(game.read('DEFAULTPACKAGE', 'CONFIG.SGO'))
    menus = [p for row in sgo.plain(config['SoldierInit']) for p in row[5][1]]
    for const in ('kMenuRangerStock', 'kMenuWingDiver', 'kMenuFencer'):
        m = re.search(const + r'=L"([^"]+)"', text)
        check(bool(m) and m.group(1) in menus, f'{const} is spelled as CONFIG.SGO')
    for const, rel in (('kMenuRanger', 'MENUOBJECT/EDF6VC_MENU_RANGER.SGO'), ('kMenuRangerLeader', 'MENUOBJECT/EDF6VC_MENU_RANGER_L.SGO')):
        m = re.search(const + r'=L"app:/([^"]+)"', text)
        check(bool(m) and m.group(1) == rel and rel in menu_preview.rels(), f'{const} is tools/menu_preview.py\'s {rel}')
    audit_colours(game, text, support_loadout, dsgo)


def stock_rows(text: str) -> list[list[float]]:
    body = text[text.index('kKindStock[kSupportWeaponCount]={'):]
    body = body[:body.index('};')]
    named = {'kRangerStock': floats(text, 'kRangerStock')}
    out = []
    for token in re.findall(r'kRangerStock|\{\{\{\{[^\n]*?\}\}\}\}', body):
        out.append(named[token] if token in named else [float(x) for x in re.findall(r'[0-9.]+(?=f)', token)])
    return out


def floats(text: str, const: str) -> list[float]:
    line = re.search(r'StockColours ' + const + r'\{([^\n]+)', text).group(1)
    return [float(x) for x in re.findall(r'[0-9.]+(?=f)', line)]


def template_colours(game, dsgo, template: str, mesh: str | None) -> list[float]:
    doc = dsgo.parse(game.read('OBJECT', template + '.SGO'))
    got = {}
    for selectors, rgba in dsgo.to_py(doc.root.get('soldier_color')):
        for sel, channel in selectors:
            if (mesh is None and not isinstance(sel, str)) or sel == mesh:
                got[channel] = rgba
    return got['change_color0'] + got['change_color1']


def near(a: list[float], b: list[float]) -> bool:
    return len(a) == len(b) and all(abs(x - y) < 5e-4 for x, y in zip(a, b))


def audit_colours(game, text: str, support_loadout, dsgo) -> None:
    rows = stock_rows(text)
    check(len(rows) == len(support_loadout.KINDS), 'kKindStock has a row a kind')
    templates = {n: t for n, _, t in support_loadout.KINDS}
    for (kind, _, template), row in zip(support_loadout.KINDS, rows):
        if not support_loadout.has_looks(kind):
            # The X-Morph infantry has no Root.cpk template: it is previewed as the rifle Ranger, so its row is the rifle's.
            template = templates['rifle']
        check(near(row, template_colours(game, dsgo, template, None)), f'stock colours of {kind} are {template}\'s')
    head = template_colours(game, dsgo, 'N601_COMMON_RANGER_FL_LEADER', 'p609_proto_ranger_common_head')
    body = template_colours(game, dsgo, 'N601_COMMON_RANGER_AF_LEADER', 'p609_proto_ranger_common_body')
    check(near(floats(text, 'kRangerLeaderHead'), head), 'kRangerLeaderHead is the leaders\' head colour')
    check(near(floats(text, 'kRangerLeaderBody'), body), 'kRangerLeaderBody is the Rifle leader\'s body colour')


def main() -> int:
    game_dir = gamedir.find()
    dll = os.path.join(game_dir, 'EDF.dll') if game_dir else ''
    if not dll or not os.path.isfile(dll):
        print('room_preview_native_audit: no game; skipped')
        return 0
    import room_preview_sigs as sigs
    import rootcpk
    pe, img = sigs.image(dll)
    audit_code(img, pe.OPTIONAL_HEADER.ImageBase)
    audit_tables(rootcpk.Game(game_dir))
    print(f'room_preview_native_audit: {checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
