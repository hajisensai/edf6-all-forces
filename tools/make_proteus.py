"""The Proteus rework's one resource: its shield, the stock Air Raider's electromagnetic barrier (src/proteus_shield.inc).

  python tools/make_proteus.py [GAME_DIR]     build and install (the installer does this; Root.cpk is only read)

EDF6VC_PROTEUS_SHIELD.SGO is a DemoIndirectFire (the missions' gunship round DEMOGUNSHIPFIRESOLID.SGO remade, as the
EMC's rounds are: pylib/vcobjects.py) whose one round is a BarrierBullet01, the bullet class of the 電磁トーチカ
(WEAPON/EWEAPON196.SGO): its wall, material, colour, collision and HP are the game's own. The round does not move
(speed 0, no gravity) and never runs out (the plugin keeps it on the hull and takes it down); its Ammo_CustomParameter
[arc (rad), radius, height, [sx, sy, sz], [offset]] is a wall round the Proteus (SHIELD_*: proteus_shield.inc reads the
same numbers). Its hit size is tiny: the barrier never touches what it stands round (a touch would anchor it there).

Until 2026-10-09 this module rewrote the stock Proteus models (private MRAB / CAS with 36 hand-made shield panels) and
redirected every VehicleBigBegaruta SGO to them. That is gone: the game loads the stock model again. install() still
runs the old write-ahead journal (Mods/.edf6vc_proteus.json, originals in .edf6vc_proteus_backup/) so that an older
install is undone on update: the redirected SGOs get their originals back, the private MRAB / CAS are removed unless a
loose SGO still names them (a third party's edit). Ledger owner 'proteus'.
"""
from __future__ import annotations

import copy
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pylib'))
import ledger
import modfiles
import rootcpk
import sgo

OWNER = 'proteus'
MANIFEST = '.edf6vc_proteus.json'
BACKUP = '.edf6vc_proteus_backup'

SHIELD_FILE = 'OBJECT/EDF6VC_PROTEUS_SHIELD.SGO'
SHIELD_STOCK = 'DEMOGUNSHIPFIRESOLID.SGO'   # OBJECT: a DemoIndirectFire with an IndirectFireControl
BARRIER_WEAPON = 'EWEAPON196.SGO'           # WEAPON: the Air Raider's electromagnetic barrier (電磁トーチカ)
BARRIER_CLASS = 'BarrierBullet01'
SHIELD_ARC_DEG = 120.0                      # src/proteus_shield.inc kBarrierArcDeg
SHIELD_RADIUS = 12.0                        # m: kBarrierRadius (the MK2's hull reaches 9.7 m forward, 8.9 m aside)
SHIELD_HEIGHT = 17.0                        # m: kBarrierHeight (the hull stands 15.2-15.9 m)
SHIELD_LIFE = 1 << 30                       # frames: never runs out (the plugin takes it down)
SHIELD_HIT_ADJUST = 0.01                    # its own touch radius (AmmoSize 1 x this): it never touches what it stands round
SEGMENT_STEP = 0.0872                       # EDF.dll 0x17A46C8: the ctor's int(arc / this) wall segments (0x2900A7)
# The hosts whose private models the old install generated (EDF6VC_<host>.MRAB / .CAS), kept only to undo it.
LEGACY_HOSTS = ('V614_PROTEUS_MK2', 'VEHICLE407_BIGBEGARUTA')


def _f32(v: float) -> float:
    import struct
    return struct.unpack('<f', struct.pack('<f', v))[0]


def shield_arc() -> float:
    """The arc as the SGO stores it (radians, a 32-bit float)."""
    import math
    return _f32(SHIELD_ARC_DEG * math.pi / 180.0)


def shield(game: rootcpk.Game) -> bytes:
    """EDF6VC_PROTEUS_SHIELD.SGO from the player's Root.cpk (see the module's doc)."""
    version, m = sgo.read(game.read('OBJECT', SHIELD_STOCK))
    p = m.get('indirect_fire_param')
    if (m.get('xgs_scene_object_class') != 'DemoIndirectFire' or not isinstance(p, list) or len(p) != 19
            or p[4] != 'SolidBullet01' or 'indirect_fire_damage' not in m):
        raise ValueError(f'{SHIELD_STOCK} 不是预期的炮舰炮弹（DemoIndirectFire）')
    w = sgo.plain(sgo.load(data=game.read('WEAPON', BARRIER_WEAPON)))   # a DSGO
    if w.get('AmmoClass') != BARRIER_CLASS or not isinstance(w.get('Ammo_CustomParameter'), list):
        raise ValueError(f'{BARRIER_WEAPON} 不是原版电磁碉堡（{BARRIER_CLASS}）')
    stock_cp = w['Ammo_CustomParameter']
    if len(stock_cp) != 5 or not isinstance(stock_cp[3], list) or not isinstance(stock_cp[4], list):
        raise ValueError(f'{BARRIER_WEAPON} 的 Ammo_CustomParameter 不是 [弧度, 半径, 高度, [缩放], [偏移]]')
    for i, value in ((2, 1), (3, 0), (5, 0.0), (6, 0.0), (9, 0.0), (10, SHIELD_LIFE), (11, 0), (14, 0), (15, 0), (16, 0)):
        p[i] = int(value) if isinstance(p[i], int) else float(value)   # each keeps its node type
    p[0] = [0.0, 0.0]
    p[4] = BARRIER_CLASS
    p[7], p[8] = 1.0, SHIELD_HIT_ADJUST          # AmmoSize scales the wall's render matrix (slot 3): it stays 1
    p[12] = copy.deepcopy(w['AmmoColor'])        # the barrier's own blue
    p[13] = [shield_arc(), SHIELD_RADIUS, SHIELD_HEIGHT, [1.0, 1.0, 1.0], [0.0, 0.0, 0.0]]
    fire = w.get('FireSe')
    if isinstance(fire, list) and len(fire) == 6 and isinstance(p[17], list) and len(p[17]) == 6:
        p[17] = copy.deepcopy(fire)              # the barrier's own deploy sound, once
        p[17][0] = 1.0
    if isinstance(p[18], list) and len(p[18]) == 6:
        p[18][2] = 0.0                           # no hit sound: it hits nothing
    m['indirect_fire_damage'] = 0.0              # its HP is the plugin's (proteus_shield.inc writes +0x14D8)
    return sgo.write(version, m)


def check_shield(data: bytes) -> None:
    """The shield SGO as src/proteus_shield.inc raises it."""
    m = sgo.load(data=data)
    p = m['indirect_fire_param']
    assert m['xgs_scene_object_class'] == 'DemoIndirectFire' and p[4] == BARRIER_CLASS, p[4]
    assert p[2] == 1 and p[5] == 0.0 and p[6] == 0.0 and p[9] == 0.0 and p[10] == SHIELD_LIFE and p[15] == 0, p
    assert p[7] == 1.0 and abs(p[8] - SHIELD_HIT_ADJUST) < 1e-6
    arc, radius, height, scale, offset = p[13]
    assert abs(arc - shield_arc()) < 1e-6 and radius == SHIELD_RADIUS and height == SHIELD_HEIGHT, p[13]
    assert scale == [1.0, 1.0, 1.0] and offset == [0.0, 0.0, 0.0], p[13]
    assert m['indirect_fire_damage'] == 0.0


def segments(arc: float) -> int:
    """The wall's segment count as the ctor computes it (float division, truncated)."""
    return int(_f32(_f32(arc) / _f32(SEGMENT_STEP)))


def range_shield(led: ledger.Ledger, game: rootcpk.Game, owner: str = 'testrange') -> str:
    """The test range's hold on the shield SGO: the installer's when there (needed, never rewritten), else made now as
    the range's own. Pass the Ledger the caller saves. Returns its Mods-relative path."""
    if os.path.isfile(led.disk(SHIELD_FILE)):
        led.need(owner, SHIELD_FILE)
    else:
        led.put(owner, SHIELD_FILE, shield(game))
    return SHIELD_FILE


def build(root: str) -> dict[str, bytes]:
    """{Mods-relative path: bytes}: the shield SGO."""
    return {SHIELD_FILE: shield(rootcpk.Game(root))}


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Write-ahead transaction; originals survive both overwrite and interruption. Whatever an earlier install wrote
    and `files` no longer has (the old private models and redirected SGOs) is restored / removed."""
    led, state = ledger.Ledger(root), _load(root)
    outputs = {ledger.key(rel): data for rel, data in files.items()}
    if any(not rel.startswith('OBJECT/') or '..' in rel.split('/') for rel in outputs):
        raise ValueError('Proteus outputs must be relative OBJECT paths')
    paths = []
    for rel in sorted(outputs):
        data, path = outputs[rel], led.disk(rel)
        entry = state['files'].get(rel)
        if entry and entry.get('restoring'):
            _restore(root, led, state, rel)
            entry = state['files'].get(rel)
        current = modfiles.sha256_file(path)
        if entry and current not in (entry.get('written'), entry.get('pending'), entry.get('original')):
            continue  # somebody changed this file after installation; retain it
        if not entry:
            original = modfiles.read(path)
            if original is not None:
                modfiles.atomic_write(_backup(root, rel), original)
            entry = {'original': modfiles.sha256(original) if original is not None else None,
                     'ledger': copy.deepcopy(led.files.get(rel))}
            state['files'][rel] = entry
        entry['pending'] = modfiles.sha256(data)
        _save(root, state)  # durable before Ledger.put touches the target
        paths.append(led.put(OWNER, rel, data))
        entry['written'] = entry.pop('pending')
        _save(root, state)
    _remove(root, led, state, set(state['files']) - set(outputs))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Restore consumers first; retain resources referenced by surviving SGOs."""
    led, state = ledger.Ledger(root), _load(root)
    return _remove(root, led, state, set(state['files']))


def _load(root: str) -> dict:
    path = Path(root, 'Mods', MANIFEST)
    return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {'version': 1, 'files': {}}


def _save(root: str, state: dict) -> None:
    if not state['files']:
        Path(root, 'Mods', MANIFEST).unlink(missing_ok=True)
        return
    modfiles.atomic_write(os.path.join(root, 'Mods', MANIFEST), json.dumps(state, sort_keys=True).encode('utf-8'))


def _backup(root: str, rel: str) -> str:
    return os.path.join(root, 'Mods', BACKUP, *rel.split('/'))


def _restore(root: str, led: ledger.Ledger, state: dict, rel: str) -> bool:
    entry, path = state['files'][rel], led.disk(rel)
    others = set(led.owners(rel)) - {OWNER, ledger.LEGACY}
    if others:
        return False
    current = modfiles.sha256_file(path)
    accepted = {entry.get('written'), entry.get('pending'), entry.get('original')}
    if current not in accepted:
        return False
    # Validate the recovery material before announcing a restore or deleting.
    original = modfiles.read(_backup(root, rel)) if entry.get('original') else None
    if entry.get('original') and (original is None or modfiles.sha256(original) != entry['original']):
        raise ValueError(f'Proteus original backup missing or corrupt: {rel}')
    entry['restoring'] = True
    _save(root, state)
    if original is None:
        if os.path.isfile(path):
            os.remove(path)
    else:
        modfiles.atomic_write(path, original)
    if entry.get('ledger') is None:
        led.files.pop(rel, None)
    else:
        led.files[rel] = entry['ledger']
    led._save()
    del state['files'][rel]
    _save(root, state)
    return True


def _references(root: str) -> set[str]:
    """The old private models a loose SGO still names (a third party's edit of a redirected SGO): kept as a pair."""
    kept = set()
    folder = Path(root, 'Mods', 'OBJECT')
    for path in folder.glob('*') if folder.is_dir() else ():
        if path.suffix.upper() != '.SGO':
            continue
        raw = path.read_bytes().lower()
        for host in LEGACY_HOSTS:
            stem = 'EDF6VC_' + host
            for ext in ('MRAB', 'CAS'):
                needle = f'app:/object/{stem.lower()}.{ext.lower()}'
                if needle.encode('utf-16le') in raw or needle.encode() in raw:
                    kept.update(f'OBJECT/{stem}.{e}' for e in ('MRAB', 'CAS'))
    return kept


def _remove(root: str, led: ledger.Ledger, state: dict, rels: set[str]) -> tuple[list[str], list[str]]:
    restored, kept = [], []
    for rel in sorted(r for r in rels if r.endswith('.SGO')):
        (restored if _restore(root, led, state, rel) else kept).append(led.disk(rel))
    needed = _references(root)
    for rel in sorted(r for r in rels if not r.endswith('.SGO')):
        if rel in needed or not _restore(root, led, state, rel):
            kept.append(led.disk(rel))
        else:
            restored.append(led.disk(rel))
    return restored, kept


def main(argv: list[str]) -> int:
    root = argv[1] if len(argv) > 1 else rootcpk.DEFAULT_GAME
    for path in install(root, build(root)):
        print('写入', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
