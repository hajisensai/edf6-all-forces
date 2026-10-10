"""The cue names a CRI ACB sound bank holds (the game's SOUND/PC/*.ACB). Read only.

An ACB is an @UTF table (pylib/cpk.py parse_utf) whose one row has a CueNameTable column: a data block that is itself an
@UTF table with a CueName per cue. A weapon or object SGO names its sounds by cue; a cue no loaded bank holds plays
nothing (pylib/vcobjects.py), so tools that carry a sound over from an earlier game check the name against these.
"""
from __future__ import annotations

import os
import struct

import cpk


def _row(block: bytes) -> tuple[dict, bytes]:
    """The first row of an @UTF table, data columns as their bytes."""
    if block[:4] != b'@UTF':
        block = cpk._unmask(block)
    body = block[8:8 + struct.unpack_from('>I', block, 4)[0]]
    data_at = struct.unpack_from('>I', body, 8)[0]
    _name, _cols, count, rows = cpk.parse_utf(block)
    if not count:
        return {}, body
    row = next(rows())
    return {k: body[data_at + v[0]:data_at + v[0] + v[1]] if isinstance(v, tuple) else v for k, v in row.items()}, body


def cue_names(data: bytes) -> list[str]:
    """Every cue name of an ACB file's bytes."""
    head, _ = _row(data)
    table = head.get('CueNameTable')
    if not isinstance(table, bytes) or len(table) < 8:
        return []
    block = table if table[:4] == b'@UTF' else cpk._unmask(table)
    _name, _cols, count, rows = cpk.parse_utf(block)
    return [r['CueName'] for r in rows()]


def game_cues(game_root: str) -> set[str]:
    """Every cue of every bank in <game>/SOUND/PC."""
    folder = os.path.join(game_root, 'SOUND', 'PC')
    out: set[str] = set()
    for name in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        if name.upper().endswith('.ACB'):
            with open(os.path.join(folder, name), 'rb') as f:
                out.update(cue_names(f.read()))
    return out
