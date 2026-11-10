"""EDF6's own Ranger shotguns held as its rifles (pylib/ranger_hold.py; user 2026-11-10, test site #32: the shotgun's
hold, the left arm bent at 90 degrees, should be like the assault rifle's; maintainer: "32 改，应该改的都改";
docs/feedback-2026-11-10-shotgun-grip.md). Into <game>/Mods:

  Mods/WEAPON/<SGO>.SGO   each stock Ranger shotgun (WEAPONTABLE category 1, ranger_hold.CLASSES) not already held as
                          the rifles: the same file with ranger_hold.rifle_like_set's five hold names (the set the
                          developers gave Destruction Blazer / Burst Blazer), nothing else changed

The player's and the soldiers' (an NPC with a player's shotgun loads the same file) hold change together; the ported
shotguns (tools/ported_weapons.py) take the same set when they are built. The weapon table is not touched. A file
another mod already put into Mods/WEAPON is left as it is (said); this tool never writes over a file it did not write.
The hold is only how the soldier stands: no number of the weapon changes, a room member without these files sees the
others' soldiers held as the stock game holds them, nothing else.

Built in memory first, written and recorded in the ledger as this tool's (pylib/ledger.py OWNER); --remove releases
them, the stock files coming back.

  python tools/make_stock_holds.py [game dir]               write / refresh
  python tools/make_stock_holds.py [game dir] --out DIR     build and check only, the files written under DIR
  python tools/make_stock_holds.py [game dir] --remove      release them
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'pylib'))
import dsgo  # noqa: E402
import ledger  # noqa: E402
import ranger_hold  # noqa: E402
import vcobjects as vc  # noqa: E402

OWNER = 'stockholds'   # pylib/ledger.py
TABLE = ('WEAPON', 'WEAPONTABLE.SGO')


def held(data: bytes, donor: ranger_hold.HoldSet) -> bytes | None:
    """The stock weapon `data` with the hold set `donor`, None when it has it already (or has no full hold set)."""
    doc = dsgo.parse(data)
    if not ranger_hold.hold_like(doc.root, donor):
        return None
    return dsgo.write(doc)


def build(root: str) -> tuple[dict[str, bytes], list[str]]:
    """({path under Mods: bytes} to write, the files left alone because another mod put its own into Mods/WEAPON).
    Reads the stock table and weapons (Root.cpk) only."""
    game = vc.Game(root)
    led = ledger.Ledger(root)
    rows = ranger_hold.rows_of(dsgo.parse(game.read(*TABLE)))
    read = lambda name: game.read('WEAPON', name)   # noqa: E731
    donor = ranger_hold.rifle_like_set(rows, read)
    out: dict[str, bytes] = {}
    skipped: list[str] = []
    for name, _set in ranger_hold.category_sets(rows, read, ranger_hold.SHOTGUN):
        rel = f'WEAPON/{name}'
        made = held(read(name), donor)
        if made is None:
            continue
        if os.path.isfile(led.disk(rel)) and OWNER not in led.owners(rel):
            skipped.append(rel)
            continue
        out[rel] = made
    check(out, donor)
    return out, skipped


def check(files: dict[str, bytes], donor: ranger_hold.HoldSet) -> None:
    """Every file built is a gun (ranger_hold.CLASSES) held `donor`."""
    for rel, data in files.items():
        r = dsgo.parse(data).root
        assert r.get('xgs_scene_object_class') in ranger_hold.CLASSES, rel
        assert ranger_hold.hold_set(r) == donor, rel


def install(root: str, files: dict[str, bytes]) -> list[str]:
    """Writes `files` (build) as this tool's; what it wrote before and does not now is released (the stock file
    comes back)."""
    led = ledger.Ledger(root)
    before = set(led.owned_by(OWNER))
    paths = [led.put(OWNER, rel, data) for rel, data in files.items()]
    led.release(OWNER, sorted(before - {ledger.key(rel) for rel in files}))
    return paths


def remove(root: str) -> tuple[list[str], list[str]]:
    """Releases this tool's files: (deleted, kept because someone else changed them since)."""
    led = ledger.Ledger(root)
    return led.release(OWNER, list(led.owned_by(OWNER)))


def main(argv: list[str]) -> int:
    import modfiles
    args = [a for a in argv if not a.startswith('--')]
    out = argv[argv.index('--out') + 1] if '--out' in argv else None
    if out in args:
        args.remove(out)
    root = args[0] if args else vc.DEFAULT_GAME
    if '--remove' in argv:
        modfiles.refuse_while_running()
        deleted, kept = remove(root)
        for p in deleted:
            print('删除', p)
        for p in kept:
            print('保留（之后被别的工具改过）', p)
        return 0
    files, skipped = build(root)
    for rel in skipped:
        print('跳过（别的 mod 已经放了自己的）', rel)
    if out:
        for rel, data in files.items():
            path = os.path.join(out, *rel.split('/'))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as f:
                f.write(data)
            print('生成', path)
        return 0
    modfiles.refuse_while_running()
    for path in install(root, files):
        print('写入', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
