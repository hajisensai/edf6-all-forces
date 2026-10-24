"""Read-only Root.cpk census behind src/allclass.cpp (ini AllClassesRide; docs/all-classes-ride-re.md).

python tests/allclass_census_check.py [GAME_DIR]
Optional game-data test: exit 77 if Root.cpk is unavailable. No game is started, nothing is written.

What the hook takes for granted, checked on every stock vehicle SGO / DSGO of Root.cpk and the four classes' CAS:
  - every soldier SGO's game_object_vehicle_mask (human+0x31C) is one class bit (1 R, 2 WD, 4 F, 8 AR);
  - every stock seat's class mask (vehicle_riding_position[i][4]) holds a class bit, so seatclass::Widen opens every
    one of them to all four classes, and the masks are the known 9 / 15 / 143 (the stock closes no seat with 0);
  - vehicle_riding_position[i][3] is a text-table key (the seat's shown name), not a pose: every one is a key of
    TEXTTABLE_STEAM.EN, or absent from it (the 410's: shown as nothing), and none is a CAS state;
  - the rider's pose is the seat locator's MAB parameters: a seat that shows its rider (DispCharacter 1) names an
    Animation; each such Animation is in the Ranger's and the Air Raider's CAS; the ones a Wing Diver's or a Fencer's
    CAS lack are exactly the two bike states; and seatclass::kFallbackRide is a state of all four classes' CAS.
"""
from __future__ import annotations

import collections
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib')]
import cas_graft
import dsgo
import rootcpk
import sgo

CLASSES = {'R': 'EDF6ARMYSOLDIER.CAS', 'WD': 'EDF6_WINGDIVER.CAS', 'F': 'EDF6HEAVYARMOR.CAS', 'AR': 'EDF6ENGINEER.CAS'}
FALLBACK = re.search(r'kFallbackRide\[\]=L"(\w+)"', (ROOT / 'src' / 'seat_class.h').read_text(encoding='utf-8')).group(1)
BIKE_STATES = {'vehicle503_bike_driver', 'vehicle613_bike_driver'}


def text(b: bytes, at: int) -> str:
    end = at
    while b[end:end + 2] != b'\0\0':
        end += 2
    return b[at:end].decode('utf-16le')


def mab_points(b: bytes) -> dict[str, dict | None]:
    """Every MAB locator's name and its own SGO parameters (record +0x1C), over all its lists (mab.py's layout)."""
    lists, = struct.unpack_from('<H', b, 0x0C)
    heads, = struct.unpack_from('<I', b, 0x14)
    out: dict[str, dict | None] = {}
    for li in range(lists):
        h = heads + li * 8
        _kind, count, first = struct.unpack_from('<HHi', b, h)
        for r in range(count):
            at = h + first + r * 0x20
            name = text(b, at + struct.unpack_from('<i', b, at)[0])
            so, = struct.unpack_from('<i', b, at + 0x1C)
            params = None
            if so:
                _v, m = sgo.read(b[at + so:])
                params = {k: sgo.plain(v) for k, v in m.items()}
            out.setdefault(name, params)
    return out


def load(game: rootcpk.Game, name: str) -> tuple[dict, bytes | None]:
    data = game.read('OBJECT', name)
    if data[:4] == b'DSGO':
        root = dsgo.parse(data).root
        model = root.get('animation_model') if 'animation_model' in root.names.values() else None
        blob = model.items[2] if model is not None and len(model.items) > 2 else None
        return dsgo.to_py(root), blob.data if isinstance(blob, dsgo.Blob) else None
    _v, m = sgo.read(data)
    model = m.get('animation_model')
    mab = model[2] if isinstance(model, list) and len(model) > 2 and isinstance(model[2], (bytes, bytearray)) else None
    return {k: sgo.plain(v) for k, v in m.items()}, mab


def run(game_dir: str) -> None:
    game = rootcpk.Game(game_dir)
    states = {c: cas_graft.states(game.read('OBJECT', n))['main'] for c, n in CLASSES.items()}
    for c in CLASSES:
        assert FALLBACK in states[c], f'{c} has no {FALLBACK}'
    _v, table = sgo.read(game.read('ETC', 'TEXTTABLE_STEAM.EN.TXT_SGO'))

    classes = collections.Counter()
    masks = collections.Counter()
    shown = collections.defaultdict(set)
    seats = 0
    for d, n in sorted(game.cpk.index):
        if d != 'OBJECT' or not n.upper().endswith('.SGO'):
            continue
        data = game.read(d, n)
        if data[:4] != b'DSGO' and 'vehicle_riding_position'.encode('utf-16le') not in data \
                and 'game_object_vehicle_mask'.encode('utf-16le') not in data:
            continue
        m, mab = load(game, n)
        if not isinstance(m, dict):
            continue
        if 'game_object_vehicle_mask' in m:
            v = int(m['game_object_vehicle_mask'])
            assert v in (1, 2, 4, 8), f'{n}: game_object_vehicle_mask {v}'
            classes[v] += 1
        if 'vehicle_riding_position' not in m:
            continue
        points = mab_points(mab) if mab else {}
        for i, s in enumerate(m['vehicle_riding_position']):
            seats += 1
            mask = int(s[4])
            masks[mask] += 1
            assert mask & 0x0F, f'{n} seat {i}: mask {mask} has no class bit'
            assert s[3] in table or s[3].startswith('410_HELI_'), f'{n} seat {i}: {s[3]} is no text key'
            assert not any(s[3] in st for st in states.values()), f'{n} seat {i}: {s[3]} is a CAS state'
            p = points.get(s[1]) or {}
            if int(p.get('DispCharacter', 0) or 0):
                anim = p.get('Animation')
                assert isinstance(anim, str) and anim, f'{n} seat {i}: shown without an Animation'
                shown[anim].add(f'{n}#{i}')
    assert set(masks) <= {9, 15, 143}, masks
    assert seats >= 150 and masks[9] >= 30, (seats, masks)
    assert set(classes) == {1, 2, 4, 8}, classes
    lacking = {a for a in shown for c in ('WD', 'F') if a not in states[c]}
    assert lacking == BIKE_STATES, lacking
    for a in shown:
        assert a in states['R'] and a in states['AR'], a
    print(f'allclass census: {seats} seats, masks {dict(masks)}, soldiers {dict(classes)}, shown {sorted(shown)}')


if __name__ == '__main__':
    # ctest passes EDF6_GAME_DIR, empty when not set: skipped then (the rule for game-data checks); by hand, no argument
    # means the game pylib finds.
    root = sys.argv[1] if len(sys.argv) > 1 else rootcpk.DEFAULT_GAME
    if not root or not (Path(root) / 'Root.cpk').is_file():
        print('SKIP: no Root.cpk')
        raise SystemExit(77)
    run(root)
