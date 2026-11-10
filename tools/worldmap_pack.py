"""The world map's base textures from the model pack into Mods (docs/plan-2026-11-10-worldmap.md §2).

The model pack's folder pylib/worldmap.py TEXTURE_DIR (built by the developer: tools/worldmap_basemap.py textures)
holds the two DXT1 textures ready for the game and TEXTURE_INDEX ({'version', 'files': {name: {'sha256', 'size'}}}).
The installer copies each to Mods/<worldmap.TEXTURES[kind]> as this tool's (ledger owner OWNER); nothing is encoded at
install time. A file already installed with the same bytes is not written again.

Nothing is substituted when the folder is missing, of another worldmap.TEXTURE_VERSION, or holds other bytes: the textures are
not installed (one line says why) and whatever this tool installed before is released, so the plugin finds no world
map texture and does not show the map. A file of the same name in Mods that is not ours (another mod's) is never
written over.

    python -B tools/worldmap_pack.py install GAME | remove GAME

    install(root, say) -> (written, skipped: [(what, why)])
    remove(root) -> (deleted, kept)
"""
from __future__ import annotations

import json
import os
import sys
from typing import Callable

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ledger  # noqa: E402
import obj_model  # noqa: E402
import worldmap  # noqa: E402
from modfiles import sha256, sha256_file  # noqa: E402

OWNER = 'worldmap'        # pylib/ledger.py OWNERS
LABEL = '世界地图底图'


def files() -> dict[str, str]:
    """{file name in the model pack folder: path under Mods}."""
    return {rel.rsplit('/', 1)[1]: rel for rel in worldmap.TEXTURES.values()}


def _index(folder: str | None) -> tuple[dict, str | None]:
    """(the index's files, None) or ({}, why not)."""
    where = f'{worldmap.TEXTURE_DIR}/{worldmap.TEXTURE_INDEX}'
    if folder is None or not os.path.isfile(os.path.join(folder, worldmap.TEXTURE_INDEX)):
        return {}, f'模型包里没有 {where}'
    try:
        with open(os.path.join(folder, worldmap.TEXTURE_INDEX), encoding='utf-8') as f:
            index = json.load(f)
    except (OSError, ValueError) as e:
        return {}, f'{where} 读不了：{type(e).__name__}: {e}'
    if not isinstance(index, dict) or index.get('version') != worldmap.TEXTURE_VERSION or not isinstance(index.get('files'), dict):
        return {}, f'{where} 的版本不是 {worldmap.TEXTURE_VERSION}（模型包与安装器不配套）'
    missing = sorted(set(files()) - set(index['files']))
    if missing:
        return {}, f"{where} 缺少 {', '.join(missing)}"
    return index['files'], None


def _foreign(led: ledger.Ledger, rel: str) -> bool:
    """A file no tool of ours wrote, another tool's, or edited since our recorded write: its bytes are kept."""
    if not os.path.exists(led.disk(rel)):
        return False
    owners = led.owners(rel)
    return not owners or any(o != OWNER for o in owners) or led.changed(rel)


def install(root: str, say: Callable[[str], None] = print) -> tuple[list[str], list[tuple[str, str]]]:
    """Both textures written as OWNER's, or neither: (paths written, [(what, why not)]). Whatever OWNER installed
    before and is not installed now is released."""
    led = ledger.Ledger(root)
    folder = obj_model.model_dir(worldmap.TEXTURE_DIR)
    entries, why = _index(folder)
    skipped: list[tuple[str, str]] = []
    data: dict[str, bytes] = {}
    if why is None:
        for name, rel in files().items():
            if _foreign(led, rel):
                why = f'Mods 里已有别的 MOD 的 {rel}'
                break
            entry = entries[name]
            path = os.path.join(folder, name)
            if not os.path.isfile(path) or os.path.getsize(path) != entry.get('size'):
                why = f'模型包里的 {worldmap.TEXTURE_DIR}/{name} 缺失或大小不对'
                break
            with open(path, 'rb') as f:
                blob = f.read()
            if sha256(blob) != entry.get('sha256'):
                why = f'模型包里的 {worldmap.TEXTURE_DIR}/{name} 内容不对'
                break
            data[rel] = blob
    written: list[str] = []
    if why is not None:
        skipped.append((LABEL, why))
        data = {}
    mine = set(led.owned_by(OWNER))
    for rel, blob in data.items():
        if ledger.key(rel) in mine and sha256_file(led.disk(rel)) == sha256(blob):
            continue
        written.append(led.put(OWNER, rel, blob))
    led.release(OWNER, sorted(mine - {ledger.key(rel) for rel in data}))
    if data:
        say(f'  {LABEL}（NASA Blue Marble，{len(data)} 张贴图）')
    return written, skipped


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER))), writer=True)


def main(argv: list[str]) -> int:
    if len(argv) != 3 or argv[1] not in ('install', 'remove'):
        print(__doc__)
        return 2
    if argv[1] == 'install':
        written, skipped = install(argv[2])
        for path in written:
            print('写入', path)
        for what, why in skipped:
            print('跳过', what, why)
        return 0
    deleted, kept = remove(argv[2])
    for path in deleted:
        print('删除', path)
    for path in kept:
        print('保留（之后被别的工具改过）', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
