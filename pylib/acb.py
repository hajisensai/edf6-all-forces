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


def cue_lengths(data: bytes) -> dict[str, int]:
    """Cue name -> authored length in milliseconds; 0xffffffff is unbounded/unknown length.

    The sentinel alone does not prove a loop: inspect the cue graph and source playback owner.
    This is cue metadata, not a waveform LoopFlag; a finite sequence can contain looped waves
    terminated by sequence commands. Never infer playback ownership from individual wave flags.
    """
    head, _ = _row(data)
    tables = []
    for key in ('CueNameTable', 'CueTable'):
        table = head.get(key)
        if not isinstance(table, bytes) or len(table) < 8:
            raise ValueError(f'ACB has no {key}')
        block = table if table[:4] == b'@UTF' else cpk._unmask(table)
        tables.append(list(cpk.parse_utf(block)[3]()))
    names, cues = tables
    out = {}
    for entry in names:
        index = entry['CueIndex']
        if not isinstance(index, int) or not 0 <= index < len(cues):
            raise ValueError(f'ACB cue {entry["CueName"]!r} has invalid index {index}')
        out[entry['CueName']] = cues[index]['Length']
    return out
