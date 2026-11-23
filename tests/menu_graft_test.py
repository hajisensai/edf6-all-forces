"""The merged menu models of the loaded vehicles (pylib/menu_graft.py, docs/unified-model-re.md §3.1).

Offline: the names the plugin builds (src/menu_callouts.h: the sidecar suffix, the header) match the tool's; the
callouts text round-trips; pylon_xs' layout; graft_pure.texture_members takes texture members only (a store's round
archive names its model and its texture alike); support_loadout.menu_files names the merged files of a mech / jet, none
of a tank.

With the game installed (Root.cpk only read; nothing written to the game), every generated variant kind: each of the
eight mech frames carrying a full set, every mech weapon in every slot it fits, an empty slot, each jet body with five
pylons and every jet store on some pylon, an even pylon count, a tank. For each one:
  - battle keys byte for byte: the unified file with the menu keys taken out is the battle file (the generator's, with
    the menu keys a jet's base already has taken out the same way), and the battle model key is the stock body's;
  - only the menu key names the merged archive (OBJECT/<stem>.MRAB, menu_model_ref), its CAS / MAB the battle's;
  - the archive: one model, named as the key names it, every texture it names a member, folder order kept, no model
    member taken along as a texture;
  - one grafted bone (or skinned set) per filled slot / pylon;
  - the callouts parse back with the tags the plugin reads, each point inside the merged model's box.
"""
from __future__ import annotations

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import dsgo  # noqa: E402
import graft_pure  # noqa: E402
import mech_parts  # noqa: E402
import menu_graft as mg  # noqa: E402
import menu_model as mm  # noqa: E402
import menu_preview  # noqa: E402
import sgo  # noqa: E402
import support_loadout as sl  # noqa: E402
from mdb import Rab, RabFile, folder_order_ok, mdb_read, rab_read  # noqa: E402

checks = 0


def check(ok: bool, what: str) -> None:
    global checks
    if not ok:
        raise AssertionError(what)
    checks += 1


def check_offline() -> None:
    with open(os.path.join(ROOT, 'src', 'menu_callouts.h'), encoding='utf-8') as f:
        hdr = f.read()
    check(f'kCalloutsHeader[] = "{mg.HEADER}"' in hdr, 'the header the plugin reads is the one the tool writes')
    check(f'kCalloutsSuffix[] = L"{mg.CALLOUTS_SUFFIX}"' in hdr, 'the sidecar suffix the plugin builds')
    check(f'kPylonCallouts = {sl.PYLONS_MOST}' in hdr, 'as many pylon callouts as pylons')
    f = 'EDF6VC_LO_MECH_6000001648C3005B.SGO'
    check(mg.archive_rel(f) == 'OBJECT/EDF6VC_LO_MECH_6000001648C3005B.MRAB', mg.archive_rel(f))
    check(mg.callouts_rel(f) == 'OBJECT/EDF6VC_LO_MECH_6000001648C3005B_CALLOUTS.TXT', mg.callouts_rel(f))
    check(mg.menu_model_ref(f) == ['app:/object/edf6vc_lo_mech_6000001648c3005b.mrab',
                                   'edf6vc_lo_mech_6000001648c3005b.mdb'], f'{mg.menu_model_ref(f)}')
    pts = {'weapon0': (1.0, 2.5, -3.25), 'frame': (0.0, 5.0, 0.0), 'pylon3': (-6.125, 0.5, 1.0)}
    text = mg.callouts_text(pts)
    check(text.startswith(mg.HEADER.encode() + b'\r\n') and text.endswith(b'\r\n') and b'\n' not in text.replace(
        b'\r\n', b''), 'callouts: CRLF lines after the header')
    check(mg.parse_callouts(text) == pts, 'callouts round-trip')
    try:
        mg.parse_callouts(b'weapon0 1 2 3\r\n')
        check(False, 'a file without the header is refused')
    except mg.GraftError:
        check(True, '')
    for n in range(sl.PYLONS_MOST + 1):
        xs = mg.pylon_xs(n, 10.0)
        check(len(xs) == n, f'{n} pylons: {xs}')
        check(sorted(xs) == sorted(-x for x in xs), f'{n} pylons mirrored: {xs}')
        check((n % 2 == 1) == (0.0 in xs), f'{n} pylons: the centre line only for an odd count')
        check(all(abs(x) <= 10.0 * mg.PYLON_SPREAD[1] + 1e-9 for x in xs), f'{n} pylons inside the spread')
    rab = Rab(0, ['TEXTURE', 'MODEL'], [RabFile('bullet_icbm01.dds', 0, 0, b'', 0),
                                        RabFile('bullet_icbm01.lod.dds', 0, 0, b'', 0),
                                        RabFile('bullet_icbm01.mdb', 1, 0, b'', 0)])
    check([x.name for x in graft_pure.texture_members(rab, 'bullet_icbm01.dds')] ==
          ['bullet_icbm01.dds', 'bullet_icbm01.lod.dds'], 'texture_members: the textures, not the same-stem model')
    mech = sl.Loadout('MECH', mech=mech_parts.Mech(mech_parts.REQUESTS[0].token, weapons=('MACHINEGUN', '', '', '')))
    jet = sl.Loadout('STRIKE', pylons=[('AAM_S', 2)])
    tank = sl.Loadout('TANK', pylons=[('HE', 10)])
    for l, want in ((mech, True), (jet, True), (tank, False)):
        f = sl.vehicle_file(l)
        got = sl.menu_files(f)
        check(got == ([mg.archive_rel(f), mg.callouts_rel(f)] if want else []), f'{f}: menu_files {got}')


def strip_menu(data: bytes) -> bytes:
    """`data` with every vehicle menu key taken out, written as the battle loader reads it. A DSGO's string pool is
    the whole file's (the menu keys' strings in it too): written again from what the battle tree references."""
    if data[:4] == b'DSGO':
        doc = dsgo.parse(data)
        kept = [(i, n) for i, n in sorted(doc.root.names.items()) if n not in mm.VEHICLE_ADDED]
        doc.root = dsgo.Node([doc.root.items[i] for i, _ in kept], {k: n for k, (_, n) in enumerate(kept)})
        return dsgo.write(dsgo.parse(dsgo.write(doc)))
    version, members = sgo.read(data)
    return sgo.write_depth_first(version, {k: v for k, v in members.items() if k not in mm.VEHICLE_ADDED})


def variants() -> list[sl.Loadout]:
    full = max((r for r in mech_parts.REQUESTS if mech_parts.NONE not in r.slots), key=lambda r: r.level)
    out = [sl.Loadout('MECH', mech=mech_parts.Mech(full.token, frame=fr.token, weapons=tuple(full.slots)))
           for fr in mech_parts.FRAMES]
    top = max(mech_parts.REQUESTS, key=lambda r: r.level)
    per_slot = [[w.token for w in mech_parts.WEAPON_BY.values() if mech_parts.allowed(w, s, top)]
                for s in range(len(mech_parts.SLOTS))]
    for j in range(max(len(x) for x in per_slot)):   # every weapon in every slot it fits, frames in turn
        weapons = tuple(x[j % len(x)] for x in per_slot)
        out.append(sl.Loadout('MECH', mech=mech_parts.Mech(top.token, frame=mech_parts.FRAMES[j % len(
            mech_parts.FRAMES)].token, weapons=weapons)))
    out.append(sl.Loadout('MECH', mech=mech_parts.Mech(top.token, weapons=(per_slot[0][0], '', per_slot[2][0], ''))))
    stores = [s[1] for s in sl.LOAD_STORES if s[4]]
    jets = [b for b, (_, most, tank) in sl.BODIES.items() if not tank and b != 'MECH']
    for i, b in enumerate(jets):
        most = sl.BODIES[b][1]
        out.append(sl.Loadout(b, pylons=[(stores[(i + k) % len(stores)], 2) for k in range(most)]))
    out.append(sl.Loadout('FIGHTER', pylons=[('AAM_M', 2), ('AAM_M', 2)]))
    out.append(sl.Loadout('TANK', pylons=[('HE', 10)]))
    return out


def check_real(root: str, game: object) -> None:
    captured: dict[str, bytes] = {}
    real_unify = menu_preview.unify_vehicle

    def unify(g, name, data, root_=None, menu=None):
        captured[name] = data
        return real_unify(g, name, data, root_, menu)
    menu_preview.unify_vehicle = unify
    try:
        for l in variants():
            check_variant(root, game, l, captured)
    finally:
        menu_preview.unify_vehicle = real_unify


def check_variant(root: str, game: object, l: sl.Loadout, captured: dict[str, bytes]) -> None:
    f = sl.vehicle_file(l)
    one = sl._build_one(root, game, f, {})
    out = one['OBJECT/' + f]
    battle = captured[f]
    check(strip_menu(out) == strip_menu(battle), f'{f}: battle keys byte for byte')
    m = mm.members_of(out)
    b = mm.members_of(battle)
    check(m['animation_model'] == b['animation_model'], f'{f}: the battle model key untouched')
    if l.body == 'TANK':
        check(sorted(one) == sorted(['OBJECT/' + f] + [k for k in one if k.startswith('WEAPON/')]),
              f'{f}: a tank has no merged model: {sorted(one)}')
        check(m[mm.MENU_MODEL][0] == m['animation_model'][0][:2], f'{f}: a tank shows its own model')
        return
    ref = mg.menu_model_ref(f)
    check(m[mm.MENU_MODEL][0] == ref, f'{f}: the menu key names the merged archive: {m[mm.MENU_MODEL][0]}')
    check(m[mm.MENU_MODEL][2] == m['animation_model'][2], f'{f}: the battle MAB for the menu')
    check(mg.archive_rel(f) in one and mg.callouts_rel(f) in one, f'{f}: {sorted(one)}')
    rab = rab_read(one[mg.archive_rel(f)])
    models = [x for x in rab.files if x.name.lower().endswith('.mdb')]
    check([x.name for x in models] == [ref[1]], f'{f}: one model, the key\'s: {[x.name for x in models]}')
    check(folder_order_ok(rab), f'{f}: folder order')
    md = mdb_read(models[0].data)
    names = {x.name.lower() for x in rab.files}
    for t in md.textures:
        check(t.filename.lower() in names, f'{f}: texture {t.filename} is a member')
    bones = [md.name_of(x.name) or '' for x in md.bones]
    points = mg.parse_callouts(one[mg.callouts_rel(f)])
    lo, hi = mg._box(mg.model_points(md))
    for tag, p in points.items():
        check(all(lo[i] - 0.5 <= p[i] <= hi[i] + 0.5 for i in range(3)), f'{f}: {tag} {p} outside {lo} {hi}')
    if l.mech is not None:
        filled = [s for s, t in enumerate(l.mech.weapons) if t]
        for s in range(len(mech_parts.SLOTS)):
            have = any(x == f'edf6vc_slot{s}' or x.startswith(f'edf6vc_slot{s}_') for x in bones)
            check(have == (s in filled), f'{f}: slot {s} grafted {have}')
        check(sorted(points) == sorted(['weapon0', 'weapon1', 'weapon2', 'weapon3', 'frame', 'core']),
              f'{f}: mech callouts {sorted(points)}')
    else:
        n = len(l.pylons)
        check(sorted(x for x in bones if re.fullmatch(r'edf6vc_pylon\d', x)) == [f'edf6vc_pylon{i}' for i in range(n)],
              f'{f}: one store a pylon: {bones}')
        check(sorted(points) == [f'pylon{i}' for i in range(n)], f'{f}: jet callouts {sorted(points)}')


def main() -> int:
    check_offline()
    try:
        import gamedir
        import rootcpk
        root = gamedir.find_or_dev()
        game = rootcpk.Game(root)
    except Exception as e:   # no game on this machine (CI)
        print(f'menu_graft_test: no game ({e}); the real variants not built')
        game = None
    if game is not None:
        check_real(root, game)
    print(f'menu_graft_test: {checks} checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
