"""A long test's units split into shards of about equal measured time (eng-2, docs/reviews/2026-11-24-optimization-audit.md).

The units' seconds come from a committed table the test itself writes when asked (a sequential run on the developer's
machine, --seconds): dealt round instead, the shards were 357/348/175/119 s (spawn_catalog) and 81..216 s
(cockpit_build). Only the balance depends on the table: a unit it does not list counts MISSING seconds, a stale value
costs a little balance, never a check. The split is deterministic, so every shard computes the same parts.
"""
from __future__ import annotations

import json
from typing import Callable, Sequence, TypeVar

T = TypeVar('T')
MISSING = 0.1


def pack(units: Sequence[T], count: int, seconds: dict[str, float], name: Callable[[T], str]) -> list[list[T]]:
    """`units` in `count` parts: longest first, each onto the part with the least time so far (ties: the lower part).
    Each part keeps its units in their original order."""
    order = {id(u): i for i, u in enumerate(units)}
    parts: list[list[T]] = [[] for _ in range(count)]
    load = [0.0] * count
    for u in sorted(units, key=lambda u: (-seconds.get(name(u), MISSING), name(u))):
        i = min(range(count), key=lambda k: (load[k], k))
        parts[i].append(u)
        load[i] += seconds.get(name(u), MISSING)
    return [sorted(p, key=lambda u: order[id(u)]) for p in parts]


def load(path: str) -> dict[str, float]:
    """A table's seconds by unit name; {} when it is not there (the units then deal evenly by count)."""
    try:
        with open(path, encoding='utf-8') as f:
            return json.load(f)['seconds']
    except FileNotFoundError:
        return {}


def save(path: str, about: str, seconds: dict[str, float]) -> None:
    body = {'about': about, 'seconds': {k: round(v, 1) for k, v in sorted(seconds.items())}}
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(body, f, ensure_ascii=False, indent=1)
        f.write('\n')
