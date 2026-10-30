"""An X-Morph: Defense jet's files (pylib/xm_units.py family 'jet', docs/xmorph-port.md 「固定翼」): one of the plugin's own
aircraft (pylib/vcobjects.py XM_JETS: src/jet.cpp flies it by its mark, src/playerjet_kinds.h kBoardable lets the player
board it) in its own model (pylib/xm_air_model.py: the archive EDF6VC_XM_<KEY>.MRAB and its animation EDF6VC_XM_<KEY>.CAS).

    files(game, unit) -> {Mods-relative path: bytes}   its model, animation, NPC SGO and requested twin's SGO
    holders(unit, info) -> {bone: point}              the weapon holders' bones in its model (info: model.json)

Its weapons (its gun pair's files and its stores) are the jets' (vcobjects.jet_guns: tools/make_jets.py writes them with
every jet weapon); its SGOs are vcobjects.jet_sgo's, as every jet's.
"""
from __future__ import annotations

import sgo
import vcobjects as vc
import xm_air_model as xa
import xm_units

GUN_HOLDERS = ('xm_gun_l', 'xm_gun_r')


def sgo_file(key: str) -> str:
    """Mods/OBJECT file of its NPC body (src/jet_internal.h kBodies' file)."""
    return f'EDF6VC_{key.upper()}.SGO'


def request_file(key: str) -> str:
    """Mods/OBJECT file of its requested twin (its request's vehicle: tools/calls.py Call.vehicle)."""
    return f'EDF6VC_FLY_{key.upper()}.SGO'


def holders(unit: xm_units.Unit, info: dict) -> dict[str, tuple[float, float, float]]:
    """Each weapon holder's point (model frame), vcobjects.JETS weapon_bones' order: the gun pair at its muzzles (left,
    right), a unit with no gun's (the jets' own guns) either side of the nose; each store's (xm_<arm>) the middle of its
    weapons' muzzles."""
    pts = info['attachments']
    (x0, y0, _z0), (x1, y1, z1) = info['bounds']
    if unit.air['guns']:
        left, right = (tuple(pts[m]) for m in unit.air['guns'])
    else:
        mid = (y0 + y1) / 2
        left, right = (x1 * 0.1, mid, z1), (x0 * 0.1, mid, z1)
    out = dict(zip(GUN_HOLDERS, (left, right)))
    muzzles = {w['name']: w['muzzles'] for w in info['weapons']}
    for a in unit.air['stores']:
        ms = [pts[m] for w in a.weapons for m in muzzles[w]]
        if not ms:
            raise ValueError(f'{unit.key} {a.key}: no muzzles of {a.weapons}')
        out[f'xm_{a.key}'] = tuple(sum(p[k] for p in ms) / len(ms) for k in range(3))
    return {k: tuple(float(c) for c in v) for k, v in out.items()}  # type: ignore[misc]


def check(files: dict[str, bytes], unit: xm_units.Unit) -> None:
    """Its NPC SGO and twin's: its mark, model, animation and weapons (vcobjects.JETS), the twin requested."""
    key = unit.key
    for name, file in ((xm_units.jet_name(key), sgo_file(key)), (xm_units.jet_request(key), request_file(key))):
        jet = vc.JETS[name]
        _, m = sgo.read(files[f'OBJECT/{file}'])
        if (float(vc._value(m['mission_setup'][1][0])) != unit.air['mark']
                or float(vc._value(m['game_object_durability'])) != unit.durability):
            raise ValueError(f'{file}: mark / durability')
        if m['animation_model'][0] != list(jet.model) or m['animation_model'][1] != jet.cas:
            raise ValueError(f'{file}: model {m["animation_model"][:2]}')
        if [w[0] for w in m['mission_setup'][3] if 'fuel' not in w[0]] != list(jet.weapons):
            raise ValueError(f'{file}: weapons')
        if ('vehicle_setup' in m) != jet.requested:
            raise ValueError(f'{file}: vehicle_setup')
    want = {f'OBJECT/{n}' for n in (xa.archive_name(key), xa.animation_name(key), sgo_file(key), request_file(key))}
    if set(files) != want:
        raise ValueError(f'{key}: files {sorted(set(files) ^ want)}')


def files(game, unit: xm_units.Unit) -> dict[str, bytes]:  # noqa: ANN001 - rootcpk.Game
    """Everything the unit installs (Mods-relative) besides its weapons: its model and animation, its NPC SGO and its
    requested twin's. Its model folder must be here (xm_units.deployed): the caller says so and builds nothing where it
    is not."""
    import obj_model as om
    folder = om.model_dir(xm_units.folder(unit.key))
    if folder is None:
        raise FileNotFoundError(f'{unit.key}: no model folder {xm_units.folder(unit.key)}')
    jet = vc.JETS[xm_units.jet_name(unit.key)]
    if (jet.file, jet.model[1], jet.cas) != (xa.archive_name(unit.key), xa.mdb_name(unit.key),
                                             'app:/object/' + xa.animation_name(unit.key).lower()):
        raise ValueError(f'{unit.key}: vcobjects.JETS names another model')
    info = xa.read_info(folder)
    arc, anim, _info = xa.build_archive(game, folder, unit.key, holders(unit, info))
    out = {f'OBJECT/{xa.archive_name(unit.key)}': arc, f'OBJECT/{xa.animation_name(unit.key)}': anim,
           f'OBJECT/{sgo_file(unit.key)}': vc.jet_sgo(game, xm_units.jet_name(unit.key)),
           f'OBJECT/{request_file(unit.key)}': vc.jet_sgo(game, xm_units.jet_request(unit.key))}
    check(out, unit)
    return out


def model_inputs(key: str) -> list[str]:
    """The model folder's files the build reads (pylib/buildcache.py hashes them)."""
    import obj_model as om
    folder = om.model_dir(xm_units.folder(key))
    return [] if folder is None else xa.model_files(folder)
