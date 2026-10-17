"""The EDF4.1 outfit against the shipped files and, with the game, its own EDF.dll and Root.cpk (read only).

Always: src/outfit41_logic.h's paths are the shipped registry's (edf41port/outfits.json) for every class, every shipped
file a class names is in the registry with its SHA-256, the battle / menu SGOs name the class's archive and its models,
and none of the names is a stock file's.
With the game: src/outfit41_signatures.inc is EDF.dll's code, every call site the hooks redirect calls what the plugin
expects, the five stock popup items' vtables hold their calls (and their _Copy / _Move write their own vtable), CONFIG.SGO
gives each class four outfits (the save's -1..3), each shipped SGO is its stock base SGO but animation_model[0], and no
shipped name exists in the game's archives."""
from __future__ import annotations

import hashlib
import json
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


def logic_paths() -> list[tuple[str, str, str, list[str]]]:
    with open(os.path.join(ROOT, 'src', 'outfit41_logic.h'), encoding='utf-8') as f:
        text = f.read()
    body = text[text.index('kFiles[kClasses]={'):]
    body = body[:body.index('};')]
    rows = re.findall(r'\{L"(\w+)",L"([^"]+)",L"([^"]+)",\s*\{L"([^"]+)",L"([^"]+)",L"([^"]+)"\}\}', body)
    return [(k, b, m, [x.replace('\\\\', '/') for x in d]) for k, b, m, *d in rows]


def audit_registry() -> dict:
    import edf41_outfits as eo
    import dsgo
    import mdb
    import sgo
    reg = eo.registry()
    rows = logic_paths()
    by_class = {c['soldier_class']: c for c in reg['classes']}
    check(len(rows) == 4 and set(by_class) == {0, 1, 2, 3}, 'four classes in the logic and in the registry')
    for cls, (key, battle, menu, on_disk) in enumerate(rows):
        c = by_class.get(cls)
        if not c:
            continue
        check(battle.split(':/', 1)[1].upper() == c['battle'], f'{key}: the battle path is the registry\'s {c["battle"]}')
        check(menu.split(':/', 1)[1].upper() == c['menu'], f'{key}: the menu path is the registry\'s {c["menu"]}')
        check(sorted(on_disk) == sorted(c['files']), f'{key}: the files checked in Mods are the shipped ones')
        for rel in c['files']:
            data = eo.read(rel)
            check(hashlib.sha256(data).hexdigest() == reg['files'].get(rel), f'{rel}: shipped as registered')
        arch = next(r for r in c['files'] if r.endswith('.MRAB'))
        models = [f.name for f in mdb.rab_read(eo.read(arch)).files if f.name.lower().endswith('.mdb')]
        am = dsgo.to_py(dsgo.parse(eo.read(c['battle'])).root.get('animation_model'))[0]
        check(am[0].split(':/', 1)[1].upper() == arch and am[1] in models and [x[0] for x in am[2]] == models[1:],
              f'{c["battle"]}: animation_model[0] names the archive, the body and the LODs')
        _v, mm = sgo.read(eo.read(c['menu']))
        check(mm['animation_model'][0][0].split(':/', 1)[1].upper() == arch and mm['animation_model'][0][1] == models[0],
              f'{c["menu"]}: animation_model[0] names the archive and the body')
    return reg


def audit_code(img: bytes, base: int) -> None:
    import outfit41_sigs as sigs
    with open(sigs.INC, encoding='utf-8') as f:
        check(f.read() == sigs.render(img), 'outfit41_signatures.inc is EDF.dll\'s code (rerun tools/outfit41_sigs.py)')
    for site, callee in sigs.CALLS:
        check(img[site] == 0xE8 and site + 5 + struct.unpack_from('<i', img, site + 1)[0] == callee,
              f'{site:#x} calls {callee:#x}')
    for vt, call, _outfit in sigs.ITEM_VTABLES:
        slots = [struct.unpack_from('<Q', img, vt + 8 * k)[0] - base for k in range(6)]
        check(slots[2] == call, f'item vtable {vt:#x} slot 2 is {call:#x}')
        for k in (0, 1):   # _Copy / _Move: lea rax, [rip -> vt]; mov [rdx], rax
            f = slots[k]
            check(img[f:f + 3] == b'\x48\x8d\x05' and f + 7 + struct.unpack_from('<i', img, f + 3)[0] == vt,
                  f'item vtable {vt:#x} slot {k} writes its own vtable')


def audit_game(game, reg: dict) -> None:
    import dsgo
    import edf41_outfits as eo
    import outfit41
    import sgo
    _, config = sgo.read(game.read('DEFAULTPACKAGE', 'CONFIG.SGO'))
    rows = sgo.plain(config['SoldierInit'])
    check(all(len(r[2]) == 4 and len(r[5][1]) == 4 for r in rows), 'CONFIG.SGO: four outfits a class (the save keeps -1..3)')
    have = {f'{d}/{n}'.upper() for d, n in game.cpk.index}
    for c in reg['classes']:
        spec = outfit41.SPECS[c['class']]
        for rel in c['files']:
            check(rel not in have, f'{rel} is not a stock name')
        stock = dsgo.to_py(dsgo.parse(game.read(*spec.base_sgo.split('/'))).root)
        ours = dsgo.to_py(dsgo.parse(eo.read(c['battle'])).root)
        stock['animation_model'] = stock['animation_model'][1:]
        ours['animation_model'] = ours['animation_model'][1:]
        check(stock == ours, f'{c["battle"]} is {spec.base_sgo} but animation_model[0]')
        _v, sm = sgo.read(game.read(*spec.base_menu.split('/')))
        _v, om = sgo.read(eo.read(c['menu']))
        sm['animation_model'] = sm['animation_model'][1:]
        om['animation_model'] = om['animation_model'][1:]
        check(sgo.plain(sm) == sgo.plain(om), f'{c["menu"]} is {spec.base_menu} but animation_model[0]')


def main() -> int:
    reg = audit_registry()
    game_dir = gamedir.find()
    dll = os.path.join(game_dir, 'EDF.dll') if game_dir else ''
    if dll and os.path.isfile(dll):
        import outfit41_sigs  # noqa: F401
        import room_preview_sigs
        import rootcpk
        pe, img = room_preview_sigs.image(dll)
        audit_code(img, pe.OPTIONAL_HEADER.ImageBase)
        audit_game(rootcpk.Game(game_dir), reg)
    else:
        print('outfit41_native_audit: no game; EDF.dll / Root.cpk checks skipped')
    print(f'outfit41_native_audit: {checks - failed}/{checks} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
