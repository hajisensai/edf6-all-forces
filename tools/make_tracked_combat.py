"""Build the M163 Vulcan / BMPT from authored geometry and the player's native V603 contracts.

Root.cpk is read-only. build(root, kind) returns complete {Mods-relative path: bytes};
--out writes only there. Installation/removal use the same ownership ledger as other vehicles.
The bodies are deliberately authored models, not renamed stock vehicle meshes or downloaded assets.
"""
from __future__ import annotations

import argparse
import copy
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent / 'pylib'), str(HERE)]

import cockpits
import dsgo
import ledger
import vcobjects as vc

KINDS = ('vulcan', 'bmpt')
STOCK_GUNS = ('V603_FLAK_GUN01_L.SGO', 'V603_FLAK_GUN01_R.SGO')
STOCK_VEHICLE = 'V603_FLAK.SGO'
# 20 / 30 mm gameplay values. EDF updates at 60 Hz; these are gameplay weapons,
# not a claim that the simulation reproduces every real firing mode or vehicle characteristic.
GUNS = {
    'bmpt': {
        'AmmoCount': 425.0, 'AmmoDamage': 22.0, 'AmmoSpeed': 16.0, 'AmmoAlive': 120.0,
        'AmmoSize': 0.15, 'AmmoHitSizeAdjust': 0.3, 'AmmoGravityFactor': 0.15,
        'AmmoExplosion': 1.2, 'AmmoIsPenetration': 0.0, 'AmmoOwnerMove': 1.0,
        'FireInterval': 12.0, 'FireRecoil': 0.03, 'FireAccuracy': 0.004,
        'LockonTargetType': 7302.0,
    },
}
# The BMPT's second belt (vcobjects STORES BMPT_AP / BMPT_AP_R): the 2A42 is a dual-feed gun, HE-FRAG-T (GUNS) or the
# 3UBR8 APDS-T. Over GUNS['bmpt']: penetrating, no burst, faster (3UBR8 ~1120 m/s against the HE-FRAG's ~960: 16 x 1.17
# m a frame), flatter, more on one target; the AP box is the smaller (real BMPT: 850 rounds of both, 160 of them AP).
BMPT_AP_BELT = {'AmmoCount': 150.0, 'AmmoDamage': 30.0, 'AmmoSpeed': 18.7, 'AmmoGravityFactor': 0.1,
                'AmmoExplosion': 0.0, 'AmmoIsPenetration': 1.0}
NAMES = {
    'bmpt': ('BMPT 30 mm Autocannon', '\u0042\u004d\u0050\u0054 30mm\u673a\u70ae',
             '\u0042\u004d\u0050\u0054 30mm\u6a5f\u95a2\u7832', '\u0042\u004d\u0050\u0054 30mm\u6a5f\u7832'),
}


def _node(value: object) -> object:
    if isinstance(value, (tuple, list)):
        return dsgo.Node([_node(x) for x in value])
    return float(value) if isinstance(value, int) and not isinstance(value, bool) else value


def model_module(kind: str):
    if kind == 'vulcan':
        import vulcan_model
        return vulcan_model
    if kind == 'bmpt':
        import bmpt_model
        return bmpt_model
    raise ValueError(f'unsupported tracked combat vehicle: {kind}')


def _rename(r: dsgo.Node, en: str, sc: str, ja: str, cn: str) -> None:
    for lang, name in {'en': en, 'sc': sc, 'ja': ja, 'cn': cn, 'kr': en}.items():
        if f'name.{lang}' in r.names.values():
            r.set(f'name.{lang}', name)


def set_muzzles(data: bytes, points: list[tuple[str, tuple[float, float, float]]]) -> bytes:
    """Move the native locator positions without discarding other MAB lists.

    The gun's casing/effect lists are real MAB lists too; mab.mab_read only
    supports the special one-list launcher shape. Each template here already
    has the required count (one gun muzzle / four missile tubes). The authored
    point's string is the vehicle holder bone, not the V_NULL locator's name.
    """
    found = vc.mab_muzzles(data)
    if len(found) != len(points) or not 0 < len(points) <= 16:
        raise ValueError(f'muzzle count differs: native {len(found)}, authored {len(points)}')
    out = bytearray(data)
    heads, end = struct.unpack_from('<2I', data, 0x14)
    first = heads + 8 * struct.unpack_from('<H', data, 0x0C)[0]
    for (_name, _node, offset), (_bone, point) in zip(found, points):
        users = [rec for rec in range(first, end, 0x20) for field in (0xC, 0x10, 0x14)
                 if rec + struct.unpack_from('<i', data, rec + field)[0] == offset]
        if len(users) != 1:
            raise ValueError('muzzle position is shared with another locator; cannot move it independently')
        struct.pack_into('<3f', out, offset, *point)
    return bytes(out)


def set_ejection(data: bytes, position: tuple[float, float, float]) -> bytes:
    """Relocate the native casing list without altering its velocity/rotation SGO."""
    if data[:4] != b'MAB\0' or struct.unpack_from('<H', data, 0xC)[0] < 3:
        raise ValueError('gun MAB has no native casing list')
    heads, end = struct.unpack_from('<2I', data, 0x14)
    header = heads + 2 * 8
    kind, count, first = struct.unpack_from('<HHi', data, header)
    if kind != 2 or count != 1:
        raise ValueError('gun MAB casing list is not a single kind-2 locator')
    record = header + first
    offset = record + struct.unpack_from('<i', data, record + 0xC)[0]
    records = heads + 8 * struct.unpack_from('<H', data, 0xC)[0]
    users = [rec for rec in range(records, end, 0x20) for field in (0xC, 0x10, 0x14)
             if rec + struct.unpack_from('<i', data, rec + field)[0] == offset]
    if len(users) != 1:
        raise ValueError('casing position is shared with another MAB locator')
    out = bytearray(data)
    struct.pack_into('<3f', out, offset, *position)
    return bytes(out)


def _weapon_muzzles(doc: dsgo.Document, points: list[tuple[str, tuple[float, float, float]]]) -> None:
    animation = doc.root.get('animation_model')
    block = animation.items[2]
    animation.items[2] = dsgo.Blob(set_muzzles(block.data, points), block.kind)


def gun_sgo(game: vc.Game, kind: str, stock: str, side: str,
            points: list[tuple[str, tuple[float, float, float]]], belt: str | None = None) -> bytes:
    if kind == 'vulcan':
        name = vc.store_file('VULCAN_HE' if side == 'left' else 'VULCAN_AP', 2000)
        doc = dsgo.parse(vc.store_sgo(game, name))
        values = {
            'AmmoDamage': 4.0 if side == 'left' else 6.0,
            'AmmoSpeed': 17.0, 'AmmoAlive': 120.0, 'AmmoSize': 0.15,
            'AmmoHitSizeAdjust': 0.2, 'AmmoGravityFactor': 0.15,
            'AmmoExplosion': 0.6 if side == 'left' else 0.0,
            'AmmoIsPenetration': 0.0 if side == 'left' else 1.0,
            'AmmoOwnerMove': 1.0, 'FireInterval': 1.0,
            'FireBurstCount': 1.0, 'FireBurstInterval': 0.0,
            'FireAccuracy': 0.006, 'FireRecoil': 0.006, 'LockonTargetType': 7301.0,
        }
        for key, value in values.items():
            if key not in doc.root.names.values():
                raise ValueError(f'{name}: missing {key}')
            doc.root.set(key, value)
    elif belt is not None:
        # The BMPT's APDS belt: its own store file (one switch choice for both barrels, vcobjects STORES BMPT_AP*),
        # the native gun's numbers with BMPT_AP_BELT over them, the store's name.
        doc = dsgo.parse(vc.store_sgo(game, belt))
        native = dsgo.parse(game.read('WEAPON', stock)).root
        for key, value in {**GUNS[kind], **BMPT_AP_BELT}.items():
            if doc.root.get(key) is None:
                raise ValueError(f'{belt}: missing {key}')
            doc.root.set(key, value)
        for key in ('MuzzleFlash', 'MuzzleFlash_CustomParameter', 'ShellCase'):
            doc.root.set(key, copy.deepcopy(native.get(key)))
        doc.root.set('animation_model', copy.deepcopy(native.get('animation_model')))
    else:
        doc = dsgo.parse(game.read('WEAPON', stock))
        for key, value in GUNS[kind].items():
            if doc.root.get(key) is None:
                raise ValueError(f'{stock}: missing {key}')
            doc.root.set(key, value)
        _rename(doc.root, *NAMES[kind])
    if doc.root.get('xgs_scene_object_class') != 'Weapon_VehicleShoot':
        raise ValueError(f'{stock}: not a native vehicle gun')
    # Each shot is a finite one-shot cue. A continuous native loop needs a stop
    # event and cannot safely be introduced by replacing only the firing cue.
    fire = doc.root.get('FireSe')
    if isinstance(fire, dsgo.Node) and len(fire.items) == 6:
        fire.items[:] = [0.0, 'weapon_VHC_heli302_gatling', 0.55, 1.2 if kind == 'vulcan' else 0.85, 1.0, 50.0]
    _weapon_muzzles(doc, points)
    # 20/30 mm cases must leave the authored receiver, not the Kepler's
    # floating ejection port; preserve the native velocity/rotation lists.
    block = doc.root.get('animation_model').items[2]
    ejection = model_module(kind).ejection_local(game)[side]
    doc.root.get('animation_model').items[2] = dsgo.Blob(set_ejection(block.data, ejection), block.kind)
    doc.root.set('ShellCase_CustomParameter', dsgo.Node([0.4 if kind == 'vulcan' else 0.64], {0: 'scale'}))
    if kind == 'vulcan':
        reference = dsgo.parse(game.read('WEAPON', STOCK_GUNS[0])).root
        doc.root.set('MuzzleFlash', reference.get('MuzzleFlash'))
        doc.root.set('MuzzleFlash_CustomParameter', copy.deepcopy(reference.get('MuzzleFlash_CustomParameter')))
    return dsgo.compact(doc)


# Each BMPT holder's part (vehicle weapons in order): the native guns' sides, the missile tubes, then the second belt's
# sides (on the same moving barrels) and the thermobaric missile (the same tubes). The registry's (GroundVehicle.slots):
# the plugin's twin-barrel cadence (vcobjects.twin_barrels) pairs the barrels from the same table.
BMPT_SLOTS = vc.GROUND_VEHICLES['bmpt'].slots


def slot(kind: str, index: int) -> str:
    """What holder `index` of `kind` fires from: 'left' / 'right' (a gun's barrel) or 'missiles' (the tubes)."""
    return BMPT_SLOTS[index] if kind == 'bmpt' else ('left' if index == 0 else 'right')


def missile_sgo(game: vc.Game, points: list[tuple[str, tuple[float, float, float]]],
                store: str = vc.store_file('BMPT_ATGM', 4)) -> bytes:
    doc = dsgo.parse(vc.store_sgo(game, store))
    _weapon_muzzles(doc, points)
    return dsgo.compact(doc)


def collision_path(kind: str) -> str:
    if kind not in KINDS:
        raise ValueError(f'unsupported tracked combat vehicle: {kind}')
    return f'OBJECT/EDF6VC_{kind.upper()}_COLLISION.SHKT'


def animation_path(kind: str) -> str:
    return f'OBJECT/{model_module(kind).OUT_CAS}'


def _assets(game: vc.Game, kind: str):
    """Share one actual model/collision build within a complete asset transaction."""
    import tracked_combat_model as tracked
    import tracked_collision
    cache = getattr(game, '_tracked_combat_assets', None)
    if cache is None:
        cache = {}
        game._tracked_combat_assets = cache
    if kind not in cache:
        model = model_module(kind)
        arc, md, _info = model.build_with_info(game)
        assembly = tracked.Assembly(md)
        model.parts(assembly)
        shkt, binding, overrides, report = tracked_collision.build(game, kind, md, assembly.parts,
                                                                   layout=model.collision_layout())
        cache[kind] = arc, md, shkt, binding, overrides, report
    return cache[kind]


def vehicle_sgo(game: vc.Game, kind: str) -> bytes:
    """Authored model and fitted physics on the native V603 animation/controllers."""
    vehicle = vc.GROUND_VEHICLES[kind]
    model = model_module(kind)
    doc = dsgo.parse(game.read('OBJECT', STOCK_VEHICLE))
    r = doc.root
    if r.get('xgs_scene_object_class') != 'Vehicle603_Flak':
        raise ValueError('the expected native V603 class is absent')
    animation = r.get('animation_model')
    animation.items[0].items[0] = f'app:/Object/{model.OUT_ARC.lower()}'
    animation.items[0].items[1] = model.HOST_MDB
    animation.items[1] = 'app:/' + animation_path(kind).lower()
    _arc, _md, _shkt, binding, overrides, _report = _assets(game, kind)
    ragdoll = r.get('ragdoll')
    ragdoll.items[0] = 'app:/' + collision_path(kind).lower()
    ragdoll.items[1] = dsgo.Blob(binding, ragdoll.items[1].kind)
    for key, value in overrides.items():
        r.set(key, _node(value))
    setup = r.get('vehicle_setup')
    weapons = setup.items[-1]
    if len(weapons.items) != 2:
        raise ValueError('native V603 setup must have exactly two weapons')
    native = list(weapons.items)
    weapons.items = []
    for index, path in enumerate(vehicle.weapons):
        side = slot(kind, index)
        entry = copy.deepcopy(native[1 if side == 'right' else 0 if side == 'left' else min(index, 1)])
        entry.items[0] = path
        # Both classes require a BodyRecoil variant. The BMPT's barrels (either belt) keep the native gun mount's
        # (its kick on every shot: the firing shake the request already has, call_weapons.vehicle_sgo; 2026-10-21
        # 「开火动画没了」: the entity / test range copy had none); its missiles and the M163's one M61 none.
        if kind != 'bmpt' or side not in ('left', 'right'):
            entry.items[1] = _node([0.0, 0.0])
        weapons.items.append(entry)
    setting = r.get('vehicle_weapon_setting')
    if len(setting.items) != 2:
        raise ValueError('native V603 must have two weapon mounts')
    if kind == 'bmpt':
        # The missiles' holder, then the second belt's two on the guns' own moving bones and the thermobaric
        # missile's on the missiles' (BMPT_SLOTS).
        guns = [copy.deepcopy(setting.items[0]), copy.deepcopy(setting.items[1])]
        setting.items.append(_node(['cannon_main', 0.0]))
        setting.items.extend(guns)
        setting.items.append(_node(['cannon_main', 0.0]))
    else:
        # HE/AP are alternate belts in the same visible M61: the AP weapon
        # must follow the left slide too, throughout elevation and recoil.
        setting.items[1].items[0] = 'cannon_slide_l'
    camera = r.get('game_object_camera_setting')
    camera.items[0] = _node([0.0, model.CAMERA['model_eye'][1], 2.0])
    camera.items[1] = _node([0.0, model.CAMERA['third_person_height'], -model.CAMERA['third_person_distance']])
    r.set('game_object_durability', vehicle.durability)
    # Include the fitted private SHKT in the same resource transaction.
    resource = r.get('resource') if 'resource' in r.names.values() else None
    if resource is not None:
        for path in [animation.items[0].items[0], animation.items[1], ragdoll.items[0], *vehicle.weapons]:
            if path.lower() not in {str(item).lower() for item in resource.items}:
                resource.items.append(path)
    return dsgo.compact(doc)


def _points(model, game: vc.Game) -> dict[str, list[tuple[str, tuple[float, float, float]]]]:
    points = model.muzzle_local(game)
    for side in ('left', 'right'):
        if side not in points:
            raise ValueError(f'{model.__name__}: missing {side} muzzle contract')
    return points


def check(files: dict[str, bytes], kind: str, game: vc.Game | None = None) -> None:
    vehicle = vc.GROUND_VEHICLES[kind]
    model = model_module(kind)
    expected = {f'OBJECT/{vehicle.sgo}.SGO', f'OBJECT/{model.OUT_ARC}', collision_path(kind), animation_path(kind),
                *(f'WEAPON/{path.rsplit("/", 1)[1].upper()}' for path in vehicle.weapons)}
    if set(files) != expected:
        raise ValueError(f'{kind}: incomplete generated asset set {sorted(set(files) ^ expected)}')
    model.check(files[f'OBJECT/{model.OUT_ARC}'])
    r = dsgo.parse(files[f'OBJECT/{vehicle.sgo}.SGO']).root
    settings = dsgo.to_py(r.get('vehicle_weapon_setting'))
    weapons = dsgo.to_py(r.get('vehicle_setup'))[-1]
    if len(settings) != len(vehicle.weapons) or tuple(w[0] for w in weapons) != vehicle.weapons:
        raise ValueError(f'{kind}: weapon/mount/setup contracts disagree')
    if r.get('xgs_scene_object_class') != 'Vehicle603_Flak':
        raise ValueError(f'{kind}: native vehicle class changed')
    if dsgo.to_py(r.get('animation_model'))[0] != [f'app:/Object/{model.OUT_ARC.lower()}', model.HOST_MDB]:
        raise ValueError(f'{kind}: generated model is not selected')
    if r.get('animation_model').items[1] != 'app:/' + animation_path(kind).lower():
        raise ValueError(f'{kind}: private fixed-optics animation is not selected')
    for index, path in enumerate(vehicle.weapons):
        blob = files[f'WEAPON/{path.rsplit("/", 1)[1].upper()}']
        gun = dsgo.parse(blob).root
        if gun.get('AmmoCount') <= 0 or gun.get('AmmoDamage') <= 0:
            raise ValueError(f'{kind}: nonfunctional weapon {path}')
        muzzles = vc.mab_muzzles(gun.get('animation_model').items[2].data)
        expected_count = 4 if slot(kind, index) == 'missiles' else 1
        if len(muzzles) != expected_count:
            raise ValueError(f'{kind}: {path} has {len(muzzles)} muzzle locators, expected {expected_count}')
        if dsgo.write(dsgo.parse(blob)) != blob:
            raise ValueError(f'{kind}: weapon does not round-trip')
    if game is not None:
        original = dsgo.parse(game.read('OBJECT', STOCK_VEHICLE)).root
        for key in ('car_base_rigid_body', 'car_base_constraint_data', 'tank_caterpillar_animation'):
            if dsgo.to_py(r.get(key)) != dsgo.to_py(original.get(key)):
                raise ValueError(f'{kind}: native {key} changed without refitting its contract')
        import tracked_collision
        if files[animation_path(kind)] != model.animation(game):
            raise ValueError(f'{kind}: private optics animation payload differs')
        _arc, md, shkt, binding, overrides, report = _assets(game, kind)
        if files[collision_path(kind)] != shkt:
            raise ValueError(f'{kind}: fitted collision file differs')
        ragdoll = r.get('ragdoll')
        if ragdoll.items[0] != 'app:/' + collision_path(kind).lower() or ragdoll.items[1].data != binding:
            raise ValueError(f'{kind}: fitted collision/binding is not selected')
        for key, value in overrides.items():
            if dsgo.to_py(r.get(key)) != value:
                raise ValueError(f'{kind}: fitted {key} was not applied')
        tracked_collision.check(shkt, binding, md, overrides, report)
        points = _points(model, game)
        for index, path in enumerate(vehicle.weapons):
            gun = dsgo.parse(files[f'WEAPON/{path.rsplit("/", 1)[1].upper()}']).root
            block = gun.get('animation_model').items[2].data
            positions = [struct.unpack_from('<3f', block, off) for _, _, off in vc.mab_muzzles(block)]
            authored = points[slot(kind, index)]
            if any(bone != settings[index][0] for bone, _ in authored):
                raise ValueError(f'{kind}: holder uses a different moving bone from the visible barrel')
            wanted = [p for _, p in authored]
            if any(abs(a-b) > 1e-5 for got, want in zip(positions, wanted) for a,b in zip(got,want)):
                raise ValueError(f'{kind}: muzzle left the authored barrel/launcher')


def authored(root: str, kind: str) -> dict[str, bytes]:
    """Every file build() writes, checked, before its seats' cockpits are grafted into the model: the authored
    geometry's contracts (check) are made on this, and readers of the weapons alone (gen_spawn_catalog) need no
    cockpit."""
    game = vc.Game(root)
    model = model_module(kind)
    vehicle = vc.GROUND_VEHICLES[kind]
    points = _points(model, game)
    arc, _md, shkt, _binding, _overrides, _report = _assets(game, kind)
    files = {f'OBJECT/{vehicle.sgo}.SGO': vehicle_sgo(game, kind),
             f'OBJECT/{model.OUT_ARC}': arc, collision_path(kind): shkt,
             animation_path(kind): model.animation(game)}
    for stock, path, side in zip(STOCK_GUNS, vehicle.weapons[:2], ('left', 'right')):
        files[f'WEAPON/{path.rsplit("/", 1)[1].upper()}'] = gun_sgo(game, kind, stock, side, points[side])
    if kind == 'bmpt':
        name = lambda i: vehicle.weapons[i].rsplit('/', 1)[1].upper()   # noqa: E731
        files[f'WEAPON/{name(2)}'] = missile_sgo(game, points['missiles'])
        for i, (stock, side) in enumerate(zip(STOCK_GUNS, ('left', 'right')), 3):
            files[f'WEAPON/{name(i)}'] = gun_sgo(game, kind, stock, side, points[side], name(i))
        files[f'WEAPON/{name(5)}'] = missile_sgo(game, points['missiles'], name(5))
    check(files, kind, game)
    return files


def build(root: str, kind: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}: authored() with its seats' first-person cockpits grafted
    into the model (pylib/cockpits.py; the fits are pylib/cockpit_fits_plugin.py's, by the model's stem), as every
    generated vehicle's are: without a `vc_eye_<seat>` marker in the model a seat has no first person
    (src/fp_vehicle.cpp VehicleEye)."""
    return cockpits.graft_files(vc.Game(root), authored(root, kind))


def install(root: str, files: dict[str, bytes], kind: str) -> list[str]:
    led = ledger.Ledger(root)
    before = set(led.owned_by(kind))
    paths = [led.put(kind, rel, data) for rel, data in files.items()]
    led.release(kind, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str, kind: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(kind, sorted(set(led.owned_by(kind))), writer=True)


def main(argv: list[str], kind: str | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', nargs='?', default=vc.DEFAULT_GAME)
    if kind is None:
        parser.add_argument('--kind', choices=KINDS, required=True)
    parser.add_argument('--out', type=Path, help='write generated files under this output root; never installs')
    parser.add_argument('--check', action='store_true', help='generate and validate in memory only')
    parser.add_argument('--remove', action='store_true')
    args = parser.parse_args(argv)
    kind = kind or args.kind
    if args.remove:
        if args.out or args.check:
            parser.error('--remove cannot be combined with --out/--check')
        deleted, kept = remove(args.game, kind)
        for path in deleted:
            print('deleted', path)
        for path in kept:
            print('kept modified file', path)
        return 0
    files = build(args.game, kind)
    if args.out:
        for rel, data in files.items():
            path = args.out / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        print(f'{kind}: generated and checked {len(files)} files in {args.out}')
    elif args.check:
        print(f'{kind}: generated and checked {len(files)} files in memory')
    else:
        for path in install(args.game, files, kind):
            print('wrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
