"""Find every gold model whose reflection EDF6 whitens, and the pylib/goldcoat.py entries it needs (read only).

    python tools/goldcoat_scan.py [--game DIR] [--edf41 DIR]

Scans EDF6's OBJECT / WEAPON archives with GOLD in their name (goldcoat.discover) and, with EDF4.1, the ported gold
archives of edf41port/objects.json; prints each gold coat material found with its texture hashes as a ready COATS
line, and every difference from goldcoat.COATS. Exit 1 when they differ (a gold model would be missed, or a
registered one is gone), 0 when the registry is complete. tests/test_goldcoat.py runs the same comparison.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'pylib'))
import gamedir  # noqa: E402
import goldcoat  # noqa: E402
import mdb  # noqa: E402
import rootcpk  # noqa: E402


def _entry(where: str, data: bytes, model: str, material: str, shader: str) -> str:
    rab = mdb.rab_read(data)
    md = mdb.mdb_read(next(f for f in rab.files if f.name == model).data)
    mat = next(m for m in md.materials if md.name_of(m.name) == material)
    files = {f.name.lower(): f for f in rab.files}

    def tex(kind: str) -> tuple[str, str]:
        slot = next(x for x in mat.textures if x.kind.startswith(kind))
        name = md.textures[slot.texture].filename
        return name, hashlib.sha256(files[name.lower()].data).hexdigest()

    a, r = tex('albedo'), tex('param_reflect')
    return f"Coat({model!r}, {material!r}, {shader!r}, {a[0]!r}, {a[1]!r}, {r[0]!r}, {r[1]!r}, {where!r})"


def edf41_archives(root41: str) -> dict[str, bytes]:
    import legacy_assets
    with open(os.path.join(ROOT, 'edf41port', 'objects.json'), encoding='utf-8') as f:
        files = json.load(f)['files']
    out = {}
    for rel in files:
        if 'GOLD' not in rel.upper() or not rel.upper().endswith(('.RAB', '.MRAB')):
            continue
        folder, name = rel.split('/')
        for archive in ('Root.cpk', 'Chunk01.cpk', 'Chunk02.cpk', 'Chunk03.cpk'):
            if os.path.isfile(os.path.join(root41, archive)):
                try:
                    out['edf41:' + rel.upper()] = legacy_assets.convert(rel, rootcpk.Game(root41, archive).read(folder, name))
                    break
                except KeyError:
                    pass
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--game', default=gamedir.find())
    ap.add_argument('--edf41', default=gamedir.find_other(gamedir.EDF41))
    args = ap.parse_args(argv)
    if not gamedir.is_game(args.game):
        print('EDF6 not found (--game)')
        return 2
    game = rootcpk.Game(args.game)
    archives = {f'{d.upper()}/{n.upper()}': (d, n) for d, n in game.cpk.index
                if d.upper() in ('OBJECT', 'WEAPON') and 'GOLD' in n.upper() and n.upper().endswith(('.RAB', '.MRAB'))}
    data = {where: game.read(*dn) for where, dn in archives.items()}
    if args.edf41 and os.path.isfile(os.path.join(args.edf41, 'Root.cpk')):
        data.update(edf41_archives(args.edf41))
    else:
        print('（未找到 EDF4.1：跳过移植的金色模型）')
    found = {}
    for where, raw in sorted(data.items()):
        for w, model, material, shader, share in goldcoat.discover_archive(where, raw):
            found[(w, model.lower(), material)] = (model, shader, share, raw)
    registered = {(c.where, c.model.lower(), c.material) for c in goldcoat.COATS}
    if not any(k[0].startswith('edf41:') for k in found):
        registered = {k for k in registered if not k[0].startswith('edf41:')}
    for key, (model, shader, share, raw) in sorted(found.items()):
        mark = '已登记' if key in registered else '未登记'
        print(f'{mark}  {key[0]}  {model}/{key[2]}  {shader}  涂装占比 {share:.2f}')
        if key not in registered:
            print('    ' + _entry(key[0].split(':')[-1] if key[0].startswith('edf41:') else key[0], raw, model, key[2], shader))
            if goldcoat._legacy_name(shader) not in goldcoat.LEGACY:
                print(f'    ！着色器 {shader} 还没有转换规则（goldcoat.LEGACY）')
    gone = registered - set(found)
    for key in sorted(gone):
        print(f'登记了但没找到  {key}')
    return 1 if gone or set(found) - registered else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
