"""What every cockpit kit (pylib/cockpit_kits*.py) lays itself out with: the room round the seat's eye (Cabin), the
margin its walls keep inside the hull, its textures' paths. Its own module so the family modules (cockpit_kits_<family>)
and the aggregator that merges them (cockpit_kits) both import it, and no family imports the aggregator."""
from __future__ import annotations

import math

from cockpits import KitMaterial

TEX = 'textures/{}.png'
MARGIN = 0.02   # m: a kit's walls this far inside the hull

Cabin = dict[str, float]   # 'left', 'right', 'up', 'down', 'ahead', 'back': metres to the hull (inf: open)


class CabinTooSmall(ValueError):
    """The eye of a fit has no room for its kit: the eye is outside the hull or in a wall (fix the fit)."""


def _materials(*names: str) -> dict[str, KitMaterial]:
    return {n: KitMaterial(TEX.format(n)) for n in names}


def _room(cabin: Cabin, want: Cabin, least: Cabin, kit: str) -> Cabin:
    """Each side's wall: where the kit wants it, pulled in to MARGIN short of the hull; a side with less room than
    `least` (or open, inf, where the kit needs a hull) is an error."""
    out = {}
    for side, w in want.items():
        room = cabin[side] - MARGIN
        if not math.isfinite(cabin[side]) or room < least[side]:
            raise CabinTooSmall(f'{kit}: {side} {cabin[side]:.2f} m (needs {least[side] + MARGIN:.2f}): {cabin}')
        out[side] = min(w, room)
    return out
