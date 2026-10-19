"""Finite resident cue fallbacks when the optional EDF4.1 sound bank is absent."""
from __future__ import annotations

import json
from collections import defaultdict
from typing import TYPE_CHECKING

import dsgo

if TYPE_CHECKING:
    import make_edf5_weapons as m5

Path = tuple[str | int, ...]
UNBOUNDED = 0xffffffff


def _leaves(value: object, path: Path = ()) -> list[tuple[Path, object]]:
    """Equivalent field paths for converted DSGO and native plain SGO."""
    if isinstance(value, dsgo.Node):
        return [leaf for i, child in enumerate(value.items)
                for leaf in _leaves(child, path + (value.names.get(i, i),))]
    if isinstance(value, dict):
        return [leaf for key, child in value.items() for leaf in _leaves(child, path + (key,))]
    if isinstance(value, list):
        return [leaf for i, child in enumerate(value) for leaf in _leaves(child, path + (i,))]
    return [(path, value)]


def build(weapons: list[dict], e6: m5.Edf6, source_cues: set[str],
          resident_lengths: dict[str, int]) -> dict:
    """Choose one finite resident fallback per missing source cue actually used.

    Keep a finite original template choice first; otherwise use the same field of
    the same category's native weapons, preferring class, ammo and nearest level.
    All uses of a source name share one deterministic choice. Preserve source
    names and modes in the weapon; lookup tries stock/source bank before this.
    Fail if any used cue has no safe candidate instead of publishing a partial map.
    """
    finite = {cue for cue, length in resident_lengths.items()
              if type(length) is int and 0 < length < UNBOUNDED}
    choices: dict[tuple[int, Path], list[tuple[str, str, str, float, str]]] = defaultdict(list)
    for row in e6.rows:
        if float(row[3]) != 1.0:
            continue
        native = e6.weapon(row[1].rsplit('/', 1)[-1])
        for path, cue in _leaves(native):
            if isinstance(cue, str) and cue in finite:
                choices[int(row[2]), path].append((row[0], native.get('xgs_scene_object_class', ''),
                                                 native.get('AmmoClass', ''), float(row[4]), cue))
    candidates: dict[str, list[tuple[tuple, dict]]] = defaultdict(list)
    needed: set[str] = set()
    for weapon in weapons:
        leaves = _leaves(dsgo.load(weapon['weapon']))
        ammo = next((value for path, value in leaves if path == ('AmmoClass',)), '')
        swaps = weapon.get('cues', {})
        for path, value in leaves:
            if not isinstance(value, str):
                continue
            cue = swaps.get(value, value)
            if cue not in source_cues or cue in resident_lengths:
                continue
            needed.add(cue)
            for template, cls, native_ammo, level, target in choices[int(weapon['category']), path]:
                rank = (template != weapon['template'], cls != weapon['class'], native_ammo != ammo,
                        abs(level - float(weapon['level'])), template, target, json.dumps(path), weapon['id'])
                evidence = {'cue': target, 'length_ms': resident_lengths[target],
                            'template': template, 'field': list(path)}
                candidates[cue].append((rank, evidence))
    missing = needed - candidates.keys()
    if missing:
        raise ValueError('no finite resident same-field fallback for EDF4.1 cues: ' + ', '.join(sorted(missing)))
    return {cue: min(candidates[cue], key=lambda candidate: candidate[0])[1] for cue in sorted(needed)}


def render(mapping: dict) -> str:
    """Native pair rows from registry evidence, without reading game files."""
    lines = ['// Generated from edf41port/weapons.json sound_fallbacks; do not edit.']
    for source, evidence in sorted(mapping.items()):
        target, length = evidence['cue'], evidence['length_ms']
        if not isinstance(source, str) or not isinstance(target, str) or not source or not target:
            raise ValueError('sound fallback names must be nonempty strings')
        if type(length) is not int or not 0 < length < UNBOUNDED:
            raise ValueError(f'sound fallback {source!r} has no finite authored length')
        if any(not name.isascii() or any(ord(c) < 32 for c in name) for name in (source, target)):
            raise ValueError('sound fallback cue names must be printable ASCII')
        lines.append(f'    {{L{json.dumps(source)}, L{json.dumps(target)}}},')
    return '\n'.join(lines) + '\n'
