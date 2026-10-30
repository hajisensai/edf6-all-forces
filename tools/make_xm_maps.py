"""X-Morph: Defense's maps as EDF6 maps (map bodies: ground, buildings, props, collision; no missions).

The developer builds them from their own copy of X-Morph (pylib/xm_map.py reads a map, pylib/map_build.py writes it)
into the model pack's `xm_maps` folder; the installer copies them into Mods/MAP with the donor EDF6 map's weather and
navmesh read from the player's EDF6 (as tools/edf41_maps.py does for 4.1's maps). docs/xmorph-maps.md has the details.

    python tools/make_xm_maps.py build [--out DIR] [MAP ...]     the model pack folder (default: D:/APP/edf6-models)
    python tools/make_xm_maps.py install GAME | remove GAME

The folder (MODEL_DIR):
  xm_maps.json            {'version', 'maps': {MAP: {'donor', 'weathers', 'label', 'files': {file: sha256},
                          'units': [[xm_units key, x, y, z, yaw]], 'bounds': [[x, y, z], [x, y, z]]}}}
  <MAP>.MAC / .MAD / .RAB the map's own files
  LICENSE.txt

    install(root, say) -> (written, skipped)
    remove(root) -> (deleted, kept)

A map is left out, with the reason, when the model pack lacks it (or holds other bytes), when its donor's files are not
in the player's EDF6, or when a file of the same name is in Mods and is not ours (another mod's: never written over).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ledger  # noqa: E402
import obj_model  # noqa: E402
from modfiles import sha256, sha256_file  # noqa: E402

OWNER = 'xmmaps'      # pylib/ledger.py OWNERS
VERSION = 1           # raise when the builder's output changes
MODEL_DIR = 'xm_maps'
INDEX = 'xm_maps.json'
NAVMESH = ('NAVMESH.HKT', 'NAVMESH.NVMD', 'NAVMESH_H.HKT', 'NAVMESH_H.NVMD')
OWN = ('MAC', 'MAD', 'RAB')
LICENSE = """X-Morph: Defense (c) EXOR Studios.

X-Morph: Defense's maps (story_japan, story_hawaii, dlc_3_iceland, dlc_1_france, dlc_1_finland): converted by
tools/make_xm_maps.py from the developer's own copy of the game for the EDF6 All Forces model pack. These are the game's
assets, not this project's: they are not in the public repository and are not licensed for redistribution; the model
pack carries them for the user's own installation only.
"""


def files_of(name: str, entry: dict) -> list[str]:
    """The Mods paths map `name` installs."""
    return ([f'MAP/{name}.{ext}' for ext in OWN]
            + [f'MAP/{name}_{w}.{ext}' for w in entry['weathers'] for ext in ('MAE', 'RAB')]
            + [f'MAP/{name}.{n}' for n in NAVMESH])


# ------------------------------------------------------------------------------------------------ build (developer)

def build(out: str, names: list[str], say: Callable[[str], None] = print) -> dict:
    import map_build
    import rootcpk
    import xm_map
    import xm_pack
    folder = os.path.join(out, MODEL_DIR)
    os.makedirs(folder, exist_ok=True)
    index_path = os.path.join(folder, INDEX)
    index = json.load(open(index_path, encoding='utf-8')) if os.path.exists(index_path) else {}
    if index.get('version') != VERSION:
        index = {'version': VERSION, 'maps': {}}
    import edf41_maps
    e6 = edf41_maps.Archives(rootcpk.DEFAULT_GAME)
    template = e6.read(map_build.TEMPLATE_MAC)
    donor_mad = e6.read(map_build.DONOR_MAD)
    packs = xm_pack.Packs()
    for name in names or list(xm_map.MAPS):
        xm = xm_map.MAPS[name]
        say(f'{name}: reading X-Morph {xm.folder} ...')
        scene = xm_map.scene(packs, name)
        say(f'  {len(scene.models)} models, {len(scene.placements)} placements, {len(scene.textures)} textures; '
            f'skipped {dict(scene.skipped.most_common(8))}')
        files = {f'{name}.MAC': map_build.mac(scene, template), f'{name}.MAD': map_build.mad(scene, donor_mad),
                 f'{name}.RAB': map_build.rab(scene, say)}
        for fn, data in files.items():
            with open(os.path.join(folder, fn), 'wb') as f:
                f.write(data)
            say(f'  {fn}: {len(data) / 1e6:.1f} MB')
        lo, hi = scene.bounds
        index['maps'][name] = {
            'donor': xm.donor, 'weathers': list(xm.weathers), 'label': xm.label,
            'files': {fn: hashlib.sha256(d).hexdigest() for fn, d in files.items()},
            'units': [[key, *(round(v, 2) for v in p.pos), round(p.yaw, 2)] for key, p in scene.units],
            'bounds': [[round(float(v), 2) for v in lo], [round(float(v), 2) for v in hi]]}
        with open(index_path, 'w', encoding='utf-8') as f:
            json.dump(index, f, indent=1, ensure_ascii=False)
    with open(os.path.join(folder, 'LICENSE.txt'), 'w', encoding='utf-8') as f:
        f.write(LICENSE)
    return index


# ------------------------------------------------------------------------------------------------ install

def _index(folder: str | None) -> dict:
    if folder is None or not os.path.isfile(os.path.join(folder, INDEX)):
        return {}
    with open(os.path.join(folder, INDEX), encoding='utf-8') as f:
        index = json.load(f)
    return index.get('maps', {}) if index.get('version') == VERSION else {}


def _foreign(led: ledger.Ledger, rel: str) -> bool:
    """A file no tool of ours wrote, another tool's, or edited since our recorded write: preserve its bytes."""
    if not os.path.exists(led.disk(rel)):
        return False
    owners = led.owners(rel)
    return not owners or any(o != OWNER for o in owners) or led.changed(rel)


def install(root: str, say: Callable[[str], None] = print) -> tuple[list[str], list[tuple[str, str]]]:
    """Every map of the model pack written as OWNER's; (paths written, [(map, why not)])."""
    import edf41_maps
    folder = obj_model.model_dir(MODEL_DIR)
    maps = _index(folder)
    if not maps:
        return [], [('X-Morph 地图', f'模型包里没有 {MODEL_DIR}/{INDEX}（或版本不是 {VERSION}）')]
    e6 = edf41_maps.Archives(root)
    led = ledger.Ledger(root)
    written: list[str] = []
    skipped: list[tuple[str, str]] = []
    wanted: set[str] = set()
    for name, entry in maps.items():
        foreign = [rel for rel in files_of(name, entry) if _foreign(led, rel)]
        if foreign:
            skipped.append((name, f"Mods 里已有别的 MOD 的文件：{', '.join(foreign)}"))
            continue
        files: dict[str, bytes] = {}
        bad = []
        for fn, sha in entry['files'].items():
            path = os.path.join(folder, fn)
            if not os.path.isfile(path) or sha256_file(path) != sha:
                bad.append(fn)
                continue
            with open(path, 'rb') as f:
                files[f'MAP/{fn}'] = f.read()
        if bad:
            skipped.append((name, f"模型包里的文件缺失或内容不对：{', '.join(bad)}"))
            continue
        donor = entry['donor']
        try:
            for w in entry['weathers']:
                for ext in ('MAE', 'RAB'):
                    files[f'MAP/{name}_{w}.{ext}'] = e6.read(f'{donor}_{w}.{ext}')
            for n in NAVMESH:
                files[f'MAP/{name}.{n}'] = e6.read(f'{donor}.{n}')
        except KeyError as e:
            skipped.append((name, f'EDF6 里没有借用的天气 / 寻路文件 {e}'))
            continue
        say(f"  X-Morph 地图 {name}（{entry['label']}，借用 EDF6 {donor} 的天气与寻路网格）")
        wanted.update(ledger.key(rel) for rel in files)
        mine = set(led.owned_by(OWNER))
        for rel, data in files.items():
            if ledger.key(rel) in mine and sha256_file(led.disk(rel)) == sha256(data):
                continue
            written.append(led.put(OWNER, rel, data))
    led.release(OWNER, sorted(set(led.owned_by(OWNER)) - wanted))
    return written, skipped


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('--out', default=obj_model.DEV_MODELS)
    b.add_argument('maps', nargs='*')
    for c in ('install', 'remove'):
        sub.add_parser(c).add_argument('game')
    a = ap.parse_args(argv)
    if a.cmd == 'build':
        build(a.out, a.maps)
    elif a.cmd == 'install':
        written, skipped = install(a.game)
        for p in written:
            print('写入', p)
        for n, why in skipped:
            print('跳过', n, why)
    else:
        deleted, kept = remove(a.game)
        print('删除', len(deleted), '保留', len(kept))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
