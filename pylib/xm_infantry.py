"""The X-Morph infantry (pylib/xm_units.py family 'infantry') as an EDF6 support soldier kind (src/support_call.h
SupportWeapon::xmInfantry, the map's support bar and the transports' squads): the Ranger's soldier SGOs (member and
leader: class, CAS, ragdoll, voice, the weapon the plugin hands the kind) on the X-Morph powered suit retargeted onto
the Ranger's skeleton (pylib/xm_rig.py), the suit's gun left out (Rig.trim: it is one piece with its fist; the soldier
holds the kind's EDF weapon).

    build(game, key) -> {Mods path: bytes}    the MRAB (with its LODs) and the two soldier SGOs
    check(files, key)

The plugin offers the kind only where the member SGO is installed (src/support_soldier.cpp: no model folder, no file,
no kind; CLAUDE.md: no stand-in soldier).
"""
from __future__ import annotations

import xm_units

STOCK = ('N601_COMMON_RANGER_AF', 'N601_COMMON_RANGER_AF_LEADER')   # Root.cpk OBJECT: the Ranger (assault rifle)


def model_file(key: str) -> str:
    return f'EDF6VC_{key.upper()}.MRAB'


def body_files(key: str) -> tuple[str, str]:
    """(member, leader) soldier SGO files (Mods/OBJECT): src/support_soldier.cpp kBodies' row of the kind."""
    return f'EDF6VC_{key.upper()}.SGO', f'EDF6VC_{key.upper()}_LEADER.SGO'


def _lods(arc: bytes, model: str) -> tuple[bytes, list[tuple[str, float]]]:
    """The archive with its model merged (model_lod.merge_meshes) and its LOD members (model_lod.lods): soldiers are no
    vehicle class, so model_lod.finish leaves them; the same steps, the table returned for the SGO."""
    import model_lod
    from mdb import RabFile, cmpl_compress, insert_member, mdb_read, mdb_write, rab_read, rab_write
    rab = rab_read(arc)
    member = model_lod._member(rab, model)
    merged = model_lod.merge_meshes(mdb_read(member.data))
    models = model_lod.lods(merged)
    bad = model_lod.check_lods(merged, models)
    if bad:
        raise ValueError(f'{model}: ' + '; '.join(bad))
    member.stored = cmpl_compress(mdb_write(merged))
    table = []
    for k, (md, d) in enumerate(models, 1):
        name = model_lod.lod_name(member.name, k)
        insert_member(rab, RabFile(name, member.folder, member.flag, cmpl_compress(mdb_write(md))))
        table.append((name, round(d, 1)))
    return rab_write(rab), table


def soldier_sgo(game, key: str, stock: str, table: list[tuple[str, float]]) -> bytes:  # noqa: ANN001 - rootcpk.Game
    """The stock soldier SGO on the unit's model (its LOD table `table`) and durability."""
    import dsgo
    u = xm_units.UNITS[key]
    doc = dsgo.parse(game.read('OBJECT', stock + '.SGO'))
    r = doc.root
    if r.get('xgs_scene_object_class') != 'AssultSoldier':
        raise ValueError(f'{stock} 不是 AssultSoldier')
    ref = r.get('animation_model').items[0]
    ref.items[:] = [f'app:/object/{model_file(key).lower()}', f'edf6vc_{key}.mdb',
                    dsgo.Node([dsgo.Node([name, float(d)]) for name, d in table])]
    if not table:
        del ref.items[2]
    r.set('game_object_durability', float(u.durability))
    return dsgo.write(doc)


def build(game, key: str) -> dict[str, bytes]:  # noqa: ANN001
    import xm_rig
    model = f'edf6vc_{key}.mdb'
    arc, _md, _info = xm_rig.build(game, key, model=model)
    arc, table = _lods(arc, model)
    member, leader = body_files(key)
    files = {f'OBJECT/{model_file(key)}': arc,
             f'OBJECT/{member}': soldier_sgo(game, key, STOCK[0], table),
             f'OBJECT/{leader}': soldier_sgo(game, key, STOCK[1], table)}
    check(files, key)
    return files


def check(files: dict[str, bytes], key: str) -> None:
    """Both soldier SGOs: the Ranger's class on the unit's model, every LOD member it names in the archive, its
    durability; the archive's model there."""
    import sgo
    from mdb import rab_read
    u = xm_units.UNITS[key]
    names = {f.name.lower() for f in rab_read(files[f'OBJECT/{model_file(key)}']).files}
    assert f'edf6vc_{key}.mdb' in names, sorted(names)
    for body in body_files(key):
        v = sgo.load(data=files[f'OBJECT/{body}'])
        assert v['xgs_scene_object_class'] == 'AssultSoldier', v['xgs_scene_object_class']
        ref = v['animation_model'][0]
        assert ref[:2] == [f'app:/object/{model_file(key).lower()}', f'edf6vc_{key}.mdb'], ref
        lods = ref[2] if len(ref) > 2 else []
        assert all(name.lower() in names for name, _d in lods), (lods, sorted(names))
        assert [d for _n, d in lods] == sorted(d for _n, d in lods), lods
        assert abs(v['game_object_durability'] - u.durability) < 1e-3, v['game_object_durability']
