"""Writes the ultimate strike's effect files (src/ultimate.cpp, docs/plan-2026-10-19-ultimate.md, pylib/vcobjects.py ULT_*)
into <game>/Mods/OBJECT:

  EDF6VC_ULT_SIGHT.SGO   the charge's aim beams: thin crimson, silent, no damage (the plugin grows them)
  EDF6VC_ULT_BOLT.SGO    a lightning bolt: a short blue-white flash with a crack
  EDF6VC_ULT_SPARK.SGO   the burst where a bolt lands
  EDF6VC_ULT_BEAM.SGO    the main beam: the satellite laser at its thickest, violet
  EDF6VC_ULT_CORE.SGO    its white-hot core
  EDF6VC_ULT_BLAST.SGO   a ring's blast
  EDF6VC_ULT_CIRCLE_<LAYER>.MRAB / .SGO   the magic circles (pylib/magic_circle.py): the ground's OUTER, STAR and CORE,
                         the sky's SKY; their patterns drawn here with Pillow, the stock mothership barrier's material

Without these files the ultimate call is the stock satellite call it is made from (Spritefall Destroy). Built in memory
from the player's own Root.cpk (only read), written atomically and recorded in the ledger as this tool's; --remove
releases them.

  python tools/make_ultimate.py [game dir]               write / refresh
  python tools/make_ultimate.py [game dir] --out DIR     build and check only, the files written under DIR
  python tools/make_ultimate.py [game dir] --remove      release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import ledger  # noqa: E402
import magic_circle  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'ultimate'   # pylib/ledger.py


def names() -> list[str]:
    """Every path under Mods this tool writes: the rounds, then each magic circle's archive and object."""
    return [f'OBJECT/{n}' for n in vc.ULT_FILES] + [f'OBJECT/{n}' for n in magic_circle.files()]


def build(root: str) -> dict[str, bytes]:
    """Every file this tool writes, {path under Mods: bytes}, checked (vcobjects.check_ult, magic_circle.check), from the
    game's Root.cpk."""
    game = vc.Game(root)
    rounds = vc.ult_rounds(game)
    vc.check_ult(rounds)
    out = {f'OBJECT/{n}': data for n, data in rounds.items()}
    for layer in magic_circle.LAYERS:
        arc = magic_circle.build(game, layer)
        magic_circle.check(arc, layer)
        out[f'OBJECT/{magic_circle.out_arc(layer)}'] = arc
        out[f'OBJECT/{magic_circle.sgo_file(layer)}'] = magic_circle.sgo(game, layer)
    return out


def install(root: str, files: dict[str, bytes]) -> list[str]:
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    led = ledger.Ledger(root)
    return led.release(OWNER, sorted(set(led.owned_by(OWNER)) | {ledger.key(n) for n in names()}), writer=True)


def write_out(files: dict[str, bytes], out: str) -> list[str]:
    """The files under `out` (a scratch folder, never the game's): for an offline build and check."""
    paths = []
    for rel, data in files.items():
        p = os.path.join(out, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as h:
            h.write(data)
        paths.append(p)
    return paths


def main(argv: list[str]) -> int:
    out = argv[argv.index('--out') + 1] if '--out' in argv else None
    args = [a for i, a in enumerate(argv) if not a.startswith('--') and (i == 0 or argv[i - 1] != '--out')]
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        deleted, kept = remove(root)
        for path in deleted:
            print('删除', path)
        for path in kept:
            print('保留（已被别人改过）', path)
        return 0
    files = build(root)
    for path in write_out(files, out) if out else install(root, files):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
