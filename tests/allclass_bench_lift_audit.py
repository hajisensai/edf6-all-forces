"""The bench lift table (src/seat_class.h kBenchLifts) recomputed from the game's CAS (src/allclass.cpp PlaceRider).

python tests/allclass_bench_lift_audit.py [GAME_DIR]
Optional game-data test: exit 77 if Root.cpk is unavailable. Read-only.

For each bike state a seat may name (vehicle503_bike_driver, vehicle613_bike_driver): the pelvis (koshi, frame 0) it
gives in the Ranger's and the Air Raider's CAS (the classes that have it; the two must agree), and the bench pose
(seatclass::kFallbackRide, vehicle_striker_rear) in the Wing Diver's and the Fencer's; each table row must be the bike
state's pelvis less that class's bench pelvis, within 0.5 mm, and the table must cover exactly the classes lacking the
state.
"""
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pylib')]
import cas_graft
import edf41bike
import rootcpk

CLASSES = {1: 'EDF6ARMYSOLDIER', 2: 'EDF6_WINGDIVER', 4: 'EDF6HEAVYARMOR', 8: 'EDF6ENGINEER'}
BIKES = ('vehicle503_bike_driver', 'vehicle613_bike_driver')
BENCH = 'vehicle_striker_rear'


def table() -> dict[tuple[str, int], tuple[float, float, float]]:
    text = (ROOT / 'src' / 'seat_class.h').read_text(encoding='utf-8')
    rows = re.findall(r'\{L"(\w+)",(\d+),\{([-0-9.f,]+)\}\}', text)
    return {(state, int(cls)): tuple(float(v.rstrip('f')) for v in d.split(',')) for state, cls, d in rows}


def main() -> int:
    # ctest passes EDF6_GAME_DIR, empty when not set: skipped then; by hand, no argument means the game pylib finds.
    root = sys.argv[1] if len(sys.argv) > 1 else rootcpk.DEFAULT_GAME
    if not root or not (Path(root) / 'Root.cpk').is_file():
        print('SKIP: no Root.cpk')
        return 77
    game = rootcpk.Game(root)
    cas = {bit: game.read('OBJECT', name + '.CAS') for bit, name in CLASSES.items()}
    states = {bit: cas_graft.state_clips(data).get('main', {}) for bit, data in cas.items()}
    want = {}
    for bike in BIKES:
        have = [bit for bit in CLASSES if bike in states[bit]]
        assert have == [1, 8], (bike, have)
        pelvis = [edf41bike._state_root(cas[bit], bike) for bit in have]
        assert max(abs(a - b) for a, b in zip(*pelvis)) < 1e-4, (bike, pelvis)
        for bit in (2, 4):
            assert BENCH in states[bit], (bit, BENCH)
            bench = edf41bike._state_root(cas[bit], BENCH)
            want[(bike, bit)] = tuple(p - b for p, b in zip(pelvis[0], bench))
    got = table()
    assert set(got) == set(want), sorted(set(got) ^ set(want))
    for key, d in want.items():
        assert max(abs(a - b) for a, b in zip(got[key], d)) < 5e-4, (key, got[key], d)
    print(f'allclass bench lift audit: {len(want)} lifts ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
