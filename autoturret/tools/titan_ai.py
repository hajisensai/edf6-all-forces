"""Give the NPC Titan (OBJECT/VEHICLE404_BIGTANK_AI.SGO) the player Titan's two side cannons.

The stock AI Titan loads only its main cannon: mission_setup leaves the gunner seats' weapon slots
1 and 2 as [0], so NPC-crewed Titans (e.g. mission 64) drive without side guns and the tank
gunners have nothing to aim. This copies slots 1 and 2 from the player Titan
(v_404bigtank_subCannon.sgo) and preloads that weapon. Everything else in the file stays stock.

build.py installs it as OBJECT/VEHICLE404_BIGTANK_AI.SGO with the rest of the mod's data.
"""
from __future__ import annotations

import rootcpk
import sgo

NAME = 'VEHICLE404_BIGTANK_AI.SGO'
SIDE_SLOTS = (1, 2)   # mission_setup[2][i] = weapon of seat i; 0 is the driver's main cannon


def weapons(members: dict[str, sgo.Value]) -> list:
    """mission_setup's gun mounts ([2], as in every vehicle setup: autoturret/tools/vehicle_setup.py)."""
    setup = members['mission_setup']
    if not isinstance(setup, list) or len(setup) < 3 or not isinstance(setup[2], list) or len(setup[2]) <= max(SIDE_SLOTS):
        raise SystemExit(f'{NAME}: mission_setup is not the layout this tool knows')
    return setup[2]


def build() -> bytes:
    game = rootcpk.default()
    version, ai = sgo.read(game.read('OBJECT', NAME))
    _, player = sgo.read(game.read('OBJECT', 'VEHICLE404_BIGTANK.SGO'))
    if sgo.read(sgo.write_depth_first(version, ai)) != (version, ai):
        raise SystemExit('VEHICLE404_BIGTANK_AI.SGO does not round-trip')
    slots, stock = weapons(ai), weapons(player)
    resource = ai['resource']
    for i in SIDE_SLOTS:
        slots[i] = stock[i]
        name = stock[i][0]
        if not any(r.lower() == name.lower() for r in resource):
            resource.append(name)
    return sgo.write_depth_first(version, ai)
