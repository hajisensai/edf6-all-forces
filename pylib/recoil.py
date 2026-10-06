"""A vehicle gun mount's body recoil, and making a mission vehicle's mounts the player's (docs/recoil-re.md).

Every gun mount in a vehicle setup is [weapon SGO, recoil, turret parameters, ...]; `recoil` is the BodyRecoil (or,
tagged, ['BodyRecoil' / 'AimRecoil', [...]]) spec [push, kick]. A call carries the setup the player's vehicle gets
(under its Ammo_CustomParameter); an OBJECT SGO's mission_setup is what a mission-placed (NPC or boardable) one gets.
The rule (autoturret/tools/npc_recoil.py for the stock mission files, testrange/gen.py for the range's): every
mission mount takes the recoil of the same gun in the player's call. "The same gun" is checked (candidates): the
mount's weapon, that weapon without its `_ai` part (the stock AI copies' naming), or the player gun the object's own
vehicle_setup puts in that slot.

Both file formats (classic SGO, DSGO) are read and written back in their own format; numbers compare as float32,
which both store.
"""
from __future__ import annotations

import re
import struct
from typing import Callable, Iterator

import dsgo
import sgo

Mount = tuple[str, object]   # (weapon file name, lower case; recoil spec as plain Python)


AI_PART = re.compile(r'_ai(?=[_.])', re.IGNORECASE)


def base(path: str) -> str:
    return path.replace('\\', '/').split('/')[-1].lower()


def plain(v: object) -> object:
    """A spec value as plain Python, from either file format (floats as float32, as both store them)."""
    if isinstance(v, dsgo.Node):
        return [plain(c) for c in v.items]
    if isinstance(v, list):
        return [plain(c) for c in v]
    if isinstance(v, sgo.Float):
        return v.value
    if isinstance(v, float):
        return struct.unpack('<f', struct.pack('<f', v))[0]
    return v


def _as_sgo(v: object) -> sgo.Value:
    if isinstance(v, list):
        return [_as_sgo(c) for c in v]
    if isinstance(v, float):
        return sgo.Float(struct.pack('<f', v))
    return v


def _as_dsgo(v: object) -> dsgo.Value:
    return dsgo.Node([_as_dsgo(c) for c in v]) if isinstance(v, list) else v


def items_of(v: object) -> list | None:
    if isinstance(v, dsgo.Node):
        return v.items
    return v if isinstance(v, list) else None


def guns_of(setup: object, where: str) -> list:
    """The setup's weapon list: [2] in a vehicle's ([multipliers, vehicle params, weapons, ...]), the last entry
    in a bike's ([multipliers, bike params, fuel, weapons]: the Freed bike's, the sidecar's)."""
    items = items_of(setup)
    if items and len(items) > 2:
        for at in (2, len(items) - 1):
            guns = items_of(items[at])
            if guns and any(gun_of(m) for m in guns):
                return guns
    raise SystemExit(f'{where}: not the vehicle setup layout this tool knows')


def gun_of(mount: object) -> list | None:
    """The mount's [weapon SGO, recoil, ...] items, or None for an empty mount ([0]) or a non-gun entry (a bike's
    fuel tank [SGO] alone; the Depth Crawler's [light bone, 0.7] lights)."""
    items = items_of(mount)
    return (items if items and len(items) > 1 and isinstance(items[0], str) and items[0].lower().endswith('.sgo')
            else None)


def _is_setup(n: object) -> bool:
    items = items_of(n)
    if not items or len(items) < 3 or not items_of(items[0]) or not isinstance(plain(items_of(items[0])[0]), float):
        return False
    try:
        guns_of(n, '')
        return True
    except SystemExit:
        return False


def _walk(n: object) -> Iterator[object]:
    if items_of(n) is not None:
        yield n
        for c in items_of(n):
            yield from _walk(c)


def mounts_of(call: bytes, where: str) -> list[Mount | None]:
    """(weapon, recoil) of each slot of a call's vehicle setup (the one setup under its Ammo_CustomParameter),
    None for an empty slot."""
    root = dsgo.parse(call).root if call[:4] == b'DSGO' else sgo.read(call)[1]
    custom = root.get('Ammo_CustomParameter')
    found = [n for n in _walk(custom) if _is_setup(n)]
    if len(found) != 1:
        raise SystemExit(f'{where}: {len(found)} vehicle setups in the call (expected one)')
    return [(base(g[0]), plain(g[1])) if g else None for g in (gun_of(m) for m in guns_of(found[0], where))]


def candidates(weapon: str, player_slot: str | None) -> list[str]:
    """The player gun a mission mount's weapon stands for: itself, its AI copy's original (the stock names
    the AI copy with an `_ai` part), the player gun the object's own vehicle_setup has in that slot."""
    out = [weapon, AI_PART.sub('', weapon)]
    return out + [player_slot] if player_slot else out


def align(mission: list, vehicle: list | None, mounts: list[Mount | None], where: str,
          convert: Callable[[object], object]) -> int:
    """Sets each mission mount's recoil to the call's for the same gun; the number of mounts changed."""
    changed = 0
    for i, mount in enumerate(mission):
        gun = gun_of(mount)
        if gun is None:
            continue
        if i >= len(mounts) or mounts[i] is None:
            raise SystemExit(f'{where}: mount {i} has no gun in the player call')
        player = gun_of(vehicle[i]) if vehicle and i < len(vehicle) else None
        weapon, recoil = mounts[i]
        if weapon not in candidates(base(gun[0]), base(player[0]) if player else None):
            raise SystemExit(f'{where}: mount {i} is {base(gun[0])}, the call mounts {weapon} there')
        if plain(gun[1]) != recoil:
            gun[1] = convert(recoil)
            changed += 1
    return changed


def align_data(data: bytes, mounts: list[Mount | None], where: str) -> tuple[bytes, int]:
    """A vehicle SGO (classic or DSGO) with each mission_setup mount's recoil set to `mounts`' for the same gun,
    in its own format, and the number of mounts changed (0: `data` itself)."""
    if data[:4] == b'DSGO':
        doc = dsgo.parse(data)
        names = set(doc.root.names.values())
        vehicle = doc.root.get('vehicle_setup') if 'vehicle_setup' in names else None
        n = align(guns_of(doc.root.get('mission_setup'), where), guns_of(vehicle, where) if vehicle else None,
                  mounts, where, _as_dsgo)
        return (dsgo.write(doc) if n else data), n
    version, members = sgo.read(data)
    if sgo.read(sgo.write_depth_first(version, members)) != (version, members):
        raise SystemExit(f'{where} does not round-trip')
    vehicle = members.get('vehicle_setup')
    n = align(guns_of(members['mission_setup'], where), guns_of(vehicle, where) if vehicle else None,
              mounts, where, _as_sgo)
    return (sgo.write_depth_first(version, members) if n else data), n
