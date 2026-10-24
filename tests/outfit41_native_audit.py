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
    on_message = struct.unpack_from('<Q', img, sigs.VIEW_VTABLE + 8 * sigs.VIEW_SLOT_MESSAGE)[0] - base
    check(on_message == sigs.VIEW_ONMESSAGE, f'3D view vtable {sigs.VIEW_VTABLE:#x} slot {sigs.VIEW_SLOT_MESSAGE} is OnMessage')
    menu_sites = [site for site, callee in sigs.CALLS if callee == 0x0E3420]
    check([site for site, callee in sigs.CALLS if callee == sigs.VIEW_ONMESSAGE] == [0x9393D3],
          "the view constructor's direct OnMessage call is redirected too (its first build)")
    check(len(menu_sites) == 2 and all(on_message < site < 0x93C304 for site in menu_sites),
          'the menu path calls the hook redirects are inside OnMessage (where its player is known)')
    for vt, call, _outfit in sigs.ITEM_VTABLES:
        slots = [struct.unpack_from('<Q', img, vt + 8 * k)[0] - base for k in range(6)]
        check(slots[2] == call, f'item vtable {vt:#x} slot 2 is {call:#x}')
        for k in (0, 1):   # _Copy / _Move: lea rax, [rip -> vt]; mov [rdx], rax
            f = slots[k]
            check(img[f:f + 3] == b'\x48\x8d\x05' and f + 7 + struct.unpack_from('<i', img, f + 3)[0] == vt,
                  f'item vtable {vt:#x} slot {k} writes its own vtable')
    # The locked outfit change (outfit41_logic.h OfferLocked): the soldier screen's OnUpdate slot, the only read of its
    # +0x7A4 (the popup, inside OnUpdate) and its write (the screen's init), the item destructor the popup's cleanup uses.
    upd = struct.unpack_from('<Q', img, sigs.SOLDIER_VTABLE + 8 * sigs.SOLDIER_SLOT_UPDATE)[0] - base
    check(upd == sigs.SOLDIER_UPDATE, f'soldier screen vtable {sigs.SOLDIER_VTABLE:#x} slot {sigs.SOLDIER_SLOT_UPDATE} is its OnUpdate')
    check(img[0x8AB59C:0x8AB5A3] == bytes.fromhex('80bfa407000000'),
          'the popup is built in OnUpdate behind cmp byte [screen+0x7A4], 0')
    check(img[0x8A9B23:0x8A9B29] == bytes.fromhex('888ea4070000'), 'the screen init copies GS +0x300 into +0x7A4')
    check(img[0x8ABC4D:0x8ABC50] == bytes.fromhex('4c8d0d') and 0x8ABC54 + struct.unpack_from('<i', img, 0x8ABC50)[0] == 0x8E170
          and struct.unpack_from('<I', img, 0x8ABC55)[0] == 0x90, 'the popup destroys its 0x90-byte items with 0x8E170')
    # 2026-10-20 (#226 review): the member record packed by MissionSync_Begin, and the save's copy of the main block.
    mission = open(os.path.join(ROOT, 'src', 'mission.cpp'), encoding='utf-8').read()
    m = re.search(r'kSyncSig\[\]=\{([^}]*)\}', mission)
    sync = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-F]{2})', m.group(1))) if m else b''
    check(sync == img[0x7920E0:0x7920E0 + len(sync)] and len(sync) == 0x17, "mission.cpp's MissionSync_Begin bytes are EDF.dll's")
    names = [n for n in re.findall(rb'int MissionSync_Begin\(int \)', img[0x17EF1A8:0x17EF1C8])]
    check(len(names) == 1 and 0x794097 + 7 + struct.unpack_from('<i', img, 0x79409A)[0] == 0x17EF1A8 and
          0x794079 + 7 + struct.unpack_from('<i', img, 0x79407C)[0] == 0x7920E0,
          'the binding 0x7920E0 is registered as "int MissionSync_Begin(int )"')
    src = open(os.path.join(ROOT, 'src', 'outfit41.cpp'), encoding='utf-8').read()
    logic = open(os.path.join(ROOT, 'src', 'outfit41_logic.h'), encoding='utf-8').read()
    check(f'kJobData=0x{sigs.JOB_DATA:X},kJobSize=0x{sigs.JOB_SIZE:X}' in src and
          f'kMainBlock=0x{sigs.MAIN_BLOCK:X},kMainSize=0x{sigs.MAIN_SIZE:X}' in logic,
          'the job copy (+0x90 / +0x98) and the main block (GS +0x6E68, 0xDCE8) as the plugin reads them')
    check(img[0x7BCE6D:0x7BCE73] == bytes.fromhex('41b9e8dc0000') and img[0x7C2F0C:0x7C2F13] == bytes.fromhex('4881c3686e0000'),
          'the save hands 0xDCE8 bytes of GS + 0x6E68 to the job')
    check(img[0x7BDA0D:0x7BDA14] == bytes.fromhex('48898690000000') and img[0x7BDA14:0x7BDA1B] == bytes.fromhex('4889be98000000'),
          'the job keeps its copy at +0x90 (data) / +0x98 (size)')
    check(img[0x7C2232:0x7C2240] == bytes.fromhex('488b9698000000488b8e90000000') and img[0x7C2240] == 0xE8 and
          0x7C2245 + struct.unpack_from('<i', img, 0x7C2241)[0] == 0x7C1270, 'the run checksums that copy (0x7C1270)')


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
