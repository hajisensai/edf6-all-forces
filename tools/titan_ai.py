"""Give the NPC Titan (OBJECT/VEHICLE404_BIGTANK_AI.SGO) the player Titan's two side cannons.

The stock AI Titan loads only its main cannon: mission_setup leaves the gunner seats' weapon slots
1 and 2 as [0], so NPC-crewed Titans (e.g. mission 64) drive without side guns and the tank
gunners have nothing to aim. This copies slots 1 and 2 from the player Titan
(v_404bigtank_subCannon.sgo) and preloads that weapon. Everything else in the file stays stock.

  python -B tools/titan_ai.py            -> dist/Mods/OBJECT/VEHICLE404_BIGTANK_AI.SGO
"""
from __future__ import annotations

import os

import gamefs
import sgo_write as sgo

OUT = os.path.join(os.path.dirname(__file__), '..', 'dist', 'Mods', 'OBJECT', 'VEHICLE404_BIGTANK_AI.SGO')
SIDE_SLOTS = (1, 2)   # mission_setup[2][i] = weapon of seat i; 0 is the driver's main cannon


def weapons(values: list) -> list:
    return dict(values)['mission_setup'].value[2].value


def build() -> bytes:
    ai = sgo.parse(gamefs.read('OBJECT', 'VEHICLE404_BIGTANK_AI.SGO'))
    player = sgo.parse(gamefs.read('OBJECT', 'VEHICLE404_BIGTANK.SGO'))
    if sgo.parse(sgo.write(ai)) != ai:
        raise SystemExit('VEHICLE404_BIGTANK_AI.SGO does not round-trip')
    slots, stock = weapons(ai), weapons(player)
    resource = dict(ai)['resource'].value
    for i in SIDE_SLOTS:
        slots[i] = stock[i]
        name = stock[i].value[0].value
        if not any(r.value.lower() == name.lower() for r in resource):
            resource.append(sgo.Str(name))
    return sgo.write(ai)


if __name__ == '__main__':
    data = build()
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'wb') as f:
        f.write(data)
    print(f'{os.path.normpath(OUT)}: {len(data)} bytes')
