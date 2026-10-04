"""Writes the submarine carrier (src/subcarrier.cpp SubLaunch, docs/subcarrier-re.md) into <game>/Mods:

  Mods/OBJECT/EDF6VC_SUB.MRAB          the mission object EV603_MARINE's model (the 潜水母艦 of M082 / M092 /
                                       M123) at its own size (1664 m), root bone renamed `mdl`, `body` levelled (tools/jet_models.py
                                       SUB_MODELS); every other member of the stock archive byte-identical
  Mods/OBJECT/EDF6VC_SUB_CARRIER.SGO   a Vehicle506_Helicopter body with that model (pylib/vcobjects.py jet_sgo:
                                       'edf6tr_sub_carrier_mission', mark 7101, HP 30000 (the plugin raises it
                                       to SubHullHp), the hull's box)
  Mods/WEAPON/EDF6VC_JET_GUN_L / _R.SGO  the jets' guns (vcobjects.jet_guns), which its turrets fire; the same
                                       bytes tools/make_jets.py writes

All of it is built in memory first, then written atomically and recorded in the ledger as this tool's
(pylib/ledger.py); --remove releases it, so the guns stay while make_jets or the test range still use them,
and the sub's own files while testrange/sub_vs_mothership.py's mission does. No shared table
(Mods/WEAPON/WEAPONTABLE.SGO, ...) is touched. The plugin preloads the sub only while both files and both guns
are there.

  python tools/make_sub.py [game dir]            write / refresh
  python tools/make_sub.py [game dir] --remove   release them (deleted unless another tool still needs one)
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import jet_models  # noqa: E402
import ledger  # noqa: E402
import vcobjects as vc  # noqa: E402

JET = 'edf6tr_sub_carrier_mission'
SGO_FILE = 'EDF6VC_SUB_CARRIER.SGO'
MODEL_FILE = 'EDF6VC_SUB.MRAB'
OWNER = 'sub'   # pylib/ledger.py
# Hull box (half extents) the plugin's SubFrame and the SGO agree on: its bottom is kHullBottom metres under
# the body origin (src/subcarrier.cpp; negative: the box is the slab under the deck, over the origin).
HULL_BOTTOM = -163.08
# The seat weapons as src/subcarrier.cpp kSystems takes them: each part's weapon is the one whose barrel is at the
# part's bone, and it must be the holder the 506's fire bytes fire for it (0x2020 holders 0 and 1, 0x2021 holder 2)
# and homing or not as the part fires it. The plugin checks the same at run time and turns a part off that does not
# match; this keeps the SGO from being written that way.
PLUGIN_WEAPONS = (('gunA_tilt_l', False), ('gunB_tilt_l', False), ('missle_l', True))


def check_sgo(data: bytes) -> None:
    """The written SGO: the sub's mark, HP, model, box and weapon bones, each bone one the model has."""
    import sgo
    v = sgo.load(data=data)
    jet = vc.JETS[JET]
    assert v['mission_setup'][1][0] == jet.mark, 'mark'
    assert v['game_object_durability'] == jet.durability, 'durability'
    assert v['animation_model'][0] == list(jet.model), v['animation_model'][0]
    assert v['animation_model_bone_mapping'] == ['mdl', 'body']
    box = v['heli_rigid_body']
    assert all(abs(a - b) < 1e-3 for got, want in zip(box[:2], jet.rigid) for a, b in zip(got, want)), box   # float32
    assert abs((box[0][1] - box[1][1]) + HULL_BOTTOM) < 0.02, 'hull bottom'
    bones = [b for b, _ in v['vehicle_weapon_setting']]
    assert bones == list(jet.weapon_bones) + ['body'], bones
    assert len(jet.weapon_bones) == len(jet.weapons)
    assert [w[0] for w in v['mission_setup'][3]][:3] == list(jet.weapons)
    assert tuple(jet.weapon_bones) == tuple(b for b, _ in PLUGIN_WEAPONS), (jet.weapon_bones, PLUGIN_WEAPONS)
    homing = tuple(w == vc._MISSILE for w in jet.weapons)
    assert homing == tuple(h for _, h in PLUGIN_WEAPONS), ('holder order', jet.weapons)


def check_model(arc: bytes) -> None:
    """Every bone the SGO hangs something on is in the model."""
    from mdb import mdb_read, rab_read
    md = mdb_read(next(f for f in rab_read(arc).files if f.name.lower() == 'ev603_marine.mdb').data)
    names = {md.name_of(b.name) for b in md.bones}
    missing = ({'mdl', 'body'} | set(vc.JETS[JET].weapon_bones)) - names
    assert not missing, f'model lacks {missing}'


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked, from the game's Root.cpk (only read)."""
    game = vc.Game(root)
    out = {f'WEAPON/{name}': data for name, data in vc.jet_guns(game).items()}
    arc = jet_models.build(game, jet_models.SUB_MODELS)[MODEL_FILE]     # checked by jet_models.check
    check_model(arc)
    data = vc.jet_sgo(game, JET)
    check_sgo(data)
    out[f'OBJECT/{MODEL_FILE}'] = arc
    out[f'OBJECT/{SGO_FILE}'] = data
    return out


def install(root: str, files: dict[str, bytes]) -> list[str]:
    led = ledger.Ledger(root)
    return [led.put(OWNER, rel, data) for rel, data in files.items()]


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files (and the sub's own from before the ledger): (deleted, kept changed)."""
    led = ledger.Ledger(root)
    own = {ledger.key(f'OBJECT/{n}') for n in (SGO_FILE, MODEL_FILE)}
    return led.release(OWNER, sorted(set(led.owned_by(OWNER)) | own), writer=True)


def main(argv: list[str]) -> int:
    args = [a for a in argv if not a.startswith('--')]
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    for path in install(root, build(root)):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
